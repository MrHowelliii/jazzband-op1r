# Jazz Band companion — development revision 0.5

Open **Jazz Band Companion.html** in Chrome or Edge. This is the standalone bundle;
it needs no account, server, or internet connection for library editing. Web MIDI and
folder access depend on browser support and permission. If those are unavailable when
opening a local file, serve this folder with `python3 -m http.server 8765 --bind 127.0.0.1`
and open `http://127.0.0.1:8765/Jazz%20Band%20Companion.html`.

## Play along: the plugin itself, in any browser (iPhone and iPad too)

The **Play** tab now runs the Jazz Band plugin's own C code, compiled to WebAssembly (`jazzband-web.js`, built from
`jazzband-op1r/web/`), so the browser band is the OP-1 band: all 42 players, swing / ballad / bossa nova, loops
(sections or bars), key cycles, choruses, intros, turnarounds, endings, and the same bar-line rules for key, song and
jump changes. The old preview (the 0.5 band only) is gone. A scripted session renders sample-for-sample the same as
the plugin built natively (`make test-web`).

- **Transport** (red on the OP-1): Play, then Finish (the ending after this chorus or loop pass), then Stop. The
  square button stops at once. Space / Esc on a keyboard.
- **Key** and **tempo** steppers (blue and red): stopped, at once; playing, a key change waits for the next bar line
  (Cancel in the notice). Tap the key for all 12 keys, tap the tempo for a slider, tap tempo, the songbook tempo,
  and *Keep as practice tempo* (saved with the song and sent to the OP-1 with the songbook). Hold − / + to repeat.
- **Song** (SONGS): tap the title. Set lists are chips; stopped, a song loads; playing, it waits for the bar line.
  In a set list, *Up next* queues the next song.
- **Chart**: tap a bar for *Play from here* / *Start here next time* (stopped), *Jump here* (playing, at the next bar
  line), *Loop from / to here*, *Loop section*.
- **Practice / Arrange / Band** (SETUP): loop, from/to bar, key cycle, choruses; feel, intro, turnaround, ending;
  the four players with what each does, the mixer (level and mute per instrument), band volume and mute.
- Settings stay with each song **on this device** (the OP-1 forgets them at restart); players and mixer are global.
- **Sounds**: without packs the plugin's stand-in synths play. *Load sound packs…* (Band tab) takes the same
  `jbpiano`, `jbbass`, `jbguitar`, `jbdrums`, `jbnylon` .op1p files you install on the OP-1; they are kept in the
  browser. Served next to a `sound packs` folder holding them, the page loads them by itself.
- *Play along over MIDI* (Chrome / Edge) sends one part, and optionally clock, to the OP-1 as before.

**On iPhone / iPad:** Safari needs the page from a web address (opening the .html from Files does not run it). Put
`Jazz Band Companion.html` (and optionally the `sound packs` folder) on any static web host, or serve this folder
from a Mac on the same Wi-Fi (`python3 -m http.server 8765`, then open `http://<mac-name>.local:8765/Jazz%20Band%20Companion.html`).
Add it to the Home Screen for a full-screen app. Sound starts on the first tap; iOS 17 and later play with the
ringer switch on silent (earlier iOS: a silent media clip is used for the same), and the screen stays on while the
band plays (iOS 16.4+). Over plain `http://` from another computer Safari has no AudioWorklet, so the band runs on the
page instead (same sound, slightly more latency). Lockdown Mode disables WebAssembly, and with it the band.
Sending songs and installing plugins still needs Chrome or Edge on a computer (Web MIDI).

## Plugin 1.5: 42 players, bossa nova, samples only

SETUP BAND has 12 drummers (new: Tony Williams, Roy Haynes, Billy Higgins, Jo Jones, Jack DeJohnette, Max Roach,
Airto), 10 bassists (Sam Jones, Charlie Haden, Jimmy Garrison, Walter Page, Oscar Pettiford), 10 pianists (Wynton
Kelly, Count Basie, Ahmad Jamal, Horace Silver, Erroll Garner) and 10 guitarists (Grant Green, Herb Ellis, Barney
Kessel, Charlie Christian, João Gilberto). SETUP ARRANGEMENT › FEEL adds BOSSA NOVA. The SOUND rows are gone: every
player plays the sound packs (a stand-in synth only if a pack is missing, shown in red). Install the new `jazzband.op1p`
**and** `sound packs/jbdrums.op1p` (now with toms, shaker and conga, 0.79 MB), plus the new `sound packs/jbnylon.op1p`
(a nylon-string guitar that only João Gilberto plays, 0.15 MB), then open **Make room on your OP-1**: the update needs
about 0.35 MB more plugin memory.

## Plugin 1.4: three screens

On the OP-1, press **blue** for the screen switcher: **PLAY** (chords; blue = key, white = bar cursor and bar
actions, red = tempo), **SONGS** (set lists and songs: browse with white, push white to load or queue a song) and
**SETUP** (practice, arrangement, band and song settings: white picks a row, red changes it). Green is volume and
mute everywhere; a red press is play / finish / stop everywhere. Shift + the knobs is the band mixer.

