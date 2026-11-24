#!/usr/bin/env python3
"""Build the Jazz Band sound packs (jbpiano, jbbass, jbguitar, jbdrums .op1p) from free sample libraries.

    python3 make_packs.py <sources dir> <jbpack template .op1p> <out dir>

<sources dir> holds the sfzinstruments git checkouts (only the files listed in PACKS are needed):
  SalamanderGrandPiano (CC-BY 3.0), karoryfer.meatbass (CC0), karoryfer.shinyguitar (CC0), virtuosity_drums (CC0).
Each pack: the jbpack template renamed, its header and zone table filled, IMA ADPCM audio appended after the image
(layout: plugins/jbpack/jbpack.h). Every zone is decoded again with the same decoder as the plugin and its signal to
noise ratio reported. Writes <out>/<name>.op1p, <out>/packs.json (contents, sizes, quality) and preview WAVs."""
import binascii, ctypes, json, math, os, struct, sys
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly, butter, sosfilt

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = ctypes.CDLL(os.path.join(HERE, 'adpcm.so'))
LIB.encode.restype = ctypes.c_double
D_KICK, D_SNARE, D_RIDE, D_HAT, D_CRASH, D_STICK, D_TOMH, D_TOML, D_SHAKER, D_CONGA = range(10)
INST = {'drums': 0, 'bass': 1, 'piano': 2, 'guitar': 3, 'nylon': 4}   # nylon: Joao Gilberto only (1.5)
NOTE = {'c': 0, 'db': 1, 'c#': 1, 'd': 2, 'eb': 3, 'd#': 3, 'e': 4, 'f': 5, 'gb': 6, 'f#': 6, 'g': 7, 'ab': 8, 'g#': 8, 'a': 9, 'bb': 10, 'a#': 10, 'b': 11}


def midi_of(name):                                   # 'D#2', 'eb3' -> MIDI (C4 = 60)
    n = name.lower(); i = len(n.rstrip('0123456789'))
    return NOTE[n[:i]] + 12 * (int(n[i:]) + 1)


def load(path, rate):
    x, sr = sf.read(path, always_2d=True)
    x = x.mean(axis=1)
    if sr != rate:
        g = math.gcd(sr, rate); x = resample_poly(x, rate // g, sr // g)
    return x


def f0(x, sr, lo=25.0, hi=1500.0):                   # YIN on 0.08 .. 0.6 s after the start
    a, b = int(0.08 * sr), int(0.6 * sr)
    seg = x[a:b] - np.mean(x[a:b]); W = len(seg) // 2
    tmin, tmax = int(sr / hi), min(int(sr / lo), W - 1)
    d = np.array([np.sum((seg[:W] - seg[t:t + W]) ** 2) for t in range(tmax + 1)])
    cm = np.ones_like(d); cm[1:] = d[1:] * np.arange(1, tmax + 1) / np.maximum(np.cumsum(d[1:]), 1e-12)
    t = next((t for t in range(tmin, tmax) if cm[t] < 0.15 and cm[t] <= cm[t + 1]), int(np.argmin(cm[tmin:tmax]) + tmin))
    if 0 < t < tmax:                                  # parabolic interpolation
        p = (cm[t - 1] - cm[t + 1]) / (2 * (cm[t - 1] - 2 * cm[t] + cm[t + 1]) or 1); t = t + p
    return sr / t, float(cm[int(round(t))] if int(round(t)) < len(cm) else 1)


def shape(x, sr, seconds, fade=0.25):                 # from the attack, at most `seconds`, quiet tail cut, faded out
    pk = np.max(np.abs(x)) or 1.0
    on = int(np.argmax(np.abs(x) > 0.01 * pk)); on = max(0, on - int(0.002 * sr))
    x = x[on:]
    loud = np.nonzero(np.abs(x) > 0.0015 * pk)[0]
    end = min(len(x), int(seconds * sr), (loud[-1] + int(0.05 * sr)) if len(loud) else len(x))
    x = x[:end].copy()
    nf = min(int(fade * sr), end // 3)
    if nf > 8: x[-nf:] *= 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, nf))
    return x


