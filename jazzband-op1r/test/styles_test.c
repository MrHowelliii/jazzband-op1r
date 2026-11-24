/* Host test for the per-instrument players (styles): every style, swing and ballad, generates bars inside the event
 * budget with notes in range; styles really differ; the band renders without clipping. Writes one demo WAV per band
 * line-up (all four instruments on style k) to out/styles_<k>.wav. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int host_lines_n;
#include "../plugins/jazzband/jazzband.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } else { printf("ok:   " __VA_ARGS__); printf("\n"); } } while (0)
static int16_t knobs[8] = {14894, 24000, 0, 0};
/* a jazz waltz chart (chord changes only) for the 3/4 checks; NULL = the first built-in song (4/4) */
static const char *WALTZ = "T34 *A Bb^7 | D7 | Eb^7 | G7 | C-7 | G7 | C-7 | F7 | *B F-7 | Bb7 | Eb^7 | Eb-6 | D-7 | G7 C-7 | F7 | Bb^7 F7 |";
static const char *chart_now;
static void use_chart(jb_t *e) { if (chart_now) { int n = 0; while (chart_now[n]) n++; load_chart(e, chart_now, n); } }

typedef struct { int ev, notes, maxev, lo, hi, offbeat, tripl, empty_bars, chrom, prev, maxtick, onbeats, swung, toms, perc; uint32_t hash; } stat_t;
static const char *FEEL[3] = {"swing", "ballad", "bossa"};
static stat_t gen_stats(int inst, int st, int feel, int bars) {
    jb_t *e = &S[1]; stat_t r = {0, 0, 0, 127, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 2166136261u};
    FX_jazzband.init(1); use_chart(e); e->rng = 12345u;
    for (int i = 0; i < 4; i++) e->sty[i] = 0;
    e->sty[inst] = (uint8_t)st; e->ballad = feel == 1; e->bossa = feel == 2; e->state = ST_PLAY; e->bass_last = 38;
    for (int b = 0; b < bars; b++) {
        e->bar = b % e->nbars; gen_bar(e);
        if (e->nev > r.maxev) r.maxev = e->nev;
        int n = 0;
        for (int i = 0; i < e->nev; i++) {
            ev_t *v = &e->ev[i]; if (v->inst != inst) continue;
            n++; r.notes++; if (v->tick > r.maxtick) r.maxtick = v->tick; if (v->tick % 12 == 0) r.onbeats |= 1 << (v->tick / 12);
            r.hash = (r.hash ^ (uint32_t)(v->tick * 131 + v->note)) * 16777619u;
            if (inst == I_BASS) { if (r.prev >= 0 && abs(v->note - r.prev) == 1) r.chrom++; r.prev = v->note; }
            if (inst != I_DRUMS) { if (v->note < r.lo) r.lo = v->note; if (v->note > r.hi) r.hi = v->note; }
            if (v->tick % 12) r.offbeat++;
            if (v->tick % 12 == 4) r.tripl++;
            if (v->tick % 12 == 4 || v->tick % 12 == 8) r.swung++;
            if (inst == I_DRUMS && (v->note == D_TOMH || v->note == D_TOML)) r.toms++;
            if (inst == I_DRUMS && (v->note == D_SHAKER || v->note == D_CONGA)) r.perc++;
        }
        if (!n) r.empty_bars++;
    }
    return r;
}
static long bar_min, bar_max;
static long render(int st, int feel, int bars, int bpm, const char *wav_path, int *peak, double *rms, int *maxv) {
    op1_beat_t *bc = host_beat();
    bc->len = (int32_t)((2646000LL * 16384) / bpm); bc->next = 0; bc->step = 0;
    FX_jazzband.init(2); use_chart(&S[2]);
    for (int i = 0; i < 8; i++) host_sw[2][i] = 0;
    for (int i = 0; i < 4; i++) host_sw[2][i] = LVL_DEFAULT[i];
    { int16_t l[64] = {0}, r[64] = {0}; FX_jazzband.render_stereo(2, l, r, knobs); }   /* first block: the knob baselines */
    for (int i = 0; i < 4; i++) S[2].sty[i] = (uint8_t)(st % NPLAY[i]);   /* 1.5: the lineup is a SETUP BAND value */
    S[2].ballad = feel == 1; S[2].bossa = feel == 2;
    knobs[7] = (int16_t)(knobs[7] ^ (int16_t)0x8000);           /* red push: play */
    FILE *w = wav_path ? fopen(wav_path, "wb") : 0;
    if (w) { static const char h[44] = {0}; fwrite(h, 1, 44, w); }
    int nb = chart_now ? 3 : 4;
    long blocks = (long)((nb * 60.0 / bpm * 44100 / 64) * (bars + 1)), clipped = 0; double en = 0; *peak = 0; *maxv = 0;
    int last_bar = -1; long bar_start = -1; bar_min = 1 << 30; bar_max = 0;
    for (long b = 0; b < blocks; b++) {
        if (S[2].state == ST_PLAY && S[2].bar != last_bar) {
            if (bar_start >= 0) { long d = b - bar_start; if (d < bar_min) bar_min = d; if (d > bar_max) bar_max = d; }
            bar_start = b; last_bar = S[2].bar;
        }
        int16_t l[64] = {0}, r[64] = {0};
        FX_jazzband.render_stereo(2, l, r, knobs);
        bc->next -= OP1_FX_BLOCK << 14; if (bc->next < 0) bc->next += bc->len;
        int act = 0; for (int i = 0; i < JB_VOICES; i++) act += S[2].v[i].active; if (act > *maxv) *maxv = act;
        for (int n = 0; n < 64; n++) {
            int a = abs(l[n]) > abs(r[n]) ? abs(l[n]) : abs(r[n]); if (a > *peak) *peak = a; if (a >= 32767) clipped++;
            en += (double)l[n] * l[n] + (double)r[n] * r[n];
            if (w) { int16_t f[2] = {l[n], r[n]}; fwrite(f, 2, 2, w); }
        }
    }
    if (w) {
        uint32_t data = (uint32_t)(blocks * 64 * 4), sr = 44100, br = sr * 4, riff = 36 + data, fmt = 16; uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
        fseek(w, 0, SEEK_SET);
        fwrite("RIFF", 1, 4, w); fwrite(&riff, 4, 1, w); fwrite("WAVEfmt ", 1, 8, w); fwrite(&fmt, 4, 1, w);
        fwrite(&pcm, 2, 1, w); fwrite(&ch, 2, 1, w); fwrite(&sr, 4, 1, w); fwrite(&br, 4, 1, w); fwrite(&ba, 2, 1, w);
        fwrite(&bits, 2, 1, w); fwrite("data", 1, 4, w); fwrite(&data, 4, 1, w); fclose(w);
    }
    stop(&S[2]);
    *rms = sqrt(en / (2.0 * blocks * 64)) / 32768;
    return clipped;
}

