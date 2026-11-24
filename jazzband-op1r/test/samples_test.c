/* Host test: the jazzband plugin reads a songbook and instrument multisamples from sampler slot buffers made by the
 * Mac app (opdata.js -> .aif -> raw), with level / polarity changes; checks the fallback, the Shift words, the sample
 * pitch, and renders the band with samples to a WAV. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int host_lines_n;
#include "../plugins/jazzband/jazzband.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } else { printf("ok:   " __VA_ARGS__); printf("\n"); } } while (0)

static int16_t knobs[8] = {14894, 24000, 0, 0};
static op1_beat_t *bc;

static host_sbuf_t *load_slot(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); exit(1); }
    host_sbuf_t *sb = calloc(1, sizeof *sb);
    sb->data = calloc(OP1_SAMPLE_GUARD + OP1_SAMPLE_MAX + 16, 2);
    size_t n = fread(sb->data + OP1_SAMPLE_GUARD, 2, OP1_SAMPLE_MAX, f); fclose(f);
    for (size_t b = 0; b < OP1_PEAK_BUCKETS; b++) {           /* the OP-1's peak table: max per 256 samples */
        int m = 0; for (size_t i = b * 256; i < (b + 1) * 256 && i < n; i++) { int a = abs(sb->data[OP1_SAMPLE_GUARD + i]); if (a > m) m = a; }
        sb->peaks[b] = (int16_t)m;
    }
    return sb;
}
static FILE *frames;
static void frame(const char *label) {
    host_lines_n = 0; SCREEN_jazzband.update(0, 16);
    fprintf(frames, "F %s\n", label);
    for (int i = 0; i < host_lines_n && i < 4096; i++) { host_line_t *l = &host_line_log[i];
        fprintf(frames, "%d %d %d %d %d %08x\n", l->x0, l->y0, l->x1, l->y1, l->w, l->rgba); }
}
static void run(long blocks, FILE *wav, long *frames) {
    for (long b = 0; b < blocks; b++) {
        int16_t l[64] = {0}, r[64] = {0};
        FX_jazzband.render_stereo(2, l, r, knobs);
        bc->next -= OP1_FX_BLOCK << 14; if (bc->next < 0) bc->next += bc->len;
        if (wav) { for (int n = 0; n < 64; n++) { int16_t f[2] = {l[n], r[n]}; fwrite(f, 2, 2, wav); } *frames += 64; }
    }
}
/* frequency of a voice rendered alone (autocorrelation over 0.3 s) */
static double voice_freq(jb_t *e, int inst, int note) {
    static int32_t L[OP1_FX_BLOCK], R[OP1_FX_BLOCK], G[OP1_FX_BLOCK];
    static double buf[44100];
    for (int i = 0; i < JB_VOICES; i++) e->v[i].active = 0;
    ev_t ev = { 0, (uint8_t)inst, (uint8_t)note, 100, 200 };
    e->strum_n = 0; trigger(e, &ev, 256);
    int n = 0;
    while (n < 44100 - 64) {
        for (int k = 0; k < 64; k++) L[k] = R[k] = G[k] = 0;
        for (int i = 0; i < JB_VOICES; i++) if (e->v[i].active) render_voice(e, &e->v[i], L, R, G);
        for (int k = 0; k < 64; k++) buf[n++] = L[k] + R[k] + G[k];
    }
    int st = 4410, W = 8192; double best = -1e300; int bl = 0;
    int lag0 = 20;                                              /* skip the central lobe: start after the first dip below 0 */
    for (; lag0 < 2000; lag0++) { double c = 0; for (int i = st; i < st + W; i++) c += buf[i] * buf[i + lag0]; if (c < 0) break; }
    for (int lag = lag0; lag < 2000; lag++) { double c = 0; for (int i = st; i < st + W; i++) c += buf[i] * buf[i + lag]; if (c > best) { best = c; bl = lag; } }
    double a = 0, b0 = 0, c = 0;
    for (int i = st; i < st + W; i++) { a += buf[i] * buf[i + bl - 1]; b0 += buf[i] * buf[i + bl]; c += buf[i] * buf[i + bl + 1]; }
    double off = 0.5 * (a - c) / (a - 2 * b0 + c);
    return 44100.0 / (bl + off);
}

