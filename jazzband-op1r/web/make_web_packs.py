#!/usr/bin/env python3
"""Sound packs for the web build: the same free sample libraries as packs/make_packs.py, without the OP-1's 4 MB
limit. 16-bit PCM instead of 4-bit ADPCM (no coding noise), three piano dynamics, longer sustains, a 32 kHz bass,
and a recorded alto saxophone for the lead (Karoryfer Weresax, CC0).

    python3 web/make_web_packs.py <sources dir> web/packs [names...]

<sources dir>: checkouts of sfzinstruments/{SalamanderGrandPiano, karoryfer.meatbass, karoryfer.shinyguitar,
virtuosity_drums, karoryfer.weresax}. Writes <name>.jbp: a jbpack_t (plugins/jbpack/jbpack.h, wasm32 layout) and
the audio, which web/jb_web.c maps as it is."""
import math, os, re, struct, sys, json
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'packs'))
import make_packs as mp

LEAD = 5
S = lambda *p: os.path.join(mp.SRC, *p)


def zones_piano():
    for n in mp.PIANO:
        m = mp.midi_of(n); sec = 3.8 if m < 48 else 3.0 if m < 66 else 2.2
        for v, lo, hi in ((4, 0, 62), (8, 63, 92), (12, 93, 127)):
            yield dict(key=m, vlo=lo, vhi=hi, rr=0, sec=sec, fade=0.5, src=[(S('SalamanderGrandPiano', 'Samples', f'{n}v{v}.flac'), 1.0)])


def zones_bass():
    for z in mp.zones_bass():
        z['sec'] = 2.2; yield z


def zones_guitar():
    for z in mp.zones_guitar():
        z['sec'] = 1.8; z['tone'] = 6500; yield z


GLEAD = 6
GLEAD_NOTES = "c3 eb3 gb3 a3 c4 eb4 gb4 a4 c5 eb5 gb5 a5 c6".split()


def zones_glead():
    """The melody guitar. The library's "electric" takes are the archtop's pickup straight into the desk (no
    amplifier): alone they sound plinky and synthetic. Each note here is that pickup through a modelled jazz amp and
    speaker (amp_cab), with a little of the room microphone of the same take for the wood and air, three different
    takes of every note so repeated notes are not identical, firm picking."""
    for n in GLEAD_NOTES:
        for rr in (1, 2, 3):
            yield dict(key=mp.midi_of(n), vlo=0, vhi=127, rr=rr - 1, sec=3.0, fade=0.6, amp=True,
                       src=[(S('karoryfer.shinyguitar', 'Samples', 'electric', f'{n}_vl3_rr{rr}_2.wav'), 1.0),
                            (S('karoryfer.shinyguitar', 'Samples', 'acoustic', f'{n}_vl3_rr{rr}_1.wav'), 0.0)])


def peak_eq(x, rate, f0, gain_db, q):                   # RBJ cookbook peaking filter
    from scipy.signal import lfilter
    A = 10 ** (gain_db / 40); w = 2 * math.pi * f0 / rate; al = math.sin(w) / (2 * q); c = math.cos(w)
    return lfilter([1 + al * A, -2 * c, 1 - al * A], [1 + al / A, -2 * c, 1 - al / A], x)


def amp_cab(di, mic, rate):
    """A clean jazz amp and a 12-inch speaker, from the pickup's direct signal: the low end tightened, a soft
    valve-like squeeze on the attack (a little asymmetric: some even harmonics), the speaker's mid-range body and
    its roll-off above 4-5 kHz; then 25% of the room microphone for the guitar's own acoustic sound."""
    from scipy.signal import butter, sosfilt
    x = sosfilt(butter(2, 80, 'highpass', fs=rate, output='sos'), di)
    x = x / (np.max(np.abs(x)) + 1e-9)
    d, b = 1.8, 0.12
    x = (np.tanh(d * (x + b)) - np.tanh(d * b)) / np.tanh(d)
    x = peak_eq(x, rate, 140, 2.0, 0.8)                 # cabinet thump
    x = peak_eq(x, rate, 650, 4.0, 0.9)                 # speaker body
    x = peak_eq(x, rate, 2600, -5.0, 1.2)               # the pickup's glassy edge, taken out
    x = sosfilt(butter(4, 4600, fs=rate, output='sos'), x)   # 12-inch speaker roll-off
    m = sosfilt(butter(2, 120, 'highpass', fs=rate, output='sos'), mic)
    m = m / (np.max(np.abs(m)) + 1e-9)
    n = max(len(x), len(m))
    x = np.pad(x, (0, n - len(x))) / (np.max(np.abs(x)) + 1e-9) * float(os.environ.get("GLEAD_AMP", 1)) + float(os.environ.get("GLEAD_MIC", 0.25)) * np.pad(m, (0, n - len(m)))
    return x


