# jazzband 1.5: a jazz backing band for the OP-1 (op1r plugin)

A master FX plugin for **op1r**, the community custom firmware for the original OP-1 (built on R246). It adds a
band to everything you play: swing drums, a walking bass, Bill Evans-style rootless piano and Freddie Green-style
guitar. It follows the OP-1's tempo, and its screen shows the current chord, the next chord, the beat, the bar,
the key and the current line of the chart.

## 1.5: 22 more players, bossa nova, samples only

**Players.** SETUP BAND now has one row per instrument: 12 drummers, 10 bassists, 10 pianists, 10 guitarists. Each is
an impression of how that musician accompanies (placement, density, register, voicings), not a transcription.

| | New players |
|---|---|
| Drums | Tony Williams (hat on every beat, fast bursts, tom fills) · Roy Haynes (crisp snare snaps, surprises) · Billy Higgins (buoyant ride, light touch) · Jo Jones (time on the hi-hat, Basie swing) · Jack DeJohnette (washy ride, toms and snare talking) · **Max Roach** (melodic tom phrases, hat on 2 and 4, fills down the kit) · Airto (Brazilian: shaker, conga, surdo on the low tom, cross-stick clave) |
| Bass | Sam Jones (deep walk, roots on 1 and 3) · Charlie Haden (simple roots and fifths, space) · Jimmy Garrison (pedal points, double stops) · Walter Page (even four, up the chord) · Oscar Pettiford (melodic, higher, bebop pickups) |
| Piano | Wynton Kelly (bouncy, bluesy anticipations) · Count Basie (sparse high plinks with the octave) · Ahmad Jamal (space, two-bar vamps, block chords) · Horace Silver (stabs with left-hand punches) · Erroll Garner (left hand strums every beat) |
| Guitar | Grant Green (bluesy stabs, repeated-note riffs) · Herb Ellis (swinging four, slide chords into the next) · Barney Kessel (Charleston chords, a moving top voice) · Charlie Christian (swing-era four, horn riffs) · **João Gilberto** (bossa batida: thumb on 1 and 3, fingers on the clave) |

Every new player has swing and ballad parts; in 3/4 Tony Williams, Roy Haynes, Billy Higgins, Jo Jones and Jack
DeJohnette use the nearest existing waltz feel, the others play their own part in 3.

**BOSSA feel.** SETUP ARRANGEMENT › FEEL is now SWING, BALLAD or BOSSA NOVA (kept per song for the session). In bossa
everyone plays straight eighths: drums on a cross-stick 2-bar clave with the kick on 1, the and of 2, 3, the and of 4
(Roach puts a tom in the clave, Elvin / Motian / Tony / DeJohnette ride the cymbal); bass root on 1, fifth on the and of
2 into 3 (Haden: roots only); piano and guitar comp on the clave in each player's voicing. Airto and Gilberto play
Brazilian parts in every feel, so they can be mixed into a swing band. A songbook line with feel `2` (field 3) starts
in BOSSA.

**Samples only.** The SOUND rows are gone: every player plays the sound packs (piano, bass and guitar fall back to
the slot 6–8 samples). If a pack is missing, a stand-in synth still plays so the band is never silent, and that
player's row shows NO SOUND PACK - A STAND-IN SYNTH PLAYS in red. The FM synth B voices are no longer reachable. A 1.3
preset that chose synth B or synth A now plays its player on samples.

**Nylon guitar for Gilberto.** A fifth pack, `jbnylon` (FreePats Spanish Classical Guitar, CC0: 11 notes, a major
third apart from C2 to E5, 22 kHz, 0.15 MB), plays only when João Gilberto is the guitarist; every other guitarist keeps
jbguitar. Without jbnylon, Gilberto plays jbguitar and his SETUP BAND row says ADD JBNYLON FOR NYLON. Level-matched to
jbguitar (test-packs). The pack template now names it NYLON on its own FX screen (the other packs are unchanged).

**Drum pack.** `jbdrums` now also holds a high tom, a low tom (2 dynamics each), a shaker (soft up / accented down
strokes, 2 takes) and an open conga (2 dynamics, 2 takes): 28 zones, 0.79 MB. **Install the new `jbdrums` with the
plugin.** With the old pack the new pieces play their stand-in synths; everything else plays the samples as before.

