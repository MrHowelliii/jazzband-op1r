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
]


def build(name, inst, rate, gen, credit, out):
    from scipy.signal import butter, sosfilt
    zs = list(gen())
    for z in zs:
        x = None
        for path, g in z['src']:
            y = mp.load(path, rate) * g
            x = y if x is None else (np.pad(x, (0, max(0, len(y) - len(x)))) + np.pad(y, (0, max(0, len(x) - len(y)))))
        if z.get('tone'): x = sosfilt(butter(2, z['tone'], fs=rate, output='sos'), x)
        z['x'] = mp.shape(x, rate, z['sec'], z.get('fade', 0.25))
        z['cents'] = 0
        if inst:
            f, conf = mp.f0(z['x'], rate, 25 if inst == 1 else 60, 1500)
            m = 69 + 12 * math.log2(f / 440)
            if z['key'] is None:                       # the lead: the root is what the recording sounds
                z['key'] = int(round(m)); z['cents'] = (m - z['key']) * 100 if conf < 0.3 else 0
            else:
                c = (m - z['key']) * 100
                if conf < 0.3 and abs(c) <= (20 if inst == 1 else 40): z['cents'] = c
            z['f0'], z['conf'] = f, conf
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
        table += struct.pack('<BBBBIhBBII', z['key'], z['vlo'], z['vhi'], z['rr'], step, 0, 0, 2, len(data), len(x16))
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