def low_shelf(x, rate, f0, gain_db):                     # RBJ cookbook low shelf, slope 1
    from scipy.signal import lfilter
    A = 10 ** (gain_db / 40); w = 2 * math.pi * f0 / rate; c, sn = math.cos(w), math.sin(w); al = sn / 2 * math.sqrt(2)
    b = [A * ((A + 1) - (A - 1) * c + 2 * math.sqrt(A) * al), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - 2 * math.sqrt(A) * al)]
    a = [(A + 1) + (A - 1) * c + 2 * math.sqrt(A) * al, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - 2 * math.sqrt(A) * al]
    return lfilter(b, a, x)


def zones_drums():
    for z in mp.zones_drums():
        z.pop('mulaw', None)
        if z['key'] == mp.D_RIDE: z['sec'] = 3.0
        if z['key'] == mp.D_CRASH: z['sec'] = 3.5
        yield z


def zones_lead():
    d = S('karoryfer.weresax', 'Samples', 'alto')
    names = sorted({f.split('_')[0] for f in os.listdir(d) if f.endswith('_cnd.wav')}, key=mp.midi_of)
    for n in names[::3] + ([names[-1]] if (len(names) - 1) % 3 else []):
        for dyn, lo, hi in (('p', 0, 95), ('f', 96, 127)):
            path = os.path.join(d, f'{n}_{dyn}_rr1_cnd.wav')
            if os.path.exists(path):
                yield dict(key=None, vlo=lo, vhi=hi, rr=0, sec=4.2, fade=0.35, src=[(path, 1.0)], name=n)


PACKS = [
    ('jbpiano', 2, 32000, zones_piano, 'SALAMANDER GRAND PIANO - ALEXANDER HOLM - CC-BY 3.0'),
    ('jbbass', 1, 32000, zones_bass, 'MEATBASS - KARORYFER SAMPLES - CC0'),
    ('jbguitar', 3, 32000, zones_guitar, 'SHINYGUITAR - KARORYFER SAMPLES - CC0'),
    ('jbdrums', 0, 44100, zones_drums, 'VIRTUOSITY DRUMS - VERSILIAN STUDIOS - CC0'),
    ('jblead', LEAD, 32000, zones_lead, 'WERESAX ALTO SAXOPHONE - KARORYFER SAMPLES - CC0'),
    ('jbgtrlead', GLEAD, 32000, zones_glead, 'SHINYGUITAR PICKUP - KARORYFER SAMPLES - CC0'),
]


