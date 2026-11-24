/* Host test for the jazzband plugin: simulates the OP-1 beat clock and the FX knobs, renders audio to WAV,
 * checks timing, levels, play/stop, knobs and the screen, and dumps screen frames for a PNG preview. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
int host_lines_n;
#include "../plugins/jazzband/jazzband.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } else { printf("ok:   " __VA_ARGS__); printf("\n"); } } while (0)

static int16_t knobs[8];
static op1_beat_t *bc;
static void clock_block(void) { bc->next -= OP1_FX_BLOCK << 14; if (bc->next < 0) bc->next += bc->len; }

static FILE *wav; static long wav_frames;
static void wav_open(const char *path) {
    wav = fopen(path, "wb"); static const char h[44] = {0}; fwrite(h, 1, 44, wav); wav_frames = 0;
}
static void wav_close(void) {
    uint32_t data = (uint32_t)wav_frames * 4, sr = 44100, br = sr * 4, riff = 36 + data, fmt = 16; uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav); fwrite(&riff, 4, 1, wav); fwrite("WAVEfmt ", 1, 8, wav); fwrite(&fmt, 4, 1, wav);
    fwrite(&pcm, 2, 1, wav); fwrite(&ch, 2, 1, wav); fwrite(&sr, 4, 1, wav); fwrite(&br, 4, 1, wav); fwrite(&ba, 2, 1, wav);
    fwrite(&bits, 2, 1, wav); fwrite("data", 1, 4, wav); fwrite(&data, 4, 1, wav); fclose(wav);
}

/* render n blocks at the master position; returns peak, accumulates energy */
static double energy; static long clipped;
static int run(long blocks, int write) {
    int peak = 0;
    for (long b = 0; b < blocks; b++) {
        int16_t l[64] = {0}, r[64] = {0};
        FX_jazzband.render_stereo(2, l, r, knobs);
        clock_block();
        for (int n = 0; n < 64; n++) {
            int a = abs(l[n]) > abs(r[n]) ? abs(l[n]) : abs(r[n]);
            if (a > peak) peak = a;
            if (a >= 32767) clipped++;
            energy += (double)l[n] * l[n] + (double)r[n] * r[n];
            if (write) { int16_t f[2] = {l[n], r[n]}; fwrite(f, 2, 2, wav); wav_frames++; }
        }
    }
    return peak;
}

static FILE *frames;
static void frame(const char *label) {
    host_lines_n = 0;
    SCREEN_jazzband.update(0, 16);
    fprintf(frames, "F %s\n", label);
    for (int i = 0; i < host_lines_n && i < 4096; i++) {
        host_line_t *l = &host_line_log[i];
        fprintf(frames, "%d %d %d %d %d %08x\n", l->x0, l->y0, l->x1, l->y1, l->w, l->rgba);
    }
}
/* 1.4 controls: red push = bit 15 of word 7, white push = bit 7, blue press = latch flag 0, green press = latch flag 1;
 * white and red turn in relative steps, blue is a plain knob in 12 positions */
static void red_push(void) { knobs[7] = (int16_t)(knobs[7] ^ (int16_t)0x8000); }
static void blue_push(void) { op1_fx_page[2][0] ^= 1; }
static void white_push(void) { knobs[7] ^= 0x80; }
static int wpos;
static void white_turn(int d) { wpos = ((wpos + d) % 127 + 127) % 127; knobs[2] = (int16_t)((wpos * 32768 + 16384) / 127); }
static void blue_at(int i) { knobs[0] = (int16_t)(i * 32767 / 11); }
static void go_screen(int to) {                       /* the switcher: blue press, white to the screen, white press */
    blue_push(); run(1, 1);
    int d = to - S[2].sw_row; white_turn(d); run(1, 1); white_push(); run(1, 1);
}
static const char *cname(chord_t c, char *buf) { sprintf(buf, "%s%s", ROOTS[c.root], QUALS[c.q]); return buf; }