Memory: Jazz Band 0.22 MB + jbsongs 0.07 MB + packs 3.1 MB (with jbnylon) ≈ 3.4 of op1r's 4 MB.

Demos (`make test-packs`, with the packs): `out/packs_roach.wav` (Roach, Pettiford, Silver, Kessel),
`packs_tony.wav`, `packs_basie.wav` (Jo Jones, Page, Basie, Freddie Green), `packs_kelly.wav`, `packs_haynes.wav`,
`packs_dejohnette.wav` (ballad), `packs_bossa.wav` (Airto, Haden, Jamal, Gilberto on jbnylon), `packs_bossa_cobb.wav` (the
original band in bossa), `packs_airto_swing.wav`. `make test-styles` writes `out/styles_<1-12>[_ballad|_bossa].wav`
on the synths.

## 1.4: controls

Three screens, **PLAY**, **SONGS** and **SETUP**. A **blue press** opens the screen switcher (it shows the three
names, the current one marked NOW): turn white to choose, push white to open it, or push blue again to cancel. The
switcher stays until you choose or cancel. Opening it never interrupts the band; green (volume / mute) and red
(transport) keep working there and on every screen.

**PLAY**

| Knob | Turn | Press |
|---|---|---|
| Blue | The key, in semitones. Stopped: at once. Playing: queued for the next bar line (the screen shows KEY: C TO D AT NEXT BAR); turning back to the key that plays cancels it. A manual key turns a loop's key cycle off (KEY CYCLE OFF) | The screen switcher |
| Green | Band volume | Band mute / unmute (the live input passes through) |
| White | A bar cursor (blue box on the chart). It only selects | Bar actions for that bar: **START HERE** (stopped: count-in or intro into it), **JUMP NEXT BAR** (playing: at the next bar line, after you confirm), **CANCEL**. White turns choose, white press confirms |
| Red | **Tempo**, 1 BPM per click (40-320), at once: no stop, no restart, the form goes on | **Transport**: stopped = play; playing = finish (the musical ending after this chorus or loop pass); again = stop now. The legend shows what the next press does |

The top line shows the actual key (offset beside it in blue), the status line the loop, key cycle and chorus, and the
line under the chord anything queued for the next bar (song, jump, key). A tempo turned this session shows ORIG with
the songbook's tempo beside NEXT.

One bar line, one order: a queued **song** goes first and brings its own key and settings (a queued jump or key of
the old song is dropped); otherwise the form moves on (a confirmed jump is part of it), then a queued key applies.
Confirming a jump drops a queued song; queueing a song drops a queued jump. Stopping applies a queued key.

**SONGS**: white turns browse the set lists and songs (browsing never loads or changes anything); white press opens a
set or selects a song. Stopped: the song loads, ready to play (red). Playing: it is queued for the next bar line, and
a **CANCEL QUEUE** row appears at the top (white press there cancels). The list opens on the set in use (playing: on
its next song). Set order and BACK TO SET LISTS are as before.

**SETUP**: white turns pick a row, **red turns change its value** directly, white press opens a category or does an
action (BACK, RESET). It stays open until you leave it (no timeout).

| Category | Rows |
|---|---|
| PRACTICE | LOOP (off, each section, bars), LOOP FROM, LOOP TO, KEY CYCLE (off, up 1/2, down 1/2, up a 4th per pass), CHORUSES |
| ARRANGEMENT | FEEL (swing / ballad / bossa nova), INTRO, TURNAROUND, ENDING |
| BAND | the player for drums, bass, piano and guitar (1.5: no sound rows; samples, with a stand-in synth shown in red when a pack is missing) |
| SONG | RESET KEY and RESET TEMPO, each showing the original and the current value |

**Shift** + blue / green / white / red = drums / bass / piano / guitar level (the mixer). This held-Shift mapping is
the one exception to the screen controls. The 1.3 Shift layer 2 (player and sound in one knob) is gone.

Session only: op1r plugins cannot write files. Key, tempo, practice and arrangement settings are kept **per song** for
this session (a song gets its own back when it returns; another song does not inherit them) and are lost at restart.
BAND settings apply to all songs, also until restart. To keep a tempo, set the practice tempo in the companion. The
songbook format is unchanged; its ballad flag sets each song's starting feel.