Players are chosen in SETUP BAND. Settings made on the OP-1 last until it restarts. The companion's Play tab
runs the same plugin code (see above). Full details: `jazzband-op1r/README.md`.

## Add and transfer songs

1. Open **Songs & OP-1**. Drop iReal Pro HTML/text exports or Jazz Band JSON
   backups onto **Drop your songs here**, or choose **Browse files**. To import
   a copied `irealb://` link, expand the paste-link control. Imports merge and
   matching title/chart pairs are skipped. New iReal imports start in your library;
   JSON backups retain their saved songbook selections.
2. Drag library songs into **OP-1 songbook**, or choose **Add**. Drag songs within
   the songbook to reorder them; **Up** and **Down** offer the same control. Drag
   songs back to the library or choose **Remove** to exclude them from the songbook
   while keeping their charts. **Clear set** excludes all songs. Search filters
   only the library. Click a song title to open its preview in **Practice studio**.
   The songbook order is the order in the OP-1's SONGS screen. Review import warnings.
   Limits remain 127 songs, 128 bars per song and 65,536 chart bytes; invalid or
   empty sets cannot be saved. Changes stay local until you explicitly save.
3. Connect the OP-1 with USB in normal mode (not disk mode) and click **Send to OP-1**.
   Chrome asks once for MIDI access (allow SysEx). The set is packed into a small data-only plugin,
   `plugins/jbsongs.op1p` (about 67 KB), and sent with the same 64-byte transfer as plugin updates.
4. Press **T1** on the OP-1 when the companion asks (T2 cancels). The OP-1 installs the songbook and restarts;
   open SONGS in Jazz Band (blue, then white) and the list header reads **USB SONGBOOK**. Each send replaces the last one.

Disk mode still works (**No USB? Use disk mode instead**): save `jb songbook.aif` to the OP-1's `synth` folder and
load it into synth slot 5. Jazz Band's order is: USB songbook, then slot 5, then the 2 built-in songs. While a USB
songbook is installed, slot 5 is not used. jbsongs also appears in the OP-1's FX list; it does nothing to the sound
(its screen says so). Removing it needs a plugin delete, which the companion does not offer yet.

How it is built: `jazzband-op1r/plugins/jbsongs/` is the plugin (`jbsongs.h` is the layout the companion fills).
After changing it, compile it with op1r's `make_plugin.py`, then run
`python3 make_songs_template.py <jbsongs.op1p> companion-core.js` and `python3 build_companion.py`. The template
script checks that no relocation touches the songbook area. `make test-usb` (in `jazzband-op1r`) loads a
companion-built jbsongs.op1p with op1r's own loader code (`mock/op1r-loader`, unchanged) and checks Jazz Band plays it.

The app retains the existing importer: some chord qualities are approximated and D.C./D.S. navigation plays
straight through. 3/4 songs stay in 3/4 (the chart starts with `T34`; a "Jazz Waltz" without a time signature counts
as 3/4); other meters play as 4/4 with a warning. Songs imported before this version were flattened to 4/4: re-import
them, or add `T34` at the start of the chart in the editor. It
does not import legacy `irealbook://` links, PDFs, MusicXML, or iReal Pro audio.
Tests use a synthetic modern iReal export. A real library export still needs checking.

Use **Back up library** to save a portable JSON copy, including export selections,
ordering, and warnings. Browser storage can differ between app URLs, local files,
and browsers; do not rely on browser storage as your only library copy.

## Real instrument sounds

The `sound packs` folder holds five plugins with recorded instruments: jbpiano, jbbass, jbguitar, jbdrums, jbnylon (3.1 MB,
credits in `sound packs/CREDITS.md`). In the companion open **Real instrument sounds**, choose the `.op1p` files,
click **Send sound packs to OP-1** and press **T1** when asked (one restart for all four). The upload shows its speed
and the time left. On the OP-1 every player uses its pack (1.5). Update the Jazz
Band plugin to 0.9 first: older versions ignore the packs.

**Memory.** op1r loads every plugin file at startup into one 4 MB budget (plugload.c TOTAL_CAP); a file that does not
fit is skipped. op1r 73's own 56 synths and effects take about 2.9 MB, so with Jazz Band and the songbook only about
0.95 MB is left: on Nov 22 only the drum pack loaded. **Make room on your OP-1** (in the same panel) reads the plugin list
from the OP-1, shows the budget, and pre-selects the largest synths and effects to remove so all four packs fit (Jazz
Band's own plugins are locked; op1r's sampler and drum engines and the sequencers are not suggested). One T1 removes
them. With every op1r 73 plugin installed it suggests steve, vapora, snowglobe, bubbler, delayed, rackverb, reedie,
moai, granular and clouds (1.8 MB), leaving about 87 KB spare. Removed plugins come back from your op1r build
(`~/op1r-src/synth_sdk/build/plugins`) with op1mac.py. Loaded sizes come from `tests/op1r_mem.json` (op1r 73 built
from source); a plugin not in it is estimated at 1.5x its file size.