MU = None
def mulaw_table():                                     # G.711 mu-law: byte -> int16 (the plugin has the same table)
    t = []
    for b in range(256):
        u = ~b & 0xFF; sign, exp, man = u & 0x80, (u >> 4) & 7, u & 15
        v = (((man << 3) + 0x84) << exp) - 0x84
        t.append(-v if sign else v)
    return np.array(t, dtype=np.int16)


def mulaw(x16):                                        # nearest table entry for every sample (exact search, small table)
    global MU
    if MU is None: MU = mulaw_table()
    order = np.argsort(MU); vals = MU[order].astype(np.int32)
    x = x16.astype(np.int32); i = np.clip(np.searchsorted(vals, x), 1, 255)
    pick = np.where(np.abs(vals[i - 1] - x) <= np.abs(vals[i] - x), i - 1, i)
    codes = order[pick].astype(np.uint8); y = MU[codes].astype(np.float64)
    snr = 10 * math.log10(np.sum(x16.astype(np.float64) ** 2) / max(np.sum((y - x16) ** 2), 1))
    return codes.tobytes(), snr, y


def adpcm(x16):
    n = len(x16); buf = (ctypes.c_uint8 * ((n + 1) // 2))(); arr = (ctypes.c_int16 * n)(*x16.tolist())
    best, bidx = None, 0
    probe = min(n, 384)
    for idx in range(0, 89, 2):                        # start step index: the best fit for the attack
        tmp = (ctypes.c_uint8 * ((probe + 1) // 2))()
        e = LIB.encode(arr, probe, tmp, int(x16[0]), idx)
        if best is None or e < best: best, bidx = e, idx
    LIB.encode(arr, n, buf, int(x16[0]), bidx)
    y = (ctypes.c_int16 * n)(); LIB.decode(buf, n, y, int(x16[0]), bidx)
    y = np.frombuffer(y, dtype=np.int16).astype(np.float64)
    snr = 10 * math.log10(np.sum(x16.astype(np.float64) ** 2) / max(np.sum((y - x16) ** 2), 1))
    return bytes(buf), int(x16[0]), bidx, snr, y


S = lambda *p: os.path.join(SRC, *p)
PIANO = "A1 C2 D#2 F#2 A2 C3 D#3 F#3 A3 C4 D#4 F#4 A4 C5 D#5 F#5 A5 C6".split()
BASS = "eb1 gb1 a1 c2 eb2 gb2 a2 c3 eb3 gb3 c4".split()   # a0, c1: below the bass lines (E1 = 28 up)
GUITAR = "e2 gb2 a2 c3 eb3 gb3 a3 c4 eb4 gb4 a4 c5 eb5".split()


def zones_piano():
    for n in PIANO:
        m = midi_of(n); sec = 2.6 if m < 48 else 2.1 if m < 66 else 1.5
        for v, lo, hi in ((6, 0, 72), (11, 73, 127)):
            yield dict(key=m, vlo=lo, vhi=hi, rr=0, sec=sec, src=[(S('SalamanderGrandPiano', 'Samples', f'{n}v{v}.flac'), 1.0)])


def zones_bass():
    for n in BASS:
        for v, lo, hi, rrs in ((3, 0, 103, (1, 2)), (4, 104, 127, (1,))):
            for k, r in enumerate(rrs):
                yield dict(key=midi_of(n), vlo=lo, vhi=hi, rr=k, sec=1.4, name=n,
                           src=[(S('karoryfer.meatbass', 'Samples', 'pizz', f'{n}_vl{v}_rr{r}.wav'), 1.0)])


def zones_guitar():
    for n in GUITAR:
        for v, lo, hi in ((2, 0, 58), (3, 59, 127)):   # pickup and microphone of the same take, blended
            yield dict(key=midi_of(n), vlo=lo, vhi=hi, rr=0, sec=1.1, tone=5200,
                       src=[(S('karoryfer.shinyguitar', 'Samples', 'electric', f'{n}_vl{v}_rr1_2.wav'), 0.75),
                            (S('karoryfer.shinyguitar', 'Samples', 'acoustic', f'{n}_vl{v}_rr1_1.wav'), 0.35)])


NYLON = [36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76]    # every major third, C2-E5: Gilberto's thumb and chord range


def zones_nylon():                                     # FreePats Spanish classical guitar (CC0): one dynamic, chromatic
    d = S('freepats_spanish_guitar', 'samples')
    have = {midi_of(f[:-5].lower()): f for f in os.listdir(d) if f.endswith('.flac')}
    for k in NYLON:
        root = min(have, key=lambda m: (abs(m - k), m))
        yield dict(key=root, vlo=0, vhi=127, rr=0, sec=1.2, fade=0.3, tone=7000, src=[(os.path.join(d, have[root]), 1.0)])


def zones_drums():
    MIX = {'kick': {'oh': 1.0, 'room': 0.5, 'kickmic': 0.9}, 'snare': {'oh': 1.0, 'room': 0.5, 'snaremic': 0.7},
           'ride': {'oh': 1.0, 'room': 0.5}, 'hh': {'oh': 1.0, 'room': 0.4}, 'crash': {'oh': 1.0, 'room': 0.5},
           'htom': {'oh': 1.0, 'room': 0.5}, 'ltom': {'oh': 1.0, 'room': 0.5}}
    def hit(piece, name):
        return [(S('virtuosity_drums', 'Samples', mic, piece, f'{mic}_{name}.flac'), g) for mic, g in MIX[piece].items()]
    for v, lo, hi in ((1, 0, 74), (2, 75, 127)):       # skip notes / beats, 2 takes each; cymbals: 8-bit mu-law
        for k, r in enumerate((1, 2)): yield dict(key=D_RIDE, vlo=lo, vhi=hi, rr=k, sec=2.0, fade=0.6, mulaw=1, src=hit('ride', f'ride_ride_vl{v}_rr{r}'))
    for v, lo, hi in ((2, 0, 74), (3, 75, 127)): yield dict(key=D_HAT, vlo=lo, vhi=hi, rr=0, sec=0.5, mulaw=1, src=hit('hh', f'hh_pedal_vl{v}_rr1'))
    for v, lo, hi in ((6, 0, 40), (14, 41, 60), (22, 61, 85), (30, 86, 127)):
        yield dict(key=D_SNARE, vlo=lo, vhi=hi, rr=0, sec=0.6, src=hit('snare', f'snare_center_vl{v}'))
    for v, lo, hi in ((8, 0, 80), (13, 81, 127)): yield dict(key=D_STICK, vlo=lo, vhi=hi, rr=0, sec=0.35, fade=0.1, src=hit('snare', f'snare_crossstick_vl{v}'))
    for v, lo, hi in ((2, 0, 60), (4, 61, 127)):
        for k, r in enumerate((1, 2)): yield dict(key=D_KICK, vlo=lo, vhi=hi, rr=k, sec=0.7, src=hit('kick', f'kick_snoff_vl{v}_rr{r}'))
    yield dict(key=D_CRASH, vlo=0, vhi=127, rr=0, sec=2.4, fade=0.8, mulaw=1, src=hit('crash', 'crash_crash_vl3_rr1'))
    # 1.5: toms (Max Roach), shaker and conga (bossa nova, Airto)
    for v, lo, hi in ((6, 0, 80), (13, 81, 127)):
        yield dict(key=D_TOMH, vlo=lo, vhi=hi, rr=0, sec=0.9, fade=0.3, src=hit('htom', f'htom_center_vl{v}'))
        yield dict(key=D_TOML, vlo=lo, vhi=hi, rr=0, sec=1.0, fade=0.3, src=hit('ltom', f'ltom_center_vl{v}'))
    perc = lambda d, f: [(S('virtuosity_drums', 'Samples', 'perc', 'mid', d, f), 1.0)]
    for k, r in enumerate((1, 2)):                     # up strokes soft, down strokes accented
        yield dict(key=D_SHAKER, vlo=0, vhi=70, rr=k, sec=0.3, fade=0.08, mulaw=1, src=perc('shaker', f'LShaker_Shake1U_rr{r}_Mid.wav'))
        yield dict(key=D_SHAKER, vlo=71, vhi=127, rr=k, sec=0.3, fade=0.08, mulaw=1, src=perc('shaker', f'LShaker_Shake1D_rr{r}_Mid.wav'))
        yield dict(key=D_CONGA, vlo=0, vhi=95, rr=k, sec=0.6, fade=0.2, src=perc('conga', f'Conga_22_HitN_51_100_rr{r}_Mid.wav'))
    yield dict(key=D_CONGA, vlo=96, vhi=127, rr=0, sec=0.6, fade=0.2, src=perc('conga', 'Conga_22_HitN_101_127_rr1_Mid.wav'))


PACKS = [
    ('jbpiano', 'piano', 32000, zones_piano, 'SALAMANDER GRAND PIANO - ALEXANDER HOLM - CC-BY 3.0'),
    ('jbbass', 'bass', 22050, zones_bass, 'MEATBASS - KAROYFER SAMPLES - CC0'.replace('KAROYFER', 'KARORYFER')),
    ('jbguitar', 'guitar', 32000, zones_guitar, 'SHINYGUITAR - KARORYFER SAMPLES - CC0'),
    ('jbdrums', 'drums', 44100, zones_drums, 'VIRTUOSITY DRUMS - VERSILIAN STUDIOS - CC0'),
    ('jbnylon', 'nylon', 22050, zones_nylon, 'SPANISH CLASSICAL GUITAR - FREEPATS - CC0'),
]


def build(name, inst, rate, gen, credit, template, out):
    zs = list(gen())
    for z in zs:                                       # read, mix, shape
        x = None
        for path, g in z['src']:
            y = load(path, rate) * g
            x = y if x is None else (np.pad(x, (0, max(0, len(y) - len(x)))) + np.pad(y, (0, max(0, len(x) - len(y)))))
        if z.get('tone'): x = sosfilt(butter(2, z['tone'], fs=rate, output='sos'), x)
        z['x'] = shape(x, rate, z['sec'], z.get('fade', 0.25))
        z['cents'] = 0
        if INST[inst]:                                 # pitched: check the root against the audio
            f, conf = f0(z['x'], rate, 25 if inst == 'bass' else 60, 1500)
            m = 69 + 12 * math.log2(f / 440)
            near = round(m)
            c = (m - z['key']) * 100                   # the file names are right (checked); the audio gives the fine tuning
            if conf < 0.3 and abs(c) <= (20 if inst == 'bass' else 40): z['cents'] = c   # bass pizz readings are noisy
            z['f0'], z['conf'] = f, conf
    groups = {}                                        # level: pitched packs as one, drums per piece
    for z in zs: groups.setdefault(z['key'] if inst == 'drums' else 0, []).append(z)
    for g in groups.values():
        pk = max(np.max(np.abs(z['x'])) for z in g)
        for z in g: z['x'] = z['x'] * (0.89 / pk)
    data, table, report = bytearray(), bytearray(), []
    for z in zs:
        x16 = np.clip(np.round(z['x'] * 32767), -32768, 32767).astype(np.int16)
        codec = z.get('mulaw', 0)
        if codec: (enc, snr, y), pred, idx = mulaw(x16), 0, 0
        else: enc, pred, idx, snr, y = adpcm(x16)
        z['dec'] = y
        step = round(65536 * rate / 44100 * 2 ** (-z['cents'] / 1200))
        table += struct.pack('<BBBBIhBBII', z['key'], z['vlo'], z['vhi'], z['rr'], step, pred, idx, codec, len(data), len(x16))
        data += enc
        report.append(dict(codec='mulaw' if codec else 'adpcm', key=z['key'], vel=[z['vlo'], z['vhi']], rr=z['rr'], seconds=round(len(x16) / rate, 3), snr_db=round(snr, 1),
                           cents=round(z['cents'], 1), **({'f0': round(z['f0'], 2), 'yin': round(z['conf'], 3)} if 'f0' in z else {})))
    assert len(zs) <= 160
    # the pack file: the template with header and zone table filled and the audio appended to the image
    f = bytearray(template)
    tm, bin_off, bin_len, crc = struct.unpack_from('<4I', f, len(f) - 16)
    assert tm == 0x5431504F and binascii.crc32(f[bin_off:bin_off + bin_len]) == crc
    img = bin_off + 76
    image_size, bss_size, entry_off = struct.unpack_from('<3I', f, bin_off + 32)
    assert bss_size == 0, 'the template must have no .bss: audio goes right after the image'
    e = img + entry_off
    assert struct.unpack_from('<2I', f, e + 16) == (0x4B50424A, 0x31444E53)
    sum_ = 2166136261
    for b in bytes(table) + bytes(data): sum_ = ((sum_ ^ b) * 16777619) & 0xFFFFFFFF
    struct.pack_into('<6I', f, e + 24, INST[inst], len(zs), image_size - entry_off, len(data), sum_, 1)
    cr = credit.encode('ascii')[:95]; f[e + 48:e + 48 + 96] = cr + bytes(96 - len(cr))
    f[e + 144:e + 144 + len(table)] = table
    pad = bytes((-len(data)) % 4)
    new_image = bytes(f[img:img + image_size]) + bytes(data) + pad
    assert len(new_image) <= 2048 * 1024, f'{name}: image over op1r\'s 2 MB plugin limit'
    hdr = bytearray(f[bin_off:img])
    hdr[16:32] = name.encode().ljust(16, b'\0'); struct.pack_into('<I', hdr, 32, len(new_image))
    rest = bytes(f[img + image_size:bin_off + bin_len])           # relocations, imports, metadata
    binpart = bytes(hdr) + new_image + rest
    outb = bytes(f[:bin_off]) + binpart
    outb += struct.pack('<4I', 0x5431504F, bin_off, len(binpart), binascii.crc32(binpart))
    open(os.path.join(out, name + '.op1p'), 'wb').write(outb)
    # a preview: every zone decoded, one after another
    pv = np.concatenate([np.concatenate([z['dec'] / 32768, np.zeros(int(0.15 * rate))]) for z in zs])
    sf.write(os.path.join(out, name + '-preview.wav'), pv.astype(np.float32), rate)
    return dict(name=name, instrument=inst, rate=rate, zones=len(zs), audio_bytes=len(data), file_bytes=len(outb),
                ram_bytes=len(new_image), seconds=round(sum(len(z['x']) for z in zs) / rate, 1),
                min_snr_db=min(r['snr_db'] for r in report), credit=credit, detail=report)


if __name__ == '__main__':
    SRC, tpl, out = sys.argv[1], open(sys.argv[2], 'rb').read(), sys.argv[3]
    os.makedirs(out, exist_ok=True)
    only = sys.argv[4:] or [p[0] for p in PACKS]
    res = []
    for p in PACKS:
        if p[0] in only:
            r = build(*p, tpl, out); res.append(r)
            print(f"{r['name']:9s} {r['zones']:3d} zones  {r['seconds']:6.1f} s at {r['rate']} Hz  {r['file_bytes'] / 1024:7.1f} KB  "
                  f"min SNR {r['min_snr_db']:.1f} dB")
    json.dump(res, open(os.path.join(out, 'packs.json'), 'w'), indent=1)
    print(f"total {sum(r['ram_bytes'] for r in res) / 1024:.0f} KB of plugin memory")
