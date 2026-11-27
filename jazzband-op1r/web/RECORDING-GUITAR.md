# Recording our own guitar for the play-along

Plan for replacing the melody (and, later, comping) guitar samples with recordings of our own guitar. The engine
already plays a pack built by `web/make_web_packs.py` (`jbgtrlead`, slot 6) and adds slides, hammer-ons and vibrato
on top, so the recordings only need to be clean, single picked notes. Nothing in the app changes: a new pack with
the same name replaces the current one.

## What to record

- **Notes:** every minor third from E2 to G5 (E2 G2 Bb2 C#3 E3 G3 Bb3 C#4 E4 G4 Bb4 C#5 E5 G5): 14 notes. The engine
  shifts each by up to a semitone and a half, which sounds natural. Every semitone (about 40 notes) sounds better
  still, if there is time.
- **Strings:** play each note where you would play it in a head (neck position, the string you would use), not all
  on one string.
- **Dynamics:** two layers, soft (fingertip / light pick) and firm.
- **Takes:** three of each note and dynamic, so repeated notes are not identical.
- **Length:** let every note ring until it dies (6-10 s), then mute it. One file per take.
- **Optional, for more realism later:** a hammer-on and a pull-off of a tone at a few positions; a slide up a
  semitone into a note; a note with your own vibrato (to model its speed and depth).
- **For comping (later):** muted strums and the chord shapes you use most (maj7, m7, 7, m7b5) at a few positions.

Total for the core set: 14 notes x 2 dynamics x 3 takes = 84 files, about 15-20 minutes of recording.

## How to record

- Guitar into the amp you like the sound of, one microphone on the speaker about 10-20 cm away, slightly off the
  centre of the cone; or an amp simulator if that is the sound you play with. **One source only**: do not blend a
  direct signal and a microphone recorded on separate takes (that is what made the Karoryfer library sound phasey).
  If you want both, record them at the same time on two channels of the same take.
- A quiet room, no reverb or effects (the app adds its own room), no compression or limiting.
- 48 kHz or 44.1 kHz, 24-bit WAV, mono, peaks around -6 dB on the firm notes.
- Tune carefully before each string; check tuning every few minutes.
- Leave about half a second of silence before each note.

## Naming

`<note><octave>_<soft|firm>_<take>.wav`, for example `C#4_soft_2.wav`, `Bb2_firm_1.wav` (sharps as `#`, flats as `b`).
Put them in one folder; I write a small zone list from the file names.

## Then

1. Send the folder (a zip in the chat, or a private repository).
2. I add a `zones_ownguitar()` to `web/make_web_packs.py`, build the pack, check tuning and levels per note, and
   render a few tunes for a listen before it goes live.
3. If the optional takes are there: real hammer-on and slide samples replace the engine's pitch-glide versions, and
   the vibrato settings are matched to yours.