## Update Jazz Band code independently

OP1r supports a dynamic plugin file, `jazzband.op1p`, containing the compiled band
and screen. A matching OP1r base with the plugin loader and update protocol 3+
is required. The companion checks the connection before enabling upload.

In **Update the Jazz Band plugin**, find ports, choose the OP-1 input/output,
check the connection, select a compiled `jazzband.op1p`, and upload. The app validates
the plugin name, FX/API version, footer, and CRC, then stages only
`plugins/jazzband.op1p` over USB MIDI SysEx. It does not send firmware, bootloader,
sample slots, or databases. Confirm T1 on the OP-1 to apply/restart; T2 cancels.
An interrupted apply can have an unknown outcome: check the device before retrying.

The upload sends 64-byte pieces, one at a time. From a Mac, the OP-1 drops any USB-MIDI
transfer longer than about 128 bytes, and two messages sent back to back are merged into one;
OP1r's usual 192-byte pieces therefore stall (this is what `op1mac.py probe` measured).
A lost piece or reply is resent up to three times, and when the OP-1 reports that it already
has more data than expected, the upload continues from the OP-1's position.

Changing C code still requires compiling **the plugin alone**, using the matching
OP1r SDK's `python3 make_plugin.py plugins/jazzband`. The entire firmware need not
be rebuilt for each change. No compiled plugin or firmware image is included in
this development package. If your base lacks the plugin loader, update it once
with the [official OP1r project installer](https://pcppcp.github.io/op1r/web/).
Protocol/build references: [op1push.py](https://github.com/pcppcp/op1r-src/blob/main/tools/op1push.py),
[plugin guide](https://github.com/pcppcp/op1r-src/blob/main/synth_sdk/PLUGINS.md).

## Songbook reload fix

The plugin now fingerprints the songbook's encoded length and stored checksum as
well as the sample peaks. Previously, a changed chart with the same encoded length
could retain identical peaks and be missed when slot memory was reused. A regression
test reproduces the failure in the old code and passes with the fix. Install the
updated plugin to get this fix; merely replacing the HTML cannot patch installed code.

## Build and verify

`Jazz Band.html` is the editable source and loads `companion-core.js` and `jazzband-web.js` from this folder.
`jazzband-web.js` is generated (and committed, so the page runs from a checkout): after changing the plugin or
`jazzband-op1r/web/`, run `make web` in `jazzband-op1r` (needs clang with the wasm32 target and wasm-ld). Run
`python3 build_companion.py` after source edits to regenerate the standalone bundle.

```sh
node tests/companion.test.cjs
node tests/workspace.test.cjs
cd jazzband-op1r
make test
make test-songbook
make test-web        # the WebAssembly band against the same session built natively, plus the page's glue
```

`make test-web` builds the native side with clang on purpose: in the bossa piano part one call draws two random
numbers inside its argument list (`piano_hit(..., 5 + irand(e, 4), VLO[st] - 4 + irand(e, 12))`), and C leaves the
order open, so GCC and Clang draw them the other way round. Musically equivalent, but not bit-identical between a
GCC build (the OP-1's toolchain) and the browser.

Tests cover library validation and merging, iReal link parsing, AIFF PCM preservation,
SysEx packet encoding/CRC, plugin validation, staged upload with mocked ports (including a
simulated 128-byte Mac limit, lost requests, lost replies and split replies), error,
cancel, old-protocol and timeout cases, and the real C songbook decoder, including
same-size edits. The existing audio/navigation host suite runs with address and
undefined-behavior sanitizers.

Chrome UI checks covered text import, searching, reordering, selections, capacity,
and export feedback. Browser automation could not select a local file because the
extension's file access is disabled. No physical OP-1 upload, folder write, or firmware
build was performed; MIDI transfer tests use simulated replies.

## Frontend handoff

The workspace now opens on the song library, with separate library and OP-1
songbook panels, file dropping, drag-based inclusion/removal and ordering, button
alternatives, capacity validation, pending-save status, and a separate practice
view. Optional plugin and sample controls remain under expandable sections.

Frontend source is `Jazz Band.html`; regenerate `Jazz Band Companion.html` with
`python3 build_companion.py`. The Play tab talks to the band only through `jazzband-op1r/web/jbw-glue.js` (calls named
after the OP-1's knob actions, and a state snapshot about 60 times a second, shown when its sound is heard). Keep changes to the importer, AIFF encoder, MIDI
protocol and firmware separate from UI work. The rebuild did not change
`companion-core.js` or the C plugin. UI interaction checks are in
`tests/workspace.test.cjs`; they use actual handlers with a minimal DOM and never
write to hardware. Physical folder writes and transfers still require device QA.
