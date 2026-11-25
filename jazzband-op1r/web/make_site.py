#!/usr/bin/env python3
"""Assemble the companion's web site (GitHub Pages) with the sound packs built from source.

    python3 jazzband-op1r/web/make_site.py <built packs dir> <site dir>

Copies the page and its scripts, and each built pack whose SHA-256 is the expected one into "sound packs/", where the
page loads them by itself. Expected: the 1.5 release files (baseline-v1.5.json) that make_packs.py reproduces byte
for byte (jbpiano, jbbass, jbdrums), and for jbguitar the pack the committed recipe makes (the 1.5 release file is
498,937 bytes from an earlier recipe that is not in the repository). A pack that does not match is left out (the page
then plays the stand-in synth for it) and reported, so a changed toolchain never publishes unchecked audio."""
import hashlib, json, os, shutil, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
PAGE = ['index.html', '.nojekyll', 'Jazz Band.html', 'companion-core.js', 'jazzband-web.js', 'vendor/qrcode.js', 'songs/jazz-1460.json']
RECIPE = {'jbguitar.op1p': '56a6e2b0275260a33bf153ba0b2dc089ffe07d04d681554c3dd502a61fde917a'}

packs, site = sys.argv[1], sys.argv[2]
want = {os.path.basename(a['path']): a['sha256'] for a in json.load(open(os.path.join(ROOT, 'baseline-v1.5.json')))['artifacts']
        if a['path'].startswith('sound packs/')}
want.update(RECIPE)
os.makedirs(os.path.join(site, 'sound packs'), exist_ok=True)
for f in PAGE:
    os.makedirs(os.path.dirname(os.path.join(site, f)), exist_ok=True); shutil.copy(os.path.join(ROOT, f), os.path.join(site, f))
shutil.copy(os.path.join(ROOT, 'sound packs', 'CREDITS.md'), os.path.join(site, 'sound packs', 'CREDITS.md'))
published, skipped = [], []
for name in sorted(want):
    p = os.path.join(packs, name)
    if not os.path.exists(p): skipped.append(f'{name}: not built'); continue
    h = hashlib.sha256(open(p, 'rb').read()).hexdigest()
    if h != want[name]: skipped.append(f'{name}: sha256 {h} is not the expected {want[name]}'); continue
    shutil.copy(p, os.path.join(site, 'sound packs', name)); published.append(name)
print('published:', ', '.join(published) or 'no packs')
for s in skipped: print('::warning::left out ' + s)