Migration from 1.3 presets: red used to be the feel knob and is now relative tempo clicks, so a saved red position
never changes the tempo (feel starts from the songbook). Blue is now a plain knob read relative to where it sits when a
song loads, so a saved position never moves the key; in exchange a song's key does not survive a restart. Saved 1.3
player/sound words are read once at start, so a saved lineup keeps playing; later changes are made in SETUP BAND.
Why blue is a plain knob: op1r reports a press on a stepped knob through only two push bits, used by white and red.
At the ends of its travel blue keeps turning only one way; every key is still reachable from any position.

## 1.3: Blakey, Mingus, Monk, and Django

Each instrument now has a fifth player (1.4: chosen in SETUP BAND):

| Instrument | Player 5 | Accompaniment |
|---|---|---|
| Drums | Art Blakey | Firm ride, strong snare responses, rising press-roll approximations into sections |
| Bass | Charles Mingus | Two-bar blues riffs, root pedals, forceful accents, chromatic pickups |
| Piano | Thelonious Monk | Open shell voicings with tight dissonances, displaced short stabs, rests |
| Guitar | Django Reinhardt | Short la pompe rhythm chords, backbeat accents, occasional connecting notes |

All four have swing, ballad, and 3/4 versions and work with existing sound packs or synths.
These are accompaniment impressions using the existing instruments, not transcriptions or new recordings.
In 1.3 these were knob steps on Shift layer 2 (player and sound in one value); 1.4 has separate PLAYER and SOUND rows
in SETUP BAND.

Install the updated `../jazzband.op1p` through the companion's plugin update control.
After updating, reselect any saved player/sound choices: the normalized knob positions now
cover five players instead of four, so older positions can select a different player or sound.
The original four players retain their order and accompaniment patterns.

`make test-all` checks the roster, every player/sound knob step and reload, note ranges,
event budgets, sound packs, navigation and audio clipping under ASan/UBSan. Quartet demos:
`out/styles_5.wav`, `out/styles_5_ballad.wav`, `out/waltz_5.wav`, `out/waltz_5_ballad.wav`,
and `out/packs_quartet*.wav`. `out/quartet-preview.png` shows the player screen.

## 1.2: loops, endings, intros, live tempo (now in SETUP and on red)

The 1.2 settings page became SETUP PRACTICE and SETUP ARRANGEMENT in 1.4; the musical behaviour is unchanged:

| Row | Values |
|---|---|
| LOOP | OFF, each section of the chart (from its `*A` marks: A 1-8, B 9-16, ...), or BARS |
| LOOP FROM / LOOP TO | the bar range for BARS (changing one switches the loop to BARS) |
| KEY CYCLE | OFF, up a half step, down a half step, up a 4th: after each pass of the loop, through all 12 keys |
| CHORUSES | ENDLESS or 1-16: after the last one the band plays the ending |
| INTRO | COUNT-IN (the 1-bar click), LAST 4 BARS (of the form, or of the loop; with a turnaround set, the 4th bar leads in with a ii-V), I-VI-II-V (two bars, twice, into the first chord; minor: i-vi7b5-ii7b5-V7b9). An intro replaces the count-in: the band starts on it at once |
| TURNAROUND | OFF, II-V, III-VI-II-V: replaces the last bar (or two) of each chorus with a turnaround to the top; skipped when the chart already has one, and in the last chorus |
| ENDING | FINAL CHORD, or TAG + FINAL CHORD (the ii-V to the home key three times first) |

A loop starts on its first bar after the count-in; switched on while playing, the band jumps into it at the next bar
line. The final chord: everyone on the downbeat (crash and kick, bass root, piano voicing with its root, guitar), held
for two bars, then the band stops. The home chord is the song's key (major or minor 6); a blues that starts on a
dominant chord in its key keeps it.

Tempo: the songbook can carry a practice tempo and the chart's original (`...;chart;120;100`: plays 120, shows ORIG
100). Set it in the companion (song editor, Practice tempo). Turned on the OP-1 (red, on PLAY), a tempo stays with that
song until the OP-1 restarts; op1r plugins cannot write files, so to keep it, set it in the companion and send the
songbook again.