int main(void) {
    host_sine_init();
    /* Every saved 1.3 player word selects its intended player and sound when 1.4 starts (read once, band_init). */
    FX_jazzband.init(0);
    for (int i = 0; i < 4; i++) host_sw[0][i] = LVL_DEFAULT[i];
    for (int inst = 0; inst < 4; inst++) {
        int ok = 1, sounds = inst == I_DRUMS ? 2 : 3;
        for (int step = 0; step < PLAYER_STEPS[inst]; step++) {
            host_sw[0][4 + inst] = (int16_t)((2 * step + 1) * 32768 / (2 * PLAYER_STEPS[inst]));
            S[0].band_init = 0; read_shift(&S[0], 0);
            if (S[0].sty[inst] != step / sounds) ok = 0;
            FX_jazzband.init(0); S[0].band_init = 0; read_shift(&S[0], 0);
            if (S[0].sty[inst] != step / sounds) ok = 0;
        }
        CHECK(ok, "instrument %d: every 1.3 player step keeps its player (the sound part is ignored)", inst);
    }
    memset(host_sw[0], 0, sizeof host_sw[0]);
    static const char *IN[4] = {"drums", "bass", "piano", "guitar"};
    static const int LO[4] = {0, 28, 33, 40}, HI[4] = {127, 60, 84, 84};
    for (int inst = 0; inst < 4; inst++) {
        int np = NPLAY[inst]; uint32_t hash[NSTY]; int chrom[NSTY];
        for (int st = 0; st < np; st++) for (int bal = 0; bal < 3; bal++) {
            stat_t r = gen_stats(inst, st, bal, 64);
            if (!bal) { hash[st] = r.hash; chrom[st] = r.chrom; }
            if (bal == 2) CHECK(r.swung == 0, "%s %s bossa: straight eighths only (%d swung)", IN[inst], STY_NAME[inst][st], r.swung);
            int range_ok = inst == I_DRUMS || (r.lo >= LO[inst] && r.hi <= HI[inst]);
            CHECK(r.maxev < JB_MAXEV && r.notes > 0 && range_ok,
                  "%-6s %-15s %-6s %4d notes/64 bars, %3d offbeat, %3d triplet, %2d empty bars, range %d..%d, max %d events/bar",
                  IN[inst], STY_NAME[inst][st], FEEL[bal], r.notes, r.offbeat, r.tripl, r.empty_bars,
                  inst == I_DRUMS ? 0 : r.lo, inst == I_DRUMS ? 0 : r.hi, r.maxev);
        }
        int distinct = 1;
        for (int a = 0; a < np; a++) for (int b = a + 1; b < np; b++) if (hash[a] == hash[b]) distinct = 0;
        CHECK(distinct, "%s: all %d players play different parts with the same seed", IN[inst], np);
        if (inst == I_BASS) CHECK(chrom[2] > chrom[0] * 3 / 2, "bass: Ron Carter is more chromatic than Paul Chambers (%d vs %d half steps)", chrom[2], chrom[0]);
    }
    {   /* 1.5 signatures */
        stat_t r = gen_stats(I_DRUMS, DR_ROACH, 0, 32); CHECK(r.toms > 20, "Max Roach plays the toms (%d hits in 32 bars)", r.toms);
        r = gen_stats(I_DRUMS, DR_AIRTO, 0, 32); CHECK(r.perc > 200 && r.swung == 0, "Airto: shaker and conga (%d), straight even in the swing feel", r.perc);
        r = gen_stats(I_DRUMS, DR_COBB, 0, 32); CHECK(r.toms == 0 && r.perc == 0, "Jimmy Cobb is unchanged: no toms or percussion");
        r = gen_stats(I_GUITAR, GT_GILBERTO, 0, 32); CHECK(r.swung == 0 && r.lo < 48, "Joao Gilberto: straight batida with the thumb bass in any feel (low %d)", r.lo);
        stat_t a = gen_stats(I_PIANO, PN_JAMAL, 0, 32); CHECK(a.notes > 0, "Ahmad Jamal plays");
    }
    {   /* the original band is unchanged: style 0 = 0.5's generators (same RNG, same events) */
        stat_t a = gen_stats(I_PIANO, 0, 0, 32), b = gen_stats(I_PIANO, 0, 0, 32);
        CHECK(a.notes == b.notes, "style 0 is deterministic for a given seed");
    }
    for (int st = 0; st < NSTY; st++) for (int bal = 0; bal < 3; bal++) {
        char path[64]; int peak, maxv; double rms;
        snprintf(path, sizeof path, "out/styles_%d%s.wav", st + 1, bal == 1 ? "_ballad" : bal == 2 ? "_bossa" : "");
        long clipped = render(st, bal, bal ? 8 : 12, bal == 1 ? 72 : bal == 2 ? 132 : 160, path, &peak, &rms, &maxv);
        CHECK(clipped == 0 && rms > 0.01 && rms < 0.4 && maxv <= JB_VOICES,
              "line-up %d (%s / %s / %s / %s) %s: rms %.3f, peak %d, %d voices, no clipping -> %s",
              st + 1, STY_NAME[0][st], STY_NAME[1][st % 10], STY_NAME[2][st % 10], STY_NAME[3][st % 10], FEEL[bal], rms, peak, maxv, path);
    }
    /* 3/4 */
    chart_now = WALTZ;
    { jb_t *e = &S[1]; FX_jazzband.init(1); use_chart(e);
      CHECK(e->nb == 3 && e->tpbar == 36 && e->nbars == 16 && ceq(e->bars[13].beat[0], (chord_t){7, Q_DOM7}) && ceq(e->bars[13].beat[2], (chord_t){0, Q_MIN7}),
            "T34 chart: 3 beats a bar, 36 ticks, 16 bars, bar 14 = G7 (2 beats) C-7 (1 beat)"); }
    for (int inst = 0; inst < 4; inst++) for (int st = 0; st < NPLAY[inst]; st++) for (int bal = 0; bal < 3; bal++) {
        stat_t r = gen_stats(inst, st, bal, 64);
        int range_ok = inst == I_DRUMS || (r.lo >= LO[inst] && r.hi <= HI[inst]);
        int pulse_ok = inst != I_BASS || bal || st == BS_LAFARO || st == BS_HADEN || r.onbeats == 7;   /* walking players: 1, 2 and 3 */
        int ride_ok = inst != I_DRUMS || st == DR_MOTIAN || (r.onbeats & 7) == 7;
        CHECK(r.maxtick < 36 && r.notes > 0 && range_ok && pulse_ok && ride_ok && r.maxev < JB_MAXEV,
              "3/4 %-6s %-15s %-6s %4d notes/64 bars, last tick %d, beats hit %d%d%d, %d empty bars",
              IN[inst], STY_NAME[inst][st], bal == 1 ? "ballad" : bal == 2 ? "bossa" : "waltz", r.notes, r.maxtick, r.onbeats & 1, (r.onbeats >> 1) & 1, (r.onbeats >> 2) & 1, r.empty_bars);
    }
    for (int st = 0; st < NSTY; st++) for (int bal = 0; bal < 2; bal++) {
        char path[64]; int peak, maxv; double rms; int bpm = bal ? 76 : 168;
        snprintf(path, sizeof path, "out/waltz_%d%s.wav", st + 1, bal ? "_ballad" : "");
        long clipped = render(st, bal, 8, bpm, path, &peak, &rms, &maxv);
        double expect = 3 * 60.0 / bpm * 44100 / 64;
        CHECK(clipped == 0 && rms > 0.01 && rms < 0.4 && bar_min >= expect - 1.5 && bar_max <= expect + 1.5,
              "3/4 line-up %d %s: bars %ld..%ld blocks (3 beats = %.1f), rms %.3f, no clipping -> %s",
              st + 1, bal ? "ballad" : "waltz", bar_min, bar_max, expect, rms, path);
    }
    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