int main(int argc, char **argv) {
    int choruses = argc > 1 ? atoi(argv[1]) : 2;
    int bpm = argc > 2 ? atoi(argv[2]) : 140;
    setvbuf(stdout, 0, _IONBF, 0);
    host_sine_init();
    bc = host_beat();
    bc->len = (int32_t)((2646000LL * 16384) / bpm); bc->next = bc->len / 3; bc->step = 0x100000;
    knobs[0] = 14894; knobs[1] = 24000; knobs[2] = 0; knobs[3] = 0;   /* blue at position 5 */
    for (int i = 0; i < 4; i++) host_knobs[i] = knobs[i];
    FX_jazzband.init(2);
    SCREEN_jazzband.init(0);
    frames = fopen("out/frames.txt", "w");
    jb_t *e = &S[2];
    char b1[16], b2[16];

    CHECK(e->nbars == 32, "autumn leaves parses to 32 bars (%d)", e->nbars);
    CHECK(ceq(e->bars[0].beat[0], (chord_t){0, Q_MIN7}) && ceq(e->bars[5].beat[0], (chord_t){2, Q_DOM7B9}),
          "bar 1 = %s, bar 6 = %s", cname(e->bars[0].beat[0], b1), cname(e->bars[5].beat[0], b2));
    CHECK(ceq(e->bars[26].beat[0], (chord_t){7, Q_MIN7}) && ceq(e->bars[26].beat[2], (chord_t){0, Q_DOM7}), "bar 27 = Gm7 C7");

    energy = 0;
    int pk = run(700, 0);
    frame("stopped");
    CHECK(pk == 0, "silent while stopped (peak %d)", pk);
    CHECK(e->transpose == 0, "initial key is the song's original key");
    CHECK(FX_jazzband.press[1] == OP1_PRESS_LATCH, "green press is a band mute latch");

    CHECK(FX_jazzband.press[0] == OP1_PRESS_LATCH && FX_jazzband.press[2] == OP1_FX_STEPS(127) && FX_jazzband.press[3] == OP1_FX_STEPS(127),
          "knobs: blue plain + press latch, white and red stepped");
    CHECK(host_push_regs == 2, "white and red pushes registered (%d)", host_push_regs);
    /* press red: arm, start on the next OP-1 beat */
    red_push();
    run(1, 0);
    CHECK(e->state == ST_ARMED || e->state == ST_COUNT, "red press arms the band (state %d)", e->state);
    int waited = 0; while (e->state == ST_ARMED && waited < 2000) { run(1, 0); waited++; }
    long expect_wait = (bc->len >> 14) / 64 + 2;
    CHECK(e->state == ST_COUNT && waited <= expect_wait, "starts on the next beat (%d blocks)", waited);
    frame("count-in");

    wav_open("out/jazzband_autumn.wav");
    /* bar timing: blocks between bar changes */
    int last_bar = -1; long blocks = 0, bar_start = -1, bar_len_min = 1 << 30, bar_len_max = 0; int bars_seen = 0;
    long total = (long)((4.0 * 60 / bpm * 44100 / 64) * (1 + 32 * choruses)) + 10;
    energy = 0; clipped = 0; int peak = 0, max_voices = 0;
    for (; blocks < total; blocks++) {
        int p = run(1, 1); if (p > peak) peak = p;
        int act = 0; for (int i = 0; i < JB_VOICES; i++) act += e->v[i].active; if (act > max_voices) max_voices = act;
        if (e->state == ST_PLAY && e->bar != last_bar) {
            if (bar_start >= 0) { long d = blocks - bar_start; if (d < bar_len_min) bar_len_min = d; if (d > bar_len_max) bar_len_max = d; }
            bar_start = blocks; last_bar = e->bar; bars_seen++;
            if (bars_seen == 1) frame("bar 1");
            if (e->bar == 26 && bars_seen < 40) { run(0, 0); frame("bar 27 (two chords)"); }
        }
    }
    double expect_bar = 4.0 * 60 / bpm * 44100 / 64;
    CHECK(bar_len_min >= expect_bar - 1.5 && bar_len_max <= expect_bar + 1.5, "bars last %ld..%ld blocks (expected %.1f at %d bpm)",
          bar_len_min, bar_len_max, expect_bar, bpm);
    double rms = sqrt(energy / (2.0 * total * 64)) / 32768;
    CHECK(rms > 0.02 && rms < 0.5, "band level rms %.3f, peak %d", rms, peak);
    CHECK(clipped == 0, "no clipped samples (%ld)", clipped);
    CHECK(max_voices <= JB_VOICES, "max active voices %d of %d", max_voices, JB_VOICES);
    CHECK(host_missing_glyphs == 0, "screen uses only font glyphs (%d missing)", host_missing_glyphs);

    /* key while playing: queued for the next bar line, chart / bass / piano / guitar together */
    blue_at(7); run(1, 1);                                    /* 2 positions up from where the key was set */
    CHECK(e->key_pend && e->key_tgt == 2 && e->transpose == 0, "blue while playing queues +2 for the next bar (now %d)", e->transpose);
    frame("key queued");
    { long w0 = 0; int b0 = e->bar; while (e->bar == b0 && w0 < 3000) { run(1, 1); w0++; } while (e->tick == 0 && w0 < 3000) { run(1, 1); w0++; } }
    CHECK(!e->key_pend && e->transpose == 2 && cur_chord(e, 0).root == pcof(e->vb.beat[0].root + 2), "at the bar line: the band plays +2");
    { int n0 = 0, ok = 1; chord_t c0 = cur_chord(e, 0);   /* every note of the new bar's downbeat fits the chord in the new key */
      for (int i = 0; i < e->nev; i++) if (e->ev[i].tick == 0 && e->ev[i].inst != I_DRUMS) { n0++; ok = ok && in_scale(c0, e->ev[i].note); }
      CHECK(n0 > 0 && ok, "bass, piano and guitar play the new key on the downbeat (%d notes)", n0); }
    run(400, 1);

    /* SONGS: browse, then a white press queues the song for the next bar; the band keeps playing */
    go_screen(SCR_SONGS);
    CHECK(e->screen == SCR_SONGS && e->state == ST_PLAY && e->song == 0, "blue, white, white: SONGS; the band plays on");
    white_turn(1); run(1, 1);
    CHECK(e->cursor == 1 && e->song == 0 && e->pend < 0, "a white turn browses only (cursor on song 2)");
    frame("song browser");
    int bar_before = e->bar;
    white_push(); run(1, 1);
    CHECK(e->state == ST_PLAY && e->pend == 1, "white push queues the song, does not stop");
    frame("switch queued");
    long w = 0; while (e->pend >= 0 && w < 2000) { run(1, 1); w++; }
    CHECK(e->song == 1 && e->nbars == 12 && e->bar == 0 && (bar_before != e->bar || w > 0) && w <= 1182 + 2,
          "switches to blues in F at the next bar line (%ld blocks)", w);
    CHECK(e->transpose == 0, "the new song starts in its own key (no stale key)");
    go_screen(SCR_PLAY);
    CHECK(e->screen == SCR_PLAY, "back to PLAY");
    run(800, 1);
    frame("blues");

    /* the song's tempo: built-ins carry 140 / 120; the OP-1 clock (here 100 bpm) is not followed */
    {
        jb_host_op1_tempo = 0;
        red_push(); run(1, 1);
        CHECK(e->state == ST_PLAY && e->ending == 1, "red while playing: finish after this pass");
        red_push(); run(1, 1);
        CHECK(e->state == ST_STOP, "red twice stops");
        int32_t keep = bc->len; bc->len = (int32_t)((2646000LL * 16384) / 100);
        go_screen(SCR_SONGS); white_turn(-1); run(1, 1);
        CHECK(e->song == 1, "stopped: browsing does not load");
        white_push(); run(1, 1);
        CHECK(e->song == 0 && e->bpm == 140 && e->state == ST_STOP, "white push loads autumn leaves at its 140, not started (%d)", e->bpm);
        go_screen(SCR_PLAY);
        frame("stopped, song tempo");
        red_push();
        long n = 0, b0 = -1, b1 = -1;
        while (n < 4000 && b1 < 0) { run(1, 1); n++; if (e->state == ST_PLAY && e->bar == 1 && b0 < 0) b0 = n; if (e->state == ST_PLAY && e->bar == 2) b1 = n; }
        double want = 4.0 * 60 / 140 * 44100 / 64;
        CHECK(b0 > 0 && b1 > 0 && fabs((b1 - b0) - want) <= 1.5, "bar at the song's 140 bpm: %ld blocks (want %.1f; OP-1 at 100)", b1 - b0, want);
        /* red turn: tempo at once, no stop, no restart, the form goes on */
        { int bar0 = e->bar, ch0 = e->chorus, t0 = e->tick; knobs[3] = (int16_t)((5 * 32768 + 16384) / 127); run(1, 1);
          CHECK(e->bpm == 145 && e->state == ST_PLAY && e->bar == bar0 && e->chorus == ch0 && e->tick >= t0, "red 5 clicks: 145 BPM, still playing the same bar");
          frame("tempo 145");
          knobs[3] = (int16_t)((0 * 32768 + 16384) / 127); run(1, 1);
          CHECK(e->bpm == 140 && TADJ[0] == 0, "back 5 clicks: 140, the songbook tempo again"); }
        go_screen(SCR_SONGS); white_turn(1); run(1, 1); white_push(); run(1, 1);     /* choose blues in F while playing */
        long w2 = 0; while (e->song != 1 && w2 < 3000) { run(1, 1); w2++; }
        CHECK(e->song == 1 && e->bpm == 120, "blues in F switches in at its own 120 bpm (%d)", e->bpm);
        CHECK(e->s_pend < 0, "the queued-song note clears with the switch");
        go_screen(SCR_PLAY);
        run(300, 1); frame("blues at 120");
        CHECK(!e->sync, "OP-1 at 100: band on its own clock");
        { int32_t k2 = bc->len; bc->len = (int32_t)((2646000LL * 16384) / 120); run(2, 1);
          CHECK(e->sync && e->s_op1bpm10 == 1200, "OP-1 set to 120: the band follows the OP-1 clock (%d)", e->s_op1bpm10);
          frame("blues in time with the OP-1"); bc->len = k2; run(2, 1); }
        bc->len = keep; jb_host_op1_tempo = 1; set_bpm(e, 0); run(1, 1);   /* playing on, OP-1 clock again */
    }

    /* ballad feel: SETUP ARRANGEMENT FEEL (red turns the value) */
    go_screen(SCR_SETUP); white_turn(1); run(1, 1); white_push(); run(1, 1);   /* ARRANGEMENT */
    CHECK(e->scat == SC_ARRANGE && e->srow == 0, "SETUP: ARRANGEMENT opens on BACK");
    white_turn(1); run(1, 1);
    { int t3 = (knobs[3] * 127) >> 15; knobs[3] = (int16_t)(((t3 + 1) * 32768 + 16384) / 127); run(1, 1); }
    CHECK(e->ballad == 1, "FEEL: red turn = ballad");
    run(600, 1);
    { int t3 = (knobs[3] * 127) >> 15; knobs[3] = (int16_t)(((t3 + 1) * 32768 + 16384) / 127); run(1, 1); }
    CHECK(e->ballad == 0, "and back to swing");
    go_screen(SCR_PLAY);

    /* Mute the band without stopping its clock or muting the live instrument. */
    int saved_bar = e->bar, saved_tick = e->tick;
    op1_fx_page[2][1] = 1;
    run(2, 0);  /* allow the one-block output gain ramp to settle */
    { int16_t l[64], r[64];
      for (int n = 0; n < 64; n++) { l[n] = (int16_t)(n * 100); r[n] = (int16_t)(-n * 80); }
      FX_jazzband.render_stereo(2, l, r, knobs); clock_block();
      int same = 1;
      for (int n = 0; n < 64; n++) if (l[n] != n * 100 || r[n] != -n * 80) same = 0;
      CHECK(same && e->s_muted, "band mute preserves both live input channels"); }
    CHECK(knobs[1] == 24000 && e->s_volume == 24000, "mute preserves band volume");
    run(400, 0);
    CHECK(e->state == ST_PLAY && (e->bar != saved_bar || e->tick != saved_tick), "band keeps its place while muted");
    frame("band muted");
    op1_fx_page[2][1] = 0;
    energy = 0; pk = run(400, 0);
    CHECK(pk > 1000 && !e->s_muted, "unmute restores audible band");
    host_shift = 1; host_layer = 0; frame("mix controls");
    host_shift = 0;
    /* the 1.3 Shift layer 2 (player + sound in one word) is read once at start: a saved lineup keeps playing */
    e->band_init = 0;
    host_sw[2][4] = 7 * 32768 / (2 * PLAYER_STEPS[0]); host_sw[2][5] = 9 * 32768 / (2 * PLAYER_STEPS[1]); host_sw[2][6] = 17 * 32768 / (2 * PLAYER_STEPS[2]); host_sw[2][7] = 23 * 32768 / (2 * PLAYER_STEPS[3]);
    run(1, 0);
    e->band_init = 1;   /* later changes of those words do nothing: SETUP BAND owns the lineup */
    CHECK(e->sty[0] == 1 && e->sty[1] == 1 && e->sty[2] == 2 && e->sty[3] == 3 && e->src[0] == SRC_SAMPLE && e->src[1] == SRC_SAMPLE && e->src[2] == SRC_SAMPLE,
          "players layer (1.5: players kept, samples only): drums %d, bass %d/%d, piano %d/%d, guitar %d/%d", e->sty[0], e->sty[1], e->src[1], e->sty[2], e->src[0], e->sty[3], e->src[2]);
    frame("players");
    for (int i = 4; i < 8; i++) host_sw[2][i] = (int16_t)((2 * (PLAYER_STEPS[i - 4] - 1) + 1) * 32768 / (2 * PLAYER_STEPS[i - 4]));
    run(1, 0);
    CHECK(e->sty[0] == 1 && e->sty[1] == 1, "after start, Shift words no longer change the players");
    for (int i = 4; i < 8; i++) host_sw[2][i] = 0;
    CHECK(host_missing_glyphs == 0, "control labels use supported font glyphs");

    /* 3/4: a T34 chart counts and draws in 3 */
    { static const char W[] = "T34 *A Bb^7 | D7 | Eb^7 | G7 | C-7 | G7 C-7 | F7 | Bb^7 |";
      load_chart(e, W, (int)sizeof W - 1); e->bar = 0; e->tick = 0; form_reset(e);
      long t0 = 0; int seen = 0, maxbeat = 0;
      while (seen < 3 && t0 < 20000) { int b0 = e->bar; run(1, 0); t0++; if (e->tick / JB_TPB > maxbeat) maxbeat = e->tick / JB_TPB; if (e->bar != b0) seen++; }
      CHECK(e->nb == 3 && maxbeat == 2, "3/4 chart: beats 1..3 only (%d beats a bar)", maxbeat + 1);
      while (e->tick != 2 * JB_TPB + 1) run(1, 0);
      frame("waltz (3/4)");
      load_song(e, e->song); e->bar = 0; e->tick = 0; }

    /* stop: band fades out */
    red_push(); run(1, 1);
    red_push(); run(1, 1);
    CHECK(e->state == ST_STOP, "red twice stops");
    run(60, 1);
    int tail = run(60, 1);
    CHECK(tail < 200, "silent shortly after stop (peak %d)", tail);
    frame("stopped again");
    wav_close();

    /* input passes through untouched once the tails are gone */
    run(800, 0);
    { int16_t l[64], r[64]; for (int n = 0; n < 64; n++) l[n] = r[n] = (int16_t)(n * 100);
      FX_jazzband.render_stereo(2, l, r, knobs); int same = 1; for (int n = 0; n < 64; n++) if (l[n] != n * 100) same = 0;
      CHECK(same, "input passes through while stopped"); }
    /* mono path (synth / drum FX slot) */
    { FX_jazzband.init(0); long pk0 = 0;
      { int16_t buf[64] = {0}; FX_jazzband.render(0, buf, knobs); }
      red_push();
      for (int b = 0; b < 3000; b++) { int16_t buf[64] = {0}; FX_jazzband.render(0, buf, knobs); clock_block();
        for (int n = 0; n < 64; n++) if (abs(buf[n]) > pk0) pk0 = abs(buf[n]); }
      CHECK(pk0 > 1000 && pk0 < 32767, "mono FX slot plays (peak %ld)", pk0);
      op1_fx_page[0][1] = 1;
      for (int b = 0; b < 2; b++) { int16_t buf[64] = {0}; FX_jazzband.render(0, buf, knobs); }
      int16_t buf[64]; for (int n = 0; n < 64; n++) buf[n] = (int16_t)(n * 80);
      FX_jazzband.render(0, buf, knobs);
      int same = 1; for (int n = 0; n < 64; n++) if (buf[n] != n * 80) same = 0;
      CHECK(same, "mono band mute preserves live input");
      CHECK(!S[2].s_muted, "mono mute does not affect master mute state"); }

    fclose(frames);
    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