Chord symbols: diminished is drawn as a raised circle (B°7), half-diminished as a slashed circle (Bø7).

## 0.9: sound packs (real instruments)

Four data-only plugins hold recorded instruments: `jbpiano` (Salamander Grand Piano, CC-BY 3.0), `jbbass` (Karoryfer
Meatbass, CC0), `jbguitar` (Karoryfer Shinyguitar, pickup and microphone blended, CC0) and `jbdrums` (Versilian
Virtuosity Drums, overhead, room and close mics mixed, CC0). The companion sends them over USB in one session (one T1).

| Pack | Samples | Audio | Rate | Memory |
|---|---|---|---|---|---|
| jbpiano | 18 notes (every minor third, A1–C6) x 2 dynamics | 72 s | 32 kHz | 1.13 MB |
| jbbass | 11 notes (Eb1–C4) x 2 dynamics, 2 takes for the softer | 46 s | 22 kHz | 0.50 MB |
| jbguitar | 13 notes (E2–Eb5) x 2 dynamics | 29 s | 32 kHz | 0.45 MB |
| jbnylon (1.5) | 11 notes (C2–E5, every major third), 1 dynamic; Gilberto only | 13 s | 22 kHz | 0.15 MB |
| jbdrums | ride (2 dynamics x 2 takes), pedal hi-hat, snare (4), cross-stick (2), kick (2 x 2), crash; 1.5: high and low tom (2 each), shaker (2 x 2), conga (2 x 2 + 1) | 24 s | 44.1 kHz | 0.79 MB |

Audio is IMA ADPCM (4 bits), or 8-bit mu-law for the cymbals and shaker (ADPCM was too gritty on them). Each note plays from the
nearest recorded one with its dynamic layer, round-robin over takes. Levels match the synths (test/packs_test.c).
At boot Jazz Band finds the packs through op1r's FX table, checks their checksums (4 KB a block, about 1 s), then
plays them in place. 1.5: every player uses its pack; packs come before the slot 6–8 samples; without either a stand-in synth plays.

Memory: op1r gives all plugins together 4 MB (plugload.c TOTAL_CAP). Jazz Band (0.22 MB), jbsongs (0.07 MB) and the four
packs (2.96 MB) use about 3.25 MB. If other plugins push the total over 4 MB, a pack does not load and that instrument
plays its synth.

Build the packs: `packs/make_packs.py <sfzinstruments checkouts> <jbpack template .op1p> packs/out` (needs numpy,
scipy, soundfile, and `cc -O2 -shared -fPIC packs/adpcm.c -o packs/adpcm.so`); the template is `plugins/jbpack`
compiled with op1r's make_plugin.py. Sources and licenses: packs/SOURCES.md. `make test-packs` loads the packs with
op1r's own loader, checks the decoder, pitch and levels, and writes `out/packs_*.wav`.
Also fixed: the output soft clipper could overflow 32 bits when the band was very loud (above about 1.3x full scale).

## 0.8: 3/4

