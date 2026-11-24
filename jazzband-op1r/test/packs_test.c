/* Sound packs end to end: the packer's jbpiano / jbbass / jbguitar / jbdrums .op1p files are parsed, CRC-checked and
 * relocated by op1r's own loader code (mock/op1r-loader), then jazzband finds them through op1_fxs[], checks their sums
 * and plays them. Checks the decoder against the packer's (its preview WAVs hold every zone decoded), the pitch of a
 * transposed note, the level of each instrument against its synth, and renders band demos with the real sounds. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int host_lines_n;
#include "../plugins/jazzband/jazzband.c"
#include "op1r-loader/op1p_load.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } else { printf("ok:   " __VA_ARGS__); printf("\n"); } } while (0)
static int16_t knobs[8] = {14894, 24000, 0, 0};
static uint32_t rd(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static const char *PN[5] = {"jbdrums", "jbbass", "jbpiano", "jbguitar", "jbnylon"};

/* load one pack with op1r's loader, then a host view: jbpack_t (host layout) followed by the audio */
static jbpack_t *load_pack(const char *dir, const char *name, uint32_t *ram) {
    char path[512]; snprintf(path, sizeof path, "%s/%s.op1p", dir, name);
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); exit(2); }
    static uint8_t file[3 << 20] __attribute__((aligned(4)));
    uint32_t len = (uint32_t)fread(file, 1, sizeof file, f); fclose(f);
    op1p_view v; int r = op1p_parse(file, len, &v);
    if (r) { printf("FAIL: op1r loader refuses %s (%d)\n", name, r); fails++; return 0; }
    uint8_t *mem = malloc(v.mem_size); uint32_t imp[OP1P_MAX_IMPORTS];
    for (uint32_t i = 0; i < v.hdr->n_imports; i++) imp[i] = 0x01000000u + 16 * i;
    r = op1p_relocate(&v, mem, 0x00400000u, imp);
    CHECK(r == 0 && !strcmp(v.hdr->name, name) && v.hdr->bss_size == 0, "%s: op1r loader parses and relocates it (%u KB in RAM)", name, v.mem_size >> 10);
    *ram = v.mem_size;
    const uint8_t *e = mem + v.hdr->entry_off;                   /* Blackfin layout: op1_fx_t is 16 bytes */
    uint32_t data_off = rd(e + 32), data_len = rd(e + 36);
    jbpack_t *h = calloc(1, sizeof(jbpack_t) + data_len + 8);
    memcpy(h->fx.press, e + 8, 4);
    h->magic[0] = rd(e + 16); h->magic[1] = rd(e + 20); h->inst = rd(e + 24); h->nzones = rd(e + 28);
    h->data_len = data_len; h->sum = rd(e + 40); h->version = rd(e + 44); memcpy(h->credit, e + 48, 96);
    memcpy(h->zone, e + 144, sizeof h->zone);
    h->data_off = sizeof(jbpack_t); memcpy((uint8_t *)h + sizeof(jbpack_t), e + data_off, data_len);
    CHECK(v.hdr->entry_off + data_off + data_len <= v.mem_size, "%s: audio inside the loaded image", name);
    free(mem);
    return h;
}
static void run(long blocks, FILE *w, long *frames) {
    op1_beat_t *bc = host_beat();
    for (long b = 0; b < blocks; b++) {
        int16_t l[64] = {0}, r[64] = {0};
        FX_jazzband.render_stereo(2, l, r, knobs);
        bc->next -= OP1_FX_BLOCK << 14; if (bc->next < 0) bc->next += bc->len;
        if (w) { for (int n = 0; n < 64; n++) { int16_t f[2] = {l[n], r[n]}; fwrite(f, 2, 2, w); } *frames += 64; }
    }
}
static FILE *wav_open(const char *p) { FILE *w = fopen(p, "wb"); static const char h[44] = {0}; fwrite(h, 1, 44, w); return w; }
static void wav_close(FILE *w, long frames) {
    uint32_t data = (uint32_t)frames * 4, sr = 44100, br = sr * 4, riff = 36 + data, fmt = 16; uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
    fseek(w, 0, SEEK_SET); fwrite("RIFF", 1, 4, w); fwrite(&riff, 4, 1, w); fwrite("WAVEfmt ", 1, 8, w); fwrite(&fmt, 4, 1, w);
    fwrite(&pcm, 2, 1, w); fwrite(&ch, 2, 1, w); fwrite(&sr, 4, 1, w); fwrite(&br, 4, 1, w); fwrite(&ba, 2, 1, w);
    fwrite(&bits, 2, 1, w); fwrite("data", 1, 4, w); fwrite(&data, 4, 1, w); fclose(w);
}
/* which pack a note starts from: 1 = P (that pack), 0 = another */
static int starts_in(jb_t *e, int inst, int note, int vel, const jbpack_t *P) {
    for (int i = 0; i < JB_VOICES; i++) e->v[i].active = 0;
    ev_t ev = { 0, (uint8_t)inst, (uint8_t)note, (uint8_t)vel, 20 }; e->strum_n = 0; trigger(e, &ev, 256);
    for (int i = 0; i < JB_VOICES; i++) if (e->v[i].active && e->v[i].type == VT_PK)
        return e->v[i].pk_d >= (const uint8_t *)P && e->v[i].pk_d < (const uint8_t *)P + P->data_off + P->data_len;
    return 0;
}
/* one voice alone: rms and frequency (autocorrelation over 0.1 .. 0.4 s) */
static void voice_alone(jb_t *e, int inst, int note, int vel, double *rms, double *freq) {
    static int32_t L[64], R[64], G[64]; static double buf[44100];
    for (int i = 0; i < JB_VOICES; i++) e->v[i].active = 0;
    ev_t ev = { 0, (uint8_t)inst, (uint8_t)note, (uint8_t)vel, 200 };
    e->strum_n = 0; trigger(e, &ev, 256);
    int n = 0; double en = 0;
    while (n < 44100 - 64) {
        for (int k = 0; k < 64; k++) L[k] = R[k] = G[k] = 0;
        for (int i = 0; i < JB_VOICES; i++) if (e->v[i].active) render_voice(e, &e->v[i], L, R, G);
        for (int k = 0; k < 64; k++) { buf[n] = L[k] + R[k] + G[k]; if (n < 22050) en += buf[n] * buf[n]; n++; }
    }
    *rms = sqrt(en / 22050);                                     /* the first half second: what a comping note sounds like */
    if (!freq) return;
    int st = 4410, W = 13230; double best = -1e300; int bl = 0, lag0 = 20;
    for (; lag0 < 2400; lag0++) { double c = 0; for (int i = st; i < st + W; i++) c += buf[i] * buf[i + lag0]; if (c < 0) break; }
    for (int lag = lag0; lag < 2400; lag++) { double c = 0; for (int i = st; i < st + W; i++) c += buf[i] * buf[i + lag]; if (c > best) { best = c; bl = lag; } }
    double a = 0, b0 = 0, c = 0;
    for (int i = st; i < st + W; i++) { a += buf[i] * buf[i + bl - 1]; b0 += buf[i] * buf[i + bl]; c += buf[i] * buf[i + bl + 1]; }
    *freq = 44100.0 / (bl + 0.5 * (a - c) / (a - 2 * b0 + c));
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: packs_test <packs/out dir>\n"); return 2; }
    setvbuf(stdout, 0, _IOLBF, 0);
    host_sine_init();
    jbpack_t *P[5]; uint32_t ram = 0, tot = 0;
    for (int i = 0; i < 5; i++) { P[i] = load_pack(argv[1], PN[i], &ram); tot += ram; if (!P[i]) return 1; op1_fxs[2 + i] = &P[i]->fx; }
    op1_fxs[1] = &FX_jazzband; op1_fx_count = 7;
    CHECK(tot < 3300u << 10, "all five packs: %u KB of plugin memory (op1r's cap for all plugins together: 4096 KB)", tot >> 10);

    jb_t *e = &S[2];
    FX_jazzband.init(2); SCREEN_jazzband.init(0);
    op1_beat_t *bc = host_beat(); bc->len = (int32_t)((2646000LL * 16384) / 160); bc->next = 0; bc->step = 0;
    long blocks = 0; while (PKS.state != 2 && blocks < 5000) { run(1, 0, 0); blocks++; }
    CHECK(PKS.ok[0] && PKS.ok[1] && PKS.ok[2] && PKS.ok[3] && PKS.ok[PK_NYLON], "jazzband finds all five packs and their checksums match (%ld blocks, %.2f s)", blocks, blocks * 64 / 44100.0);

    /* the decoder: zone 0 of each pack against the packer's own decode (preview WAV, 16-bit) */
    for (int i = 0; i < 5; i++) {
        char path[512]; snprintf(path, sizeof path, "%s/%s-preview.wav", argv[1], PN[i]);
        FILE *f = fopen(path, "rb"); int16_t ref[4096]; int nref = 0;
        if (f) { uint8_t h[44]; if (fread(h, 1, 44, f) == 44) nref = (int)fread(ref, 2, 4096, f); fclose(f); }
        voice_t vv = {0}; const jbzone_t *z = &P[i]->zone[0];
        vv.pk_d = pk_data(PKS.ok[i]) + z->off; vv.pk_len = (int32_t)z->len; vv.pk_codec = z->codec; vv.pk_pred = z->pred; vv.pk_idx = z->idx;
        int worst = 0; for (int k = 0; k < nref; k++) { int y = pk_next(&vv), d = abs(y - ref[k]); if (d > worst) worst = d; }
        CHECK(nref > 1000 && worst <= 1, "%s: plugin decoder = packer decoder on zone 0 (%d samples, max difference %d)", PN[i], nref, worst);
    }
    /* pitch: notes between the sampled roots */
    { static const int T[][3] = {{I_PIANO, 61, 60}, {I_PIANO, 70, 70}, {I_BASS, 31, 70}, {I_GUITAR, 52, 50}};
      e->src[0] = e->src[1] = e->src[2] = SRC_SAMPLE; for (int i = 0; i < 4; i++) e->lvl[i] = 32768;
      for (int t = 0; t < 4; t++) {
          double rms, f; voice_alone(e, T[t][0], T[t][1], T[t][2], &rms, &f);
          double want = 440 * pow(2, (T[t][1] - 69) / 12.0), cents = 1200 * log2(f / want);
          if (cents > 600) cents -= 1200; if (cents < -600) cents += 1200;     /* the autocorrelation may lock an octave off */
          CHECK(fabs(cents) < 25, "%s note %d from its pack: %.1f Hz, %+.0f cents", T[t][0] == I_PIANO ? "piano" : T[t][0] == I_BASS ? "bass" : "guitar", T[t][1], f, cents);
      } }
    /* level: each instrument with its pack against its synth (the same notes) */
    { static const int L[][3] = {{I_PIANO, 57, 66}, {I_BASS, 36, 96}, {I_GUITAR, 50, 54}, {I_DRUMS, D_RIDE, 92}, {I_DRUMS, D_SNARE, 50}, {I_DRUMS, D_KICK, 80}, {I_DRUMS, D_HAT, 80}, {I_DRUMS, D_CRASH, 90}, {I_DRUMS, D_STICK, 70},
                                     {I_DRUMS, D_TOMH, 80}, {I_DRUMS, D_TOML, 80}, {I_DRUMS, D_SHAKER, 90}, {I_DRUMS, D_CONGA, 90}};
      static const char *LN[] = {"piano", "bass", "guitar", "ride", "snare", "kick", "hi-hat", "crash", "cross-stick", "high tom", "low tom", "shaker", "conga"};
      for (int t = 0; t < 13; t++) {
          double rp, rs; int inst = L[t][0];
          voice_alone(e, inst, L[t][1], L[t][2], &rp, 0);
          const jbpack_t *keep = PKS.ok[inst]; PKS.ok[inst] = 0;            /* 1.5: the synth only plays without a pack */
          voice_alone(e, inst, L[t][1], L[t][2], &rs, 0);
          PKS.ok[inst] = keep;
          double db = 20 * log10(rp / rs);
          CHECK(fabs(db) < (t < 9 ? 2.5 : 4.0), "%s: pack %+.1f dB against the stand-in synth", LN[t], db);
      } }

    /* jbnylon: only Joao Gilberto plays it; in tune; level near the jbguitar pack */
    { double rn, rg, f; int gs = e->sty[I_GUITAR];
      e->sty[I_GUITAR] = GT_GILBERTO;
      voice_alone(e, I_GUITAR, 47, 54, &rn, &f);
      int on_nylon = starts_in(e, I_GUITAR, 47, 54, P[4]);
      double want = 440 * pow(2, (47 - 69) / 12.0), cents = 1200 * log2(f / want); if (cents > 600) cents -= 1200; if (cents < -600) cents += 1200;
      CHECK(on_nylon && fabs(cents) < 25, "Joao Gilberto plays jbnylon: note 47 at %.1f Hz (%+.0f cents)", f, cents);
      e->sty[I_GUITAR] = GT_FREDDIE; voice_alone(e, I_GUITAR, 47, 54, &rg, 0);
      int on_g = starts_in(e, I_GUITAR, 47, 54, P[3]);
      CHECK(on_g, "every other guitarist keeps jbguitar");
      CHECK(fabs(20 * log10(rn / rg)) < 3.0, "jbnylon %+.1f dB against jbguitar", 20 * log10(rn / rg));
      const jbpack_t *keep = PKS.ok[PK_NYLON]; PKS.ok[PK_NYLON] = 0; e->sty[I_GUITAR] = GT_GILBERTO; voice_alone(e, I_GUITAR, 47, 54, &rg, 0);
      on_g = starts_in(e, I_GUITAR, 47, 54, P[3]);
      CHECK(on_g, "without jbnylon, Gilberto plays jbguitar");
      PKS.ok[PK_NYLON] = keep; e->sty[I_GUITAR] = (uint8_t)gs; }

    /* band demos with the packs; no clipping */
    static const char *WALTZ = "T34 *A Bb^7 | D7 | Eb^7 | G7 | C-7 | G7 | C-7 | F7 | *B F-7 | Bb7 | Eb^7 | Eb-6 | D-7 | G7 C-7 | F7 | Bb^7 F7 |";
    /* players: drums, bass, piano, guitar; feel 0 swing, 1 ballad, 2 bossa */
    struct { const char *file; int p[4], ballad, bpm, bars, waltz; } D[] = {
        {"out/packs_swing.wav", {0, 0, 0, 0}, 0, 160, 16, 0}, {"out/packs_ballad.wav", {0, 0, 0, 0}, 1, 72, 8, 0},
        {"out/packs_waltz.wav", {1, 1, 1, 1}, 0, 168, 12, 1}, {"out/packs_lineup3.wav", {2, 2, 2, 2}, 0, 184, 16, 0},
        {"out/packs_quartet.wav", {4, 4, 4, 4}, 0, 160, 12, 0},
        {"out/packs_roach.wav", {DR_ROACH, BS_PETTIFORD, PN_SILVER, GT_KESSEL}, 0, 196, 16, 0},
        {"out/packs_tony.wav", {DR_TONY, BS_CARTER, PN_HERBIE, GT_GRANT}, 0, 208, 16, 0},
        {"out/packs_basie.wav", {DR_JOJONES, BS_PAGE, PN_BASIE, GT_FREDDIE}, 0, 150, 16, 0},
        {"out/packs_kelly.wav", {DR_HIGGINS, BS_SAMJONES, PN_KELLY, GT_ELLIS}, 0, 172, 16, 0},
        {"out/packs_haynes.wav", {DR_HAYNES, BS_GARRISON, PN_GARNER, GT_CHRISTIAN}, 0, 176, 16, 0},
        {"out/packs_dejohnette.wav", {DR_DEJOHNETTE, BS_HADEN, PN_JAMAL, GT_HALL}, 1, 70, 8, 0},
        {"out/packs_bossa.wav", {DR_AIRTO, BS_HADEN, PN_JAMAL, GT_GILBERTO}, 2, 132, 16, 0},
        {"out/packs_bossa_cobb.wav", {0, 0, 0, 0}, 2, 140, 16, 0},
        {"out/packs_airto_swing.wav", {DR_AIRTO, BS_BROWN, PN_GARLAND, GT_WES}, 0, 168, 12, 0}};
    for (int d = 0; d < (int)(sizeof D / sizeof D[0]); d++) {
        FX_jazzband.init(2);
        for (int i = 0; i < 4; i++) host_sw[2][i] = LVL_DEFAULT[i];
        int st = D[d].p[0];
        if (D[d].waltz) { int n = 0; while (WALTZ[n]) n++; load_chart(e, WALTZ, n); }
        bc->len = (int32_t)((2646000LL * 16384) / D[d].bpm); bc->next = 0;
        run(1, 0, 0); e->ballad = D[d].ballad == 1; e->bossa = D[d].ballad == 2;   /* first block (baselines), then lineup, feel, red push */
        for (int i = 0; i < 4; i++) e->sty[i] = (uint8_t)D[d].p[i];
        knobs[7] = (int16_t)(knobs[7] ^ (int16_t)0x8000);
        FILE *w = wav_open(D[d].file); long frames = 0;
        long n = (long)(((D[d].waltz ? 3 : 4) * 60.0 / D[d].bpm * 44100 / 64) * (D[d].bars + 1.5));
        int peak = 0; long clip = 0; double en = 0; int maxv = 0;
        for (long b = 0; b < n; b++) {
            int16_t l[64] = {0}, r[64] = {0};
            FX_jazzband.render_stereo(2, l, r, knobs);
            bc->next -= OP1_FX_BLOCK << 14; if (bc->next < 0) bc->next += bc->len;
            int act = 0; for (int i = 0; i < JB_VOICES; i++) act += e->v[i].active && e->v[i].type == VT_PK; if (act > maxv) maxv = act;
            for (int k = 0; k < 64; k++) { int a = abs(l[k]) > abs(r[k]) ? abs(l[k]) : abs(r[k]); if (a > peak) peak = a; if (a >= 32767) clip++;
                en += (double)l[k] * l[k] + (double)r[k] * r[k]; int16_t f[2] = {l[k], r[k]}; fwrite(f, 2, 2, w); frames++; }
        }
        wav_close(w, frames);
        stop(e); run(10, 0, 0);
        double rms = sqrt(en / (2.0 * frames)) / 32768;
        CHECK(clip == 0 && rms > 0.02 && maxv > 3, "%s: %s %s, players %d, rms %.3f, peak %d, up to %d pack voices, no clipping",
              D[d].file, D[d].waltz ? "waltz" : D[d].ballad == 2 ? "bossa" : "swing", D[d].ballad == 1 ? "ballad" : "", st + 1, rms, peak, maxv);
    }
    /* the players screen names the sound pack */
    { FILE *fr = fopen("out/packs_frames.txt", "w"); e->screen = SCR_SETUP; e->scat = SC_BAND; e->srow = 2; host_lines_n = 0; SCREEN_jazzband.update(0, 16);
      fprintf(fr, "F setup band with packs\n");
      for (int i = 0; i < host_lines_n && i < 4096; i++) { host_line_t *l = &host_line_log[i]; fprintf(fr, "%d %d %d %d %d %08x\n", l->x0, l->y0, l->x1, l->y1, l->w, l->rgba); }
      e->screen = SCR_PLAY; fclose(fr);
      CHECK(host_missing_glyphs == 0, "players screen with packs: font glyphs only"); }
    for (int i = 0; i < 5; i++) free(P[i]);
    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
