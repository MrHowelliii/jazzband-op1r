# Jazz Band in the browser (WebAssembly)

The band of the OP-1 plugin, unchanged, compiled for the web, plus a lead line. The jazzbook play-along site
(github.com/MrHowelliii/jazzbook) uses it to play lead sheets: the band plays the chords, the lead plays the melody,
and you can mute either side (or one instrument) to practise.

| File | What it is |
|---|---|
| `jb_web.c` | The wrapper: `#include`s `plugins/jazzband/jazzband.c` and exports `jbw_*` functions (load bars of per-beat chords, play/stop/finish, tempo, feel, key, players, levels, choruses, intro, ending, bar-range loops), and a lead voice |
| `op1_plugin.h` | `mock/op1_plugin.h` without libc (wasm32, freestanding) |
| `build.sh` | `clang --target=wasm32 -nostdlib ... -> jazzband.wasm` (no Emscripten; about 115 KB, no imports) |
| `pack2web.c` | A sound pack `.op1p` -> `.jbp`: the image op1r's own loader puts in memory (header, zone table, audio). wasm32 and the Blackfin share the layout, so the engine reads it in place |
| `test/wasm.test.mjs` | Node: no imports, all packs verify, bars last exactly 2 s at 120 BPM, the lead plays A4 at 440 Hz; writes `out/wasm_demo.wav` |
| `sync-jazzbook.sh` | Builds everything and copies `jazzband.wasm` and the packs into a jazzbook checkout |

The only change to the plugin source: `jazzband.c` calls `JB_TICK_HOOK(e, bpt)` on every band tick when it is defined
(it never is on the OP-1) and lets `JB_MAXBARS` be set from outside (the web build uses 512: unrolled forms with
repeats and D.S. can pass 128 bars). `make test`, `test-styles`, `test-form` and `test-setlists` pass unchanged.

## The lead

The page sends the melody as notes `(bar, t, d, pitch, velocity)` with `t`, `d` in 48ths of a quarter note, already
swung for the feel. On each band tick (12 a beat) the hook starts the notes that fall inside it, delayed by the
fraction of a tick, so the lead stays sample-locked to the band, also when the tempo changes. Sounds: a horn-like
synth (band-limited saw, resonant low-pass, delayed vibrato, legato), the piano or guitar pack, or FM vibes. A bitmask
says which choruses have the melody (head in and out; the choruses between are for solos). The pickup plays over the
end of the count-in bar.

## Build

```sh
cc -O2 -shared -fPIC packs/adpcm.c -o packs/adpcm.so
python3 packs/make_packs.py <sfz sources> plugins/jbpack/jbpack-template.op1p packs/out jbpiano jbbass jbguitar jbdrums
sh web/build.sh
cc -O2 -Imock web/pack2web.c -o out/pack2web
for p in jbpiano jbbass jbguitar jbdrums; do out/pack2web packs/out/$p.op1p web/packs/$p.jbp; done
node web/test/wasm.test.mjs
```

`<sfz sources>`: shallow, sparse clones of github.com/sfzinstruments/{SalamanderGrandPiano, karoryfer.meatbass,
karoryfer.shinyguitar, virtuosity_drums} (see `packs/SOURCES.md`). jbnylon (FreePats) is optional: without it
João Gilberto plays the regular guitar pack.

## Sound for the web (not the OP-1)

`JB_WEB` (set by `jb_web.c`) turns on three things the OP-1 build does not have:

* **16-bit PCM packs** (`web/make_web_packs.py`, about 27 MB in all instead of 3 MB): no ADPCM coding noise, three
  piano dynamics, longer sustains, a 32 kHz bass, uncompressed cymbals. Written straight to `.jbp`; `pack2web` is
  only needed for OP-1 packs.
* **A recorded lead**: pack slot 5 (`jblead`, alto saxophone). `jbw_lead_sound(6, level, octave)` plays the melody
  from it, one note letting go as the next starts.
* **A player's touch** (`JB_TRIGGER_HOOK`, `jbw_human(0..100)`, default 60): each instrument sits in its own place
  around the beat (ride and bass on top, piano and guitar a few milliseconds behind), placement and loudness move a
  little from note to note, a chord's notes do not land as one. Bar lines do not move; `jbw_human(0)` is the exact grid.

Credits: `web/PACK-CREDITS.md`.
