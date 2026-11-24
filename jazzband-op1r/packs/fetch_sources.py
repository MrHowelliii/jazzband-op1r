#!/usr/bin/env python3
"""Fetch only the sample files make_packs.py reads, from the pinned source libraries.

    python3 packs/fetch_sources.py <sources dir> [pack ...]

The sfzinstruments libraries are git repositories of several hundred MB to GB; a partial, sparse clone of the pinned
commit downloads just the notes the packs use. jbnylon's FreePats library is a release archive, checked by SHA-256.
With these sources make_packs.py reproduces the 1.5 packs byte for byte (baseline-v1.5.json; make_site.py checks)."""
import hashlib, os, subprocess, sys, tarfile, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_packs as mp

GIT = {   # library: (repository, commit)
    'SalamanderGrandPiano': ('https://github.com/sfzinstruments/SalamanderGrandPiano', '3382bf9496bba2486f5ab0de55a264d1dfc38404'),
    'karoryfer.meatbass': ('https://github.com/sfzinstruments/karoryfer.meatbass', 'ac9e859564bda286ab5ec672d00ff1aa2fef2895'),
    'karoryfer.shinyguitar': ('https://github.com/sfzinstruments/karoryfer.shinyguitar', '57243cca85277dbcc120ce17c6178032f93c80f3'),
    'virtuosity_drums': ('https://github.com/sfzinstruments/virtuosity_drums', '9f04cf9a734527edfbb0a4eee1f674e45bbf71bc'),
}
NYLON_URL = os.environ.get('JB_NYLON_URL', '')       # the FreePats archive (see SOURCES.md); filled in below
NYLON_SHA256 = os.environ.get('JB_NYLON_SHA256', '')
LIB_OF = {'jbpiano': 'SalamanderGrandPiano', 'jbbass': 'karoryfer.meatbass', 'jbguitar': 'karoryfer.shinyguitar',
          'jbdrums': 'virtuosity_drums'}


def needed(src):
    """the files each library must provide: run the zone lists with S() recording paths instead of reading them"""
    files = {}
    mp.SRC = src
    for name, inst, rate, gen, credit in mp.PACKS:
        if name not in LIB_OF: continue
        for z in gen():
            for path, gain in z['src']:
                rel = os.path.relpath(path, src)
                lib, inner = rel.split(os.sep, 1)
                files.setdefault(lib, set()).add(inner.replace(os.sep, '/'))
    return files


def run(*cmd, cwd=None):
    subprocess.run(cmd, cwd=cwd, check=True)


def fetch_git(src, lib, paths):
    url, commit = GIT[lib]
    d = os.path.join(src, lib)
    if all(os.path.exists(os.path.join(d, p)) for p in paths): return
    if not os.path.isdir(os.path.join(d, '.git')):
        os.makedirs(d, exist_ok=True)
        run('git', 'init', '-q', cwd=d)
        run('git', 'remote', 'add', 'origin', url, cwd=d)
        run('git', 'config', 'core.sparseCheckout', 'true', cwd=d)
    with open(os.path.join(d, '.git', 'info', 'sparse-checkout'), 'w') as f:
        f.write(''.join('/' + p + '\n' for p in sorted(paths)))
    run('git', 'fetch', '-q', '--depth', '1', '--filter=blob:none', 'origin', commit, cwd=d)
    run('git', 'checkout', '-q', commit, cwd=d)
    missing = [p for p in paths if not os.path.exists(os.path.join(d, p))]
    if missing: sys.exit(f'{lib}: missing {missing[:3]}')
    print(f'{lib}: {len(paths)} files at {commit[:10]}')


def fetch_nylon(src):
    d = os.path.join(src, 'freepats_spanish_guitar')
    if os.path.isdir(os.path.join(d, 'samples')): return
    if not NYLON_URL: sys.exit('jbnylon: set JB_NYLON_URL / JB_NYLON_SHA256 (the FreePats archive)')
    arc = os.path.join(src, os.path.basename(NYLON_URL))
    if not os.path.exists(arc): urllib.request.urlretrieve(NYLON_URL, arc)
    h = hashlib.sha256(open(arc, 'rb').read()).hexdigest()
    if NYLON_SHA256 and h != NYLON_SHA256: sys.exit(f'jbnylon: archive sha256 {h}, want {NYLON_SHA256}')
    tmp = os.path.join(src, 'freepats_tmp')
    with tarfile.open(arc) as t: t.extractall(tmp, filter='data')
    for root, dirs, files in os.walk(tmp):             # the folder that holds the .flac notes becomes samples/
        if sum(f.endswith('.flac') for f in files) >= 11:
            os.makedirs(d, exist_ok=True); os.rename(root, os.path.join(d, 'samples')); break
    else: sys.exit('jbnylon: no samples folder in the archive')
    print(f'freepats_spanish_guitar: archive sha256 {h}')


if __name__ == '__main__':
    src = os.path.abspath(sys.argv[1]); os.makedirs(src, exist_ok=True)
    only = sys.argv[2:] or [p[0] for p in mp.PACKS]
    files = needed(src)
    for name in only:
        if name == 'jbnylon': fetch_nylon(src)
        else: fetch_git(src, LIB_OF[name], files[LIB_OF[name]])