int main(void) {
    host_sine_init();
    bc = host_beat(); bc->len = (int32_t)((2646000LL * 16384) / 140); bc->next = bc->len / 2; bc->step = 0x100000;
    FX_jazzband.init(2);
    SCREEN_jazzband.init(0);
    CHECK(host_shift_regs == 7, "screen init registers 7 Shift words (%d)", host_shift_regs);
    jb_t *e = &S[2];
    char t[64];

    /* empty slots: built-in songs, synth fallback */
    run(400, 0, 0);
    CHECK(!B.ok && nsongs() == JB_NSONGS, "no songbook: %d built-in songs", nsongs());
    CHECK(!IS[0].ok && src_of(e, I_PIANO) == SRC_SYNTH_A, "empty slot 6: piano falls back to synth A");

    /* load the app's files into slots 5..8 */
    host_sb[4] = load_slot("/tmp/e2e/songbook.raw");
    host_sb[5] = load_slot("/tmp/e2e/piano.raw");
    host_sb[6] = load_slot("/tmp/e2e/bass.raw");          /* at 0.8 level and inverted polarity */
    host_sb[7] = load_slot("/tmp/e2e/guitar.raw");        /* at 1.3 level */
    run(400, 0, 0);
    CHECK(B.ok && B.n == 3, "songbook read from slot 5: %d songs", B.n);
    {
        const char *ti, *c; int tl, key, minor, cl;
        song_info(1, &ti, &tl, &key, &minor, &c, &cl); snprintf(t, sizeof t, "%.*s", tl, ti);
        CHECK(!strcmp(t, "BLUES FOR BRANDONS") && key == 5 && !minor, "song 2: '%s' key %d", t, key);
    }
    CHECK(e->nbars == 32, "song 1 from the book: %d bars", e->nbars);
    knobs[2] = (int16_t)((1 * 2 + 1) * 32768 / (2 * 127));   /* white: step 1 */
    run(2, 0, 0);
    CHECK(e->song == 1 && e->nbars == 24 && op1_fx_wrap[2][2] == 3, "white knob step 1 -> song 2 (%d bars), wraps at %d", e->nbars, op1_fx_wrap[2][2]);
    knobs[2] = 0; run(2, 0, 0);
    for (int k = 0; k < 3; k++) CHECK(IS[k].ok, "slot %d multisample ok: %d notes, gain %.2f", 6 + k, IS[k].nz, IS[k].gain / 32768.0);
    CHECK(IS[0].nz == 6 && IS[0].z[0].root == 48 && IS[0].z[5].root == 78, "piano zones C3 .. F#5");
    CHECK(IS[1].gain > 40000 && IS[1].gain < 42000, "bass at 0.8 level: gain compensates (%.3f)", IS[1].gain / 32768.0);

    /* sample pitch: notes between and on the zones */
    struct { int inst, note; } P[] = { {I_BASS, 33}, {I_BASS, 40}, {I_PIANO, 61}, {I_PIANO, 70}, {I_GUITAR, 50} };
    for (unsigned i = 0; i < sizeof P / sizeof P[0]; i++) {
        double f = voice_freq(e, P[i].inst, P[i].note), want = 440.0 * pow(2, (P[i].note - 69) / 12.0);
        double cents = 1200 * log2(f / want);
        CHECK(fabs(cents) < 5 || fabs(fabs(cents) - 1200) < 5, "sample note %d plays at %.2f Hz (want %.2f, %+.1f cents)", P[i].note, f, want, cents);
    }

    /* Shift words: levels and sounds */
    for (int i = 0; i < 8; i++) host_sw[2][i] = 0;
    host_sw[2][0] = 16384; host_sw[2][1] = 32767; host_sw[2][2] = 8000; host_sw[2][3] = 1;
    host_sw[2][4] = 5 * 32768 / 6;          /* piano: synth B (step 2), as a 1.3 preset saved it */
    e->band_init = 0;                       /* 1.4 reads the old player words once, at start */
    run(1, 0, 0);
    CHECK(e->src[0] == SRC_SAMPLE && e->src[1] == SRC_SAMPLE && abs(e->lvl[0] - 20480) < 4, "Shift words -> samples only (1.5), drums 62%% (%d)", e->lvl[0] * 100 >> 15);
    { ev_t ev = { 0, I_PIANO, 60, 90, 20 }; trigger(e, &ev, 256);
      int found = 0; for (int i = 0; i < JB_VOICES; i++) if (e->v[i].active && (e->v[i].type == VT_EP || e->v[i].type == VT_FMB)) found = 1;
      CHECK(!found, "a 1.3 synth B lineup plays the samples now (no FM voices)"); }
    for (int i = 0; i < 8; i++) host_sw[2][i] = 0;   /* back to defaults */
    frames = fopen("out/frames2.txt", "w");
    run(2, 0, 0); frame("songbook song 1");
    host_shift = 1; host_layer = 0; frame("shift: levels");
    host_lines_n = 0; SCREEN_jazzband.update(0, 16);
    CHECK(host_lines_n > 50 && host_missing_glyphs == 0, "Shift screen (the mixer) draws %d lines", host_lines_n);
    host_shift = 0;
    e->screen = SCR_SETUP; e->scat = SC_BAND; e->srow = 6; host_lines_n = 0; SCREEN_jazzband.update(0, 16);
    CHECK(host_lines_n > 50 && host_missing_glyphs == 0, "SETUP BAND (sounds) draws %d lines", host_lines_n);
    frame("setup band: piano sound");
    e->screen = SCR_PLAY; e->scat = -1; e->srow = 0;

    /* the band with samples, to a WAV */
    FILE *w = fopen("/tmp/e2e/band_samples.wav", "wb"); char h[44] = {0}; fwrite(h, 1, 44, w); long nfr = 0;
    knobs[7] = (int16_t)(knobs[7] ^ (int16_t)0x8000);   /* red push: play */
    run(3200, w, &nfr);
    int busy = 0; for (int i = 0; i < JB_VOICES; i++) busy += e->v[i].active;
    CHECK(e->state == ST_PLAY, "band plays with samples (bar %d, %d voices)", e->bar + 1, busy);
    /* slot cleared while playing: falls back without a crash */
    host_sb[5] = 0; run(400, w, &nfr);
    e->screen = SCR_SETUP; e->scat = SC_BAND; e->srow = 6; frame("setup band: slot 6 emptied"); e->screen = SCR_PLAY;
    CHECK(!IS[0].ok && src_of(e, I_PIANO) == SRC_SYNTH_A, "piano slot emptied while playing: synth A takes over");
    stop(e); run(300, w, &nfr);
    uint32_t data = (uint32_t)nfr * 4, sr = 44100, br = sr * 4, riff = 36 + data, fmt = 16; uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
    fseek(w, 0, SEEK_SET); fwrite("RIFF", 1, 4, w); fwrite(&riff, 4, 1, w); fwrite("WAVEfmt ", 1, 8, w); fwrite(&fmt, 4, 1, w);
    fwrite(&pcm, 2, 1, w); fwrite(&ch, 2, 1, w); fwrite(&sr, 4, 1, w); fwrite(&br, 4, 1, w); fwrite(&ba, 2, 1, w); fwrite(&bits, 2, 1, w);
    fwrite("data", 1, 4, w); fwrite(&data, 4, 1, w); fclose(w);

    fclose(frames);
    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