def sustain_loop(z, rate, f):
    """A held note: the zone ends inside the steady tone, cross-faded into the audio before the loop start, so the
    engine can jump back there for as long as the note lasts. The loop is a whole number of periods long."""
    x = z['x']
    period = rate / f
    l0 = (int(1.5 * rate) // 16) * 16
    l1 = l0 + int(round(round(1.3 * rate / period) * period))
    n = int(0.2 * rate)
    if l1 > len(x) - int(0.1 * rate) or l0 < n: return
    w = np.linspace(0.0, 1.0, n)
    x = x[:l1].copy()
    x[l1 - n:l1] = x[l1 - n:l1] * np.cos(w * np.pi / 2) + x[l0 - n:l0] * np.sin(w * np.pi / 2)
    z['x'], z['loop'] = x, l0


def build(name, inst, rate, gen, credit, out):
    from scipy.signal import butter, sosfilt
    zs = list(gen())
    for z in zs:
        x = None
        if z.get('amp'):                               # the melody guitar: pickup through an amp model, plus room mic
            x = amp_cab(mp.load(z['src'][0][0], rate), mp.load(z['src'][1][0], rate), rate)
        for path, g in ([] if z.get('amp') else z['src']):
            y = mp.load(path, rate) * g
            x = y if x is None else (np.pad(x, (0, max(0, len(y) - len(x)))) + np.pad(y, (0, max(0, len(x) - len(y)))))
        if z.get('tone'): x = sosfilt(butter(2, z['tone'], fs=rate, output='sos'), x)
        if z.get('warm'): x = low_shelf(x, rate, *z['warm'])
        z['x'] = mp.shape(x, rate, z['sec'], z.get('fade', 0.25))
        z['cents'] = 0; z['loop'] = 0
        if inst:
            f, conf = mp.f0(z['x'], rate, 25 if inst == 1 else 60, 1500)
            m = 69 + 12 * math.log2(f / 440)
            if z['key'] is None:                       # the lead: the root is what the recording sounds
                z['key'] = int(round(m)); z['cents'] = (m - z['key']) * 100 if conf < 0.3 else 0
            else:
                c = (m - z['key']) * 100
                if conf < 0.3 and abs(c) <= (20 if inst == 1 else 40): z['cents'] = c
            z['f0'], z['conf'] = f, conf
            if inst == LEAD: sustain_loop(z, rate, f)
    groups = {}
    for z in zs: groups.setdefault(z['key'] if inst == 0 else 0, []).append(z)
    for g in groups.values():
        if inst == LEAD:                               # one loudness per dynamic, so notes of a line match
            for z in g:
                r = math.sqrt(float(np.mean(z['x'][:rate] ** 2)))
                z['x'] = z['x'] * ((0.16 if z['vhi'] > 100 else 0.12) / max(r, 1e-6))
            pk = max(np.max(np.abs(z['x'])) for z in g)
            if pk > 0.95:
                for z in g: z['x'] = z['x'] * (0.95 / pk)
        else:
            pk = max(np.max(np.abs(z['x'])) for z in g)
            for z in g: z['x'] = z['x'] * (0.89 / pk)
    data, table, report = bytearray(), bytearray(), []
    for z in zs:
        x16 = np.clip(np.round(z['x'] * 32767), -32768, 32767).astype('<i2')
        step = round(65536 * rate / 44100 * 2 ** (-z['cents'] / 1200))
        table += struct.pack('<BBBBIhBBII', z['key'], z['vlo'], z['vhi'], z['rr'], step, z['loop'] // 16, 0, 2, len(data), len(x16))
        data += x16.tobytes()
        report.append(dict(key=z['key'], vel=[z['vlo'], z['vhi']], seconds=round(len(x16) / rate, 2), cents=round(z['cents'], 1),
                           **({'f0': round(z['f0'], 1)} if 'f0' in z else {})))
    assert len(zs) <= 160, len(zs)
    sum_ = 2166136261
    blob = np.frombuffer(bytes(table) + bytes(data), dtype=np.uint8)
    for chunk in np.array_split(blob, max(1, len(blob) // 65536)):      # FNV-1a 32 (python loop per byte is slow: batch)
        for b in chunk.tobytes(): sum_ = ((sum_ ^ b) * 16777619) & 0xFFFFFFFF
    HEAD = 16 + 8 + 24 + 96 + 160 * 20
    head = bytearray(HEAD)
    head[8:12] = bytes([0xCA, 0xC2, 0xD0, 0xCB])
    struct.pack_into('<2I6I', head, 16, 0x4B50424A, 0x31444E53, inst, len(zs), HEAD, len(data), sum_, 2)
    cr = credit.encode('ascii')[:95]; head[48:48 + len(cr)] = cr
    head[144:144 + len(table)] = table
    open(os.path.join(out, name + '.jbp'), 'wb').write(bytes(head) + bytes(data))
    return dict(name=name, rate=rate, zones=len(zs), bytes=HEAD + len(data), seconds=round(len(data) / 2 / rate, 1), detail=report)


if __name__ == '__main__':
    mp.SRC, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    only = sys.argv[3:] or [p[0] for p in PACKS]
    res = []
    for p in PACKS:
        if p[0] in only:
            r = build(*p, out); res.append(r)
            print(f"{r['name']:9s} {r['zones']:3d} zones {r['seconds']:6.1f} s at {r['rate']} Hz {r['bytes'] / 1e6:6.2f} MB", flush=True)
    json.dump(res, open(os.path.join(out, 'web-packs.json'), 'w'), indent=1)
    print(f"total {sum(r['bytes'] for r in res) / 1e6:.1f} MB")