A chart that starts with `T34` (iReal Pro's time signature token) plays in 3: a 3-beat bar, a 3-click count-in, three
beat dots and "3/4" on the play screen. In a 3/4 bar, 2 chords get 2 + 1 beats, 3 chords one beat each. Every player
has a waltz version: the drums ride 1, 2, the 2-let and 3 with the hi-hat on 2 and 3 (Philly Joe adds a rim on 3, Elvin a
two-against-three figure, Motian stays broken); the walking bass players walk in 3 (ballad: 1 held, then 3); piano
and guitar comp from 3/4 patterns, anticipating on the and of 3. Charts without `T34` are 4/4 as before, and older
plugins ignore the token. `make test-styles` checks every player in 3/4 and writes `out/waltz_*.wav`.

## 0.7: songs over USB

The companion's **Send to OP-1** installs your set as `plugins/jbsongs.op1p`, a data-only FX plugin (T1, restart).
At boot Jazz Band looks through op1r's FX table (`op1_fxs`, `op1_fx_count`) for it: a signature in its four knob-press
bytes, then a magic number, then an FNV-1a checksum over the charts (checked 2 KB per audio block). Songs come from
jbsongs, else the slot 5 songbook, else the 2 built-in songs; the browser header says which (USB / SLOT 5 / BUILT-IN).
A damaged jbsongs is ignored. Built and checked with op1r 73's SDK: `make_plugin.py`, `pluginkit.py check`.

## 0.6: song browser and players

**Song browser.** (1.4: the SONGS screen, see the controls above.) Five titles around the cursor, the number out
of the total and the highlighted song's key; the playing song is green.

**Players.** (1.4: SETUP BAND.) A player for each instrument. Each is an impression of how that musician
accompanies (rhythm placement, density, register, voicings), not a transcription. Player 1 is the original band.

| | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| Drums | Jimmy Cobb: steady ride, light comping | Philly Joe Jones: driving, snare chatter, rim on 4, triplet fills | Elvin Jones: triplet waves, 3 against 4 | Paul Motian: broken time, sparse, cymbal washes | Art Blakey: accents and press-roll builds |
| Bass | Paul Chambers: classic walking | Ray Brown: roots and fifths, chromatic from below, triplet pickups | Ron Carter: chromatic runs, wider range, inversions | Scott LaFaro: triplets, space, high register | Charles Mingus: blues riffs and pedals |
| Piano | Bill Evans: rootless A/B voicings | Red Garland: short stabs on the and of 2 and 4 | McCoy Tyner: fourths over left-hand root and fifth | Herbie Hancock: sparse, high, odd placements | Thelonious Monk: angular, displaced stabs |
| Guitar | Freddie Green: four to the bar | Jim Hall: light, high 3-7-9 voicings | Wes Montgomery: punchy offbeats, octave fills every 4th bar | Kenny Burrell: Charleston, half-step slides | Django Reinhardt: accented la pompe |

SETUP BAND shows each player and what it does. (1.5: players 6–12 are in the 1.5 section above; the SOUND rows are gone.)

The companion's Play tab runs this plugin compiled to WebAssembly (`web/`, `make web`), so it plays every player.

Tests: `make test-all` (plugin, players, songbook and browser, companion). `make test-styles` writes demo WAVs:
`out/styles_1.wav` … `styles_5.wav` (all four instruments on player 1 … 5, swing, 160 BPM) and `*_ballad.wav` (72 BPM).
Previews: `make preview` (out/navigation-preview.png) and out/browser-preview.png from `make test-songbook`.

## Checks

```sh
make test test-form test-setlists test-styles test-songbook test-usb test-packs   # run separately (180 s shell limit)
```

`test-form` drives the 1.4 controls: the switcher (cancel, no timeout, red and green inside it), SETUP rows and red
values, loops with a key cycle, queued and cancelled key changes, red tempo without transport changes, the bar cursor
(no automatic jump), confirmed jumps and CANCEL, song / jump / key precedence at one bar line, CANCEL QUEUE, loading
without starting, per-song settings, red presses in the count-in, intro, playing and finishing, START HERE, mute on
every screen, the 1.5 player rosters and the three feels. Screens: `out/form-preview.png`. These use the host mock of the op1r API;
they do not establish behaviour on the OP-1 itself.

## Sounds: samples, with a stand-in synth (1.5)

- **Sound packs** (jbpiano, jbbass, jbguitar, jbdrums) first.
- Else, for piano, bass and guitar: the OP-1's synth slot 6 (piano), 7 (bass) or 8 (guitar), loaded with a multisample
  made by the Jazz Band Mac app (up to 16 notes per instrument in one 6-second slot).
- Else a stand-in synth (additive piano, additive upright bass, Karplus-Strong guitar, synth drums), so the band is
  never silent; SETUP BAND shows it in red. There is no longer a choice of sound.

## Songs over USB: the songbook

The Mac app (Jazz Band.html, "OP-1 files") imports iReal Pro links, and writes the ticked songs into
`jb songbook.aif`: the charts stored as sample data. Copy it to the OP-1 in disk mode and load it into synth slot 5.
Up to 127 songs (about 66 KB of charts). No firmware or plugin rebuild for new songs.
Repeats, first / second endings and `x` / `r` bars are written out; D.C. / D.S. are played straight through; 3/4 songs
play as 4/4 (marked in the app); chords map to the nearest of maj7, m7, 7, m7b5, dim7, m6, 6, 7b9, m(maj7).

Tempo is the OP-1's own BPM (or MIDI clock), so the metronome, the sequencers and the band stay together.

## Status

- Written against the op1r plugin API (`synth_sdk/op1_plugin.h`, `PLUGINS.md`). Integer-only (no FPU on the
  OP-1), no libc, no division in the per-sample loops. About 24 KB of code and 54 KB of RAM.
- Tested on a computer with a simulated OP-1 beat clock and knobs (`test/host_test.c`, address and undefined-behaviour
  sanitizers): bar timing at 60 / 140 / 300 BPM, play/stop, key, song, feel, no clipping, input passes through,
  mono slots. Screen previews: `screens.png`.
- **1.4 has not been run on an OP-1.** It is built with the matching toolchain (the same compiler reproduces 1.3 byte
  for byte) and passes op1r's `pluginkit.py check` and the companion's loader/CRC checks. On hardware, check: blue's
  press opens the switcher and its turn steps the key (plain knob), red's press is transport and its turn the tempo,
  white's press works on every screen, Shift + knobs still mix, and CPU in op1r's resource monitor (clock screen,
  Shift + help, then T1) with the densest line-up.

## Install

You need: an original OP-1, a Mac with Chrome, and Docker Desktop to compile the plugin.

1. **Put op1r on the OP-1.** Open the op1r web patcher in Chrome (https://pcppcp.github.io/op1r/web/), patch the
   stock R246 firmware, then flash it: hold COM while switching on, connect USB, press 1, drag the file onto the disk, eject.
2. **Compile the plugin.**
   ```sh
   git clone https://github.com/pcppcp/op1r-src.git && cd op1r-src
   docker build -t bfin-elf-toolchain toolchain                 # once
   cp -r /path/to/jazzband/plugins/jazzband synth_sdk/plugins/
   cd synth_sdk
   python3 pluginkit.py check jazzband                          # op1r's own host test and previews
   python3 make_plugin.py plugins/jazzband                      # -> build/plugins/jazzband.op1p
   ```
3. **Copy it to the OP-1** with the web patcher's plugin install, or with
   `python3 ../tools/op1push.py <tree> --port <OP-1 MIDI port> --files plugins/jazzband.op1p`. Restart the OP-1.
4. **Play.** Mixer → master FX → **jazzband**. Press red. Press green to mute/unmute the band while keeping your live
   instrument audible. Press blue for the screens (PLAY, SONGS, SETUP).

If anything goes wrong, op1r has a safe mode: hold **help** while switching on, and no plugins load. Then remove
the plugin over USB. A plugin that locks the OP-1 at boot is skipped on the next boot automatically.

Rather not build it yourself? op1r's maintainers build and hardware-test plugins submitted as pull requests to
op1r-src (`synth_sdk/plugins/jazzband/`, with the WAV and screen preview).

## Files

- `plugins/jazzband/`: the plugin (`jazzband.c`, `jb_tables.h`, `jazzband.svg`, `plugin.json`)
- `gen_tables.py`: regenerates `jb_tables.h` (all pitch and decay constants, precomputed)
- `test/host_test.c`, `mock/`: host test harness (`gcc -Imock -Iplugins/jazzband test/host_test.c -lm`)
- `render_frames.py`: draws the logged screen frames to PNG
- `web/`: the browser build for the companion: `op1_plugin.h` (a freestanding stand-in for the SDK header),
  `jb_wasm.c` (includes `jazzband.c` unchanged; the songbook and packs arrive through `op1_fxs[]` as on the OP-1, and
  touch controls call the knob handlers' own functions), `jbw-glue.js` (AudioWorklet / fallback glue), `bundle.py`.
  `make web` writes `../jazzband-web.js`; `make test-web` checks it against the same session built natively

License: GPL-3.0-or-later with the OP-1 firmware permission, like op1r. Songs are chord changes only.

## Companion library and plugin-only updates

See [the companion instructions](../README.md) and open `../Jazz Band Companion.html`.
This revision also fixes missed same-size songbook edits when the sampler slot buffer is reused.
Run `make test-songbook` with Node.js available to verify the HTML-to-C songbook path.
