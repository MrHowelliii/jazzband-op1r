# Jazz Band 1.5 — User Guide

Jazz Band is a jazz rhythm section that lives inside your OP-1 (original OP-1 running the op1r custom firmware).
It plays drums, bass, piano and guitar from a chord chart while you play along, and its screen shows the chord now,
the chord coming next, and where you are in the form.

It comes in two parts:

- **The plugin** (`jazzband.op1p`) runs on the OP-1 as the master effect. It plays the band and has three screens:
  PLAY, SONGS and SETUP.
- **The companion** (`Jazz Band Companion.html`) runs in Chrome or Edge on your computer. It imports iReal Pro charts,
  builds your songbook and set lists, and installs the plugin and its sound packs over USB.

> **Status:** 1.5 is a testing release. Everything in this guide is checked in host tests on a computer; behavior on
> the OP-1 itself is still being confirmed. Please report anything that differs.

---

## Contents

1. [What's new](#whats-new)
2. [Setting up](#setting-up)
3. [Opening Jazz Band on the OP-1](#opening-jazz-band-on-the-op-1)
4. [The knobs at a glance](#the-knobs-at-a-glance)
5. [Switching screens](#switching-screens)
6. [PLAY](#play)
7. [SONGS](#songs)
8. [SETUP](#setup)
9. [The band: feels and players](#the-band-feels-and-players)
10. [Sounds](#sounds)
11. [Mixing the band](#mixing-the-band)
12. [Tempo and the OP-1's clock](#tempo-and-the-op-1s-clock)
13. [What's remembered](#whats-remembered)
14. [Coming from an earlier version](#coming-from-an-earlier-version)
15. [Troubleshooting](#troubleshooting)
16. [Credits](#credits)

---

## What's new

The last day of work brought Jazz Band from 1.1 to 1.5.

**1.5 — 42 players, bossa nova, samples only**
- 22 new players: 12 drummers, 10 bassists, 10 pianists and 10 guitarists in all, including Max Roach, Tony Williams,
  Count Basie, Ahmad Jamal, Erroll Garner, Charlie Christian and João Gilberto. ([Full list](#the-players))
- A **BOSSA NOVA** feel beside SWING and BALLAD.
- **Airto** (Brazilian percussion) and **João Gilberto** (bossa guitar) play their Brazilian parts in any feel.
- The drum pack adds **high tom, low tom, shaker and conga**.
- A new **nylon-string guitar** pack (`jbnylon`), played only by João Gilberto.
- **Samples only:** the SAMPLES / SYNTH A / SYNTH B choices are gone. Every player uses the recorded sounds. A synth
  stands in only if a sound pack is missing, and SETUP shows that in red.

**1.4 — three screens**
- Three screens, **PLAY**, **SONGS** and **SETUP**, chosen with a blue press.
- **Red** turns the tempo live and its press is play / finish / stop on every screen.
- **White** on PLAY moves a bar cursor; its press opens bar actions (start here, jump next bar).
- Key changes while playing wait for the next bar line.
- Shift + the knobs is now only the band mixer.

**1.2 — practice and arrangement tools**
- Loop a section or any range of bars, with an optional key cycle through all 12 keys.
- Chorus counts and proper endings (final chord, or tag + final chord).
- Intros (last 4 bars, or I-VI-II-V) in place of the count-in.
- ii–V and iii–VI–ii–V turnarounds at the end of each chorus.
- Live tempo, with the songbook's original tempo shown.
- Diminished (°) and half-diminished (ø) chord symbols.

---

## Setting up

### What you need

- An original OP-1 running **op1r** with its plugin loader (install it once with the
  [op1r installer](https://pcppcp.github.io/op1r/web/) if you haven't).
- A USB cable, and Chrome or Edge on your computer.
- The 1.5 files: `jazzband.op1p`, and in `sound packs/`: `jbpiano`, `jbbass`, `jbguitar`, `jbdrums` and `jbnylon`
  (`.op1p` each). They're in the `v1.5-testing` release.

### 1. Make room

op1r loads every plugin into one shared **4 MB** of memory when it starts, and skips any that don't fit. Jazz Band
with all five sound packs needs about 3.4 MB, so most of op1r's own large effects and synths have to go.

1. Open the companion, go to the **OP-1** tab, and connect the OP-1 by USB (normal mode, not disk mode).
2. Under **Plugins on your OP-1**, choose **Make room on your OP-1**. It reads what's installed, shows the memory
   budget, and pre-selects the largest synths and effects to remove. Jazz Band's own plugins are never suggested.
3. Confirm, and press **T1** on the OP-1. It removes them and restarts.

Removed plugins can be reinstalled later from your op1r build.

> **Updating from 1.4?** 1.5 needs about 0.35 MB more than 1.4. Install the new files first (step 2), then run
> **Make room** again: it may suggest removing one or two more plugins.

### 2. Install the plugin and sound packs

1. In the **OP-1** tab, under **Install on OP-1**, choose `jazzband.op1p` and the five sound pack files. You can
   pick them all at once.
2. Send them and press **T1** on the OP-1 when asked (T2 cancels). One restart installs everything.

The companion checks each file before sending it. It only sends plugins, never firmware or your sample slots.

### 3. Add your songs

1. In the **Songs** tab, drop iReal Pro exports (HTML or text) or Jazz Band backups (JSON) onto **Add songs**, or
   paste an `irealb://` link.
2. Drag songs from your **Library** into the **Songbook** (or use **Add**). The songbook's order is the order on the
   OP-1. Up to 127 songs.
3. Optional: make **Set lists** for a gig. Adding a song to a set also adds it to the songbook. An imported iReal Pro
   playlist becomes a set list.
4. Choose **Send to OP-1** and press **T1** when asked. The OP-1 restarts with your songbook; Jazz Band's SONGS
   screen then says **USB SONGBOOK**.

To set a song's starting feel (swing or ballad) or a practice tempo, open the song in the companion's chart editor.
Bossa nova is chosen on the OP-1 (see [SETUP › ARRANGEMENT](#arrangement)).

No USB? Under **No USB? Use disk mode**, save the songbook to the OP-1's `synth` folder in disk mode and load
**jb songbook** into synth slot 5.

Use **Back up library** now and then: browser storage is not a safe only copy.

---

## Opening Jazz Band on the OP-1

Jazz Band is a **master effect**. On the OP-1, open the **Mixer**, go to the master FX, and choose **JAZZBAND**.
Everything you play goes through it, with the band added.

If choosing JAZZBAND does nothing, restart the OP-1.

---

## The knobs at a glance

These work the same on every screen unless a screen says otherwise. The bottom of each screen shows what each knob's
turn and press does right now.

| Knob | Turn | Press |
|---|---|---|
| **Blue** | PLAY: the key | The screen switcher |
| **Green** | Band volume | Mute / unmute the band (what you play still comes through) |
| **White** | PLAY: bar cursor · SONGS: browse · SETUP: choose a row | Do it (bar menu, choose a song, open / back / reset) |
| **Red** | PLAY: tempo · SETUP: change the value | **Play** when stopped · **Finish** while playing (play the ending) · **Stop** (press again) |
| **Shift + any knob** | Band mixer: drums / bass / piano / guitar level | — |

---

## Switching screens

Press **blue**. The switcher lists **PLAY**, **SONGS** and **SETUP**, with the current one marked **NOW**.

- Turn **white** to choose, press **white** to open it.
- Press **blue** again to cancel.

The switcher stays open until you choose or cancel, and the band never stops for it. Green and red keep working there
too.

---

## PLAY

PLAY is where you play along.

**What you see**
- **Top:** the song's number and title on the left; the **key** on the right (big), with how far you've moved it in
  blue.
- **Status line:** STOPPED, COUNT IN, BAR n, the loop, the chorus, the key cycle, or FINISHING / LAST TIME.
- **The chord now** (large) and **NEXT** chord. If you've changed the tempo this session, **ORIG n BPM** shows the
  songbook's tempo beside NEXT.
- **The chart**, with the current bar highlighted and section letters (A, B…) marked.
- **Queued for the next bar** (under the chord): a song, a jump, or a key change waiting for the bar line.

**Blue: the key**
- Turn blue to transpose, a semitone per click.
- Stopped: the key changes at once.
- Playing: the change waits for the next bar line, and the screen shows it (for example **KEY: C TO D  AT NEXT BAR**).
  Turn back to the key that's playing to cancel.
- Changing the key yourself turns off a loop's key cycle (**KEY CYCLE OFF**).

**White: the bar cursor and bar actions**
- Turn white to move a blue box over the chart. It only selects; nothing happens until you press.
- Press white for the bar menu:
  - **START HERE** (stopped): red will start the band from that bar, with the count-in or intro leading into it.
  - **JUMP NEXT BAR** (playing): the band jumps there at the next bar line.
  - **CANCEL**: closes the menu and clears the cursor.
  - Turn white to choose, press white to do it. Jumps aren't possible while the band is finishing.

**Red: tempo and transport**
- Turn red to change the tempo, 1 BPM per click (40–320). It changes at once; the form carries on.
- Press red: **Play** (stopped) → **Finish** (playing: plays the ending after this chorus or loop pass) → **Stop**
  (press again, or during the count-in or intro).

**What happens at a bar line.** When several things are queued, a new **song** goes first and brings its own key and
settings (a queued jump or key for the old song is dropped). Otherwise the band follows the form, including a jump
you've confirmed, then applies a queued key. Stopping applies a queued key at once.

---

## SONGS

SONGS is where you choose what to play.

- Turn **white** to browse. Browsing never changes anything.
- Press **white** on:
  - **a set list:** opens it. **BACK TO SET LISTS** returns.
  - **a song, while stopped:** loads it, ready to play. Press red to start.
  - **a song, while playing:** queues it for the next bar line (**NEXT** appears beside it). A **CANCEL QUEUE** row
    appears at the top; press white there to cancel.
- The playing song is green. The list opens on the set list you're using; while playing, on its next song.
- The header shows where the songs come from: **USB SONGBOOK** (sent by the companion), **SLOT 5 SONGBOOK** (disk
  mode), or **BUILT-IN SONGS** (two demo songs, when there's no songbook).

Nothing moves to the next song on its own; you choose each one.

---

## SETUP

SETUP holds the settings, in four categories: **PRACTICE**, **ARRANGEMENT**, **BAND** and **SONG**.

- Turn **white** to choose a category or row; press **white** to open it.
- Turn **red** to change the selected row's value.
- Each category starts with **BACK**; press white there to return to the list.
- The top right says how long a setting lasts: **THIS SONG - SESSION** or **ALL SONGS - SESSION**
  (see [What's remembered](#whats-remembered)).

### PRACTICE

| Row | Values |
|---|---|
| **LOOP** | OFF · each section of the chart (A, B…) · BARS (the range below) |
| **LOOP FROM / LOOP TO** | The first and last bar for BARS. Changing either switches the loop to BARS. |
| **KEY CYCLE** | OFF · UP 1/2 STEP · DOWN 1/2 STEP · UP A 4TH — after each pass of the loop, through all 12 keys |
| **CHORUSES** | ENDLESS, or 1–16: after the last chorus the band plays the ending |

A loop starts on its first bar after the count-in. Turned on while playing, the band moves into it at the next bar line.

### ARRANGEMENT

| Row | Values |
|---|---|
| **FEEL** | SWING · BALLAD · BOSSA NOVA ([what each does](#feels)) |
| **INTRO** | COUNT-IN (a one-bar click) · LAST 4 BARS (of the form or the loop) · I-VI-II-V (two bars, twice; in minor, i-vi7♭5-ii7♭5-V7♭9). An intro replaces the count-in. |
| **TURNAROUND** | OFF · II-V · III-VI-II-V: replaces the last bar or two of each chorus with a turnaround to the top. Skipped where the chart already has one, and in the last chorus. |
| **ENDING** | FINAL CHORD · TAG + FINAL CHORD (the ii–V to the home key three times first) |

The final chord: everyone hits the downbeat together (crash and kick, bass root, piano and guitar), it rings for two
bars, then the band stops.

### BAND

One row per instrument: **DRUMS**, **BASS**, **PIANO**, **GUITAR**. Turn red to change the player. Each player's
style shows under the list (for example, Max Roach: MELODIC TOMS - HAT ON 2 AND 4). The players are listed
[below](#the-players).

If an instrument's sound pack isn't installed, its row turns **red** with **NO SOUND PACK - A STAND-IN SYNTH PLAYS**.
For João Gilberto without the nylon pack, the note reads **ADD JBNYLON FOR NYLON**.

### SONG

| Row | What it does (press white) |
|---|---|
| **RESET KEY** | Back to the written key. Shows the written key and the current one. |
| **RESET TEMPO** | Back to the songbook's tempo. Shows both. |

---

## The band: feels and players

### Feels

- **SWING:** walking bass, ride cymbal, comping on the swung eighths.
- **BALLAD:** softer and sparser: bass in two, longer chords, a quieter ride.
- **BOSSA NOVA:** straight eighths. Drums play a cross-stick clave over two bars, with the kick on 1, the and of 2, 3
  and the and of 4. Bass plays the root on 1 and the fifth on the and of 2, tied into 3 (Charlie Haden plays just the
  roots). Piano and guitar comp on the clave, each in their own voicings.

Each song starts in the feel set in the songbook (swing or ballad); change it in SETUP › ARRANGEMENT › FEEL.

**3/4.** A chart marked 3/4 (iReal Pro's waltz charts) plays as a jazz waltz: a 3-beat bar, a 3-click count-in, and a
waltz part for every player. Tony Williams, Roy Haynes, Billy Higgins, Jo Jones and Jack DeJohnette use the closest
waltz style from the original drummers; Max Roach and Airto play their own.

### The players

Each player is an impression of how that musician accompanies (where they place notes, how busy they are, their
register and voicings), not a transcription. Player 1 on each instrument is the original band.

**Drums**

| # | Player | Style |
|---|---|---|
| 1 | Jimmy Cobb | Steady ride, light comping |
| 2 | Philly Joe Jones | Driving; snare chatter, rim on 4, triplet fills |
| 3 | Elvin Jones | Triplet waves across the bar, 3 against 4 |
| 4 | Paul Motian | Broken time, open and sparse, cymbal washes |
| 5 | Art Blakey | Strong accents, press-roll builds into sections |
| 6 | Tony Williams | Hi-hat on every beat, fast bursts, tom fills |
| 7 | Roy Haynes | Crisp snare snaps in surprising places |
| 8 | Billy Higgins | Buoyant ride, a light touch |
| 9 | Jo Jones | Time on the hi-hat — Basie swing |
| 10 | Jack DeJohnette | Washy ride; toms and snare talk back |
| 11 | **Max Roach** | Melodic tom phrases, hi-hat on 2 and 4, fills down the kit |
| 12 | Airto | Brazilian percussion: shaker, conga, low tom as surdo, cross-stick clave (in any feel) |

**Bass**

| # | Player | Style |
|---|---|---|
| 1 | Paul Chambers | Classic walking line |
| 2 | Ray Brown | Roots and fifths, big push, approaches from below |
| 3 | Ron Carter | Chromatic runs, wide range |
| 4 | Scott LaFaro | Talks back: triplets and space, higher register |
| 5 | Charles Mingus | Blues riffs, pedals, big accents |
| 6 | Sam Jones | Deep walk, roots on 1 and 3 |
| 7 | Charlie Haden | Simple roots and fifths, room to breathe |
| 8 | Jimmy Garrison | Pedal points and double stops |
| 9 | Walter Page | Even four, walking up the chord |
| 10 | Oscar Pettiford | Melodic bebop lines, pickups, higher |

**Piano**

| # | Player | Style |
|---|---|---|
| 1 | Bill Evans | Rootless voicings |
| 2 | Red Garland | Short stabs on the and of 2 and 4 |
| 3 | McCoy Tyner | Fourths over a left-hand root and fifth |
| 4 | Herbie Hancock | Sparse, high, unexpected placements |
| 5 | Thelonious Monk | Angular chords, displaced stabs |
| 6 | Wynton Kelly | Bouncy, bluesy anticipations |
| 7 | Count Basie | Sparse high plinks with the octave |
| 8 | Ahmad Jamal | Space, two-bar vamps, block chords |
| 9 | Horace Silver | Funky stabs with left-hand punches |
| 10 | Erroll Garner | Left hand strums every beat |

**Guitar**

| # | Player | Style |
|---|---|---|
| 1 | Freddie Green | Four to the bar |
| 2 | Jim Hall | Light, high, lyrical voicings |
| 3 | Wes Montgomery | Punchy stabs, octave fills |
| 4 | Kenny Burrell | Bluesy Charleston, half-step slides |
| 5 | Django Reinhardt | La pompe: short, accented chords |
| 6 | Grant Green | Bluesy stabs, repeated-note riffs |
| 7 | Herb Ellis | Swinging four, slide chords into the next |
| 8 | Barney Kessel | Chord melody with a moving top voice |
| 9 | Charlie Christian | Swing-era four, horn-like riffs |
| 10 | **João Gilberto** | Bossa batida: thumb bass on 1 and 3, chords on the clave; nylon strings (in any feel) |

Mix and match freely: Airto with a swing trio, or Gilberto over Jimmy Cobb.

---

## Sounds

Every player plays recorded instruments from the sound packs:

| Pack | Instrument | Source |
|---|---|---|
| `jbpiano` | Grand piano | Salamander Grand Piano |
| `jbbass` | Upright bass, pizzicato | Karoryfer Meatbass |
| `jbguitar` | Archtop jazz guitar | Karoryfer Shinyguitar |
| `jbdrums` | Jazz kit with toms, plus shaker and conga | Versilian Virtuosity Drums |
| `jbnylon` | Nylon-string classical guitar (João Gilberto only) | FreePats Spanish Classical Guitar |

**If a pack is missing**, a stand-in synth plays that instrument so the band is never silent, and SETUP › BAND shows
the row in red. Common causes: the pack wasn't installed, or it didn't fit in memory (run **Make room**). With the
1.4 drum pack installed, the new toms, shaker and conga use synth stand-ins.

**Your own samples.** For piano, bass and guitar you can also load multisamples made by the companion into synth
slots 6, 7 and 8 (disk mode; see **Your own instrument samples** in the companion's OP-1 tab). Sound packs come
first; a slot sample is used when there's no pack.

---

## Mixing the band

Hold **Shift** and turn:

| Knob | Level |
|---|---|
| Blue | Drums |
| Green | Bass |
| White | Piano |
| Red | Guitar |

Release Shift to return. Green (without Shift) is the whole band's volume, and its press mutes the band.

---

## Tempo and the OP-1's clock

- A song with a tempo in the songbook plays at that tempo. Set a practice tempo per song in the companion's chart
  editor; the original stays on record and PLAY shows it as **ORIG**.
- A song without a tempo follows the OP-1's own tempo (the screen shows **OP-1 BPM**).
- While the OP-1's clock is running within 1 BPM of the song's tempo, the band locks to it, so the metronome and
  sequencers stay in time with it. Over 220 BPM it locks at half time.
- **Red** changes the tempo live. That tempo stays with the song until the OP-1 restarts; to keep it, set the practice
  tempo in the companion and send the songbook again.

---

## What's remembered

op1r plugins can't save files, so settings made on the OP-1 last **until the OP-1 restarts**.

- **Per song:** key, tempo, PRACTICE and ARRANGEMENT settings. Each song gets its own back when you return to it; a new
  song doesn't inherit them.
- **All songs:** the BAND lineup.
- **Kept for good:** whatever is in the songbook (feel, practice tempo), set in the companion.

Sending a new songbook clears the per-song settings, since song numbers then refer to different songs.

---

## Coming from an earlier version

- **From 1.3:** a lineup saved in an OP-1 preset still plays its players, now on samples. Players are changed in
  SETUP › BAND (Shift's second page is gone). Red used to be the feel knob; it's now the tempo, and a saved red
  position never changes the tempo. Blue reads relative to where it sits when a song loads, so a saved position never
  moves the key (in exchange, the key doesn't survive a restart).
- **From 1.4:** install the new `jazzband.op1p`, `jbdrums` and `jbnylon` together, then run **Make room**. The
  SAMPLES / SYNTH rows are gone; nothing to set.

---

## Troubleshooting

**Choosing JAZZBAND in the master FX does nothing.** Restart the OP-1.

**A row in SETUP › BAND is red.** That instrument's pack is missing or didn't fit in memory. Check **Plugins on your
OP-1** in the companion, run **Make room**, and reinstall the pack.

**The SONGS header says BUILT-IN SONGS.** No songbook reached the OP-1. Send it again from the companion and press T1.

**Playback hiccups now and then.** This is known and being investigated (the OP-1's meters can look fine while it
happens). If it happens with a particular lineup or song, note which, since that helps track it down.

**The companion can't find the OP-1.** Use normal USB mode (not disk mode), allow MIDI with SysEx when Chrome asks,
and see **Connection** in the OP-1 tab.

**An install was interrupted.** The outcome can be uncertain: check the OP-1 (and **Plugins on your OP-1**) before
retrying.

---

## Credits

Sound sources and licenses are listed in [`sound packs/CREDITS.md`](sound%20packs/CREDITS.md). The Salamander Grand
Piano is by Alexander Holm (CC-BY 3.0); the other sources are CC0. Build and developer details are in
[`jazzband-op1r/README.md`](jazzband-op1r/README.md) and [`REPOSITORY.md`](REPOSITORY.md).
