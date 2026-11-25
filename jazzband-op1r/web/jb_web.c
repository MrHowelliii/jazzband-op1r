/* jb_web.c - the Jazz Band plugin (plugins/jazzband/jazzband.c, unchanged code) built for the browser as WebAssembly,
 * plus a lead line: the melody of a lead sheet played on the band's own ticks, so lead and band never drift.
 *
 * Build (no libc, no Emscripten):  sh web/build.sh  ->  web/jazzband.wasm
 *
 * The page (an AudioWorklet) calls jbw_render() for 128 frames at a time at 44.1 kHz and reads two float buffers.
 * Songs come in as bars of per-beat chords (jbw_load) and a melody (jbw_lead_*). The OP-1 screen and knobs are not
 * used: the page sets the engine's fields (tempo, feel, players, loops, choruses...) directly. */
#define JB_WEB 1
#define JB_MAXBARS 512
#define JB_TICK_HOOK(e, bpt) web_tick((void *)(e), (bpt))
#define JB_TRIGGER_HOOK(e, ev, bpt) web_trigger((void *)(e), (const void *)(ev), (bpt))
static void web_tick(void *ev, int bpt_q8);
static void web_trigger(void *e, const void *ev, int bpt_q8);
#include "../plugins/jazzband/jazzband.c"

int host_lines_n;
#define EXPORT(name) __attribute__((export_name(#name)))

/* ------------------------------------------------------------------ memory for the sound packs (bump allocator) */
static uint8_t ARENA[44u << 20] __attribute__((aligned(16)));
static uint32_t arena_used;
EXPORT(jbw_alloc) uint8_t *jbw_alloc(uint32_t n) {
    n = (n + 15) & ~15u;
    if (arena_used + n > sizeof ARENA) return 0;
    uint8_t *p = ARENA + arena_used; arena_used += n; return p;
}
/* a pack the page copied into memory from jbw_alloc: register it like an installed op1r plugin, then rescan */
EXPORT(jbw_add_pack) int jbw_add_pack(const uint8_t *p) {
    if (op1_fx_count < 1) op1_fx_count = 1;
    if (op1_fx_count >= 64) return 0;
    op1_fxs[op1_fx_count++] = (const op1_fx_t *)p;
    for (int i = 0; i < PK_N; i++) { PKS.found[i] = 0; PKS.ok[i] = 0; }
    PKS.state = 0; PKS.cur = -1;
    return 1;
}
EXPORT(jbw_pack_ok) int jbw_pack_ok(int inst) { return inst >= 0 && inst < PK_N && PKS.ok[inst] != 0; }

/* ------------------------------------------------------------------ engine */
#define POS 2                                             /* the master FX position */
static int16_t knobs[8] = {14894, 30000, 0, 0, 0, 0, 0, 0};
static float OUTL[128], OUTR[128];
EXPORT(jbw_out_l) float *jbw_out_l(void) { return OUTL; }
EXPORT(jbw_out_r) float *jbw_out_r(void) { return OUTR; }
static jb_t *E(void) { return &S[POS]; }

EXPORT(jbw_init) void jbw_init(void) {
    host_sine_init();
    if (op1_fx_count < 1) op1_fx_count = 1;
    FX_jazzband.init(POS);
    for (int i = 0; i < 4; i++) host_sw[POS][i] = LVL_DEFAULT[i];
}

/* bars: 9 bytes each, written by the page at jbw_bars(): 4 x (root 0..11, quality Q_*), then a section letter or 0 */
static uint8_t BARBUF[JB_MAXBARS * 9];
EXPORT(jbw_bars) uint8_t *jbw_bars(void) { return BARBUF; }
EXPORT(jbw_load) void jbw_load(int nbars, int beats, int bpm, int feel) {
    jb_t *e = E();
    if (e->state != ST_STOP) stop(e);
    if (nbars < 1) nbars = 1;
    if (nbars > JB_MAXBARS) nbars = JB_MAXBARS;
    e->nbars = nbars; e->nb = beats == 3 ? 3 : 4; e->tpbar = e->nb * JB_TPB;
    for (int b = 0; b < nbars; b++) {
        const uint8_t *s = BARBUF + 9 * b;
        for (int k = 0; k < 4; k++) { e->bars[b].beat[k].root = (int8_t)(s[2 * k] % 12); e->bars[b].beat[k].q = s[2 * k + 1] % 9; }
        e->bars[b].section = (char)s[8];
    }
    e->song = 200; e->loaded = 0; e->book_gen = B.gen;          /* not a songbook song: nothing is saved per song */
    set_bpm(e, bpm < 40 ? 40 : bpm > 320 ? 320 : bpm);
    e->orig_bpm = e->bpm;
    e->nsec = 0;
    for (int b = 0; b < e->nbars && e->nsec < 16; b++)
        if (e->bars[b].section) { e->sec_at[e->nsec] = (uint8_t)b; e->sec_ch[e->nsec] = e->bars[b].section; e->nsec++; }
    e->loop = 0; e->lfrom = 0; e->lto = e->nbars - 1; e->kstep = KS_OFF; e->choruses = 0; e->intro = IN_COUNT;
    e->turn = TA_OFF; e->endtype = 0;
    e->ballad = feel == 1; e->bossa = feel == 2;
    e->shift = 0; e->pass = 0; e->chorus = 1; e->ending = 0; e->tag_i = 0; e->kind = BK_CHART;
    e->bcur = -1; e->sbar = -1; e->jump = -1; e->key_pend = 0; e->bm = 0;
    e->bar = 0; e->tick = 0;
    form_reset(e);
}
EXPORT(jbw_play) void jbw_play(int from_bar) {
    jb_t *e = E();
    e->sbar = from_bar >= 0 && from_bar < e->nbars ? from_bar : -1;
    if (e->state == ST_STOP) start(e);
}
EXPORT(jbw_stop) void jbw_stop(void) { jb_t *e = E(); if (e->state != ST_STOP) stop(e); }
EXPORT(jbw_finish) void jbw_finish(void) {              /* the musical ending after this chorus */
    jb_t *e = E();
    if (e->state == ST_PLAY && e->kind == BK_CHART && !e->ending) { e->ending = 1; plan_next(e); }
    else if (e->state != ST_STOP) stop(e);
}
EXPORT(jbw_tempo) void jbw_tempo(int bpm) { set_bpm(E(), bpm < 40 ? 40 : bpm > 320 ? 320 : bpm); }
EXPORT(jbw_feel) void jbw_feel(int feel) { jb_t *e = E(); e->ballad = feel == 1; e->bossa = feel == 2; }
EXPORT(jbw_transpose) void jbw_transpose(int semis) {
    jb_t *e = E(); semis = wrapk(semis);
    e->key_off = semis; e->transpose = e->s_transpose = semis; e->key_pend = 0;
    if (e->state == ST_PLAY) plan_next(e);
}
/* players: drums 0..11, bass 0..9, piano 0..9, guitar 0..9 (the SETUP BAND rows) */
EXPORT(jbw_player) void jbw_player(int inst, int who) {
    jb_t *e = E();
    if (inst < 0 || inst > 3) return;
    e->band_init = 1;
    e->sty[inst] = (uint8_t)(who < 0 ? 0 : who % NPLAY[inst]);
}
EXPORT(jbw_nplayers) int jbw_nplayers(int inst) { return inst >= 0 && inst < 4 ? NPLAY[inst] : 0; }
/* levels 0..32767 per instrument (0 mutes it) */
EXPORT(jbw_level) void jbw_level(int inst, int q15) {
    if (inst < 0 || inst > 3) return;
    host_sw[POS][inst] = (int16_t)(q15 < 1 ? 1 : q15 > 32767 ? 32767 : q15);   /* never all zero: that means defaults */
}
/* form: choruses (0 = until finish), ending (0 final chord, 1 tag), intro (0 count, 1 last 4 bars, 2 turnaround),
 * turnaround (0 off, 1 II-V, 2 III-VI-II-V), loop: from..to bars (both -1 = off), key step per pass */
EXPORT(jbw_form) void jbw_form(int choruses, int ending, int intro, int turn) {
    jb_t *e = E();
    e->choruses = choruses < 0 ? 0 : choruses; e->endtype = ending & 1; e->intro = intro % IN_N; e->turn = turn % TA_N;
    if (e->state == ST_PLAY) plan_next(e);
}
EXPORT(jbw_loop) void jbw_loop(int from, int to, int kstep) {
    jb_t *e = E();
    if (from < 0 || to < from || to >= e->nbars) { e->loop = 0; }
    else { e->loop = e->nsec + 1; e->lfrom = from; e->lto = to; }   /* loop = nsec + 1: the bar range */
    e->kstep = kstep % KS_N;
    if (e->state == ST_PLAY) plan_next(e);
}
/* where the band is: state | kind << 4 | beat << 8 | chorus << 16; bar separately */
EXPORT(jbw_state) int jbw_state(void) { jb_t *e = E(); return e->state | e->kind << 4 | (e->s_beat & 15) << 8 | (e->chorus & 255) << 16; }
EXPORT(jbw_bar) int jbw_bar(void) { return E()->bar; }
EXPORT(jbw_tick) int jbw_tick(void) { return E()->tick; }
EXPORT(jbw_shift) int jbw_shift(void) { return E()->shift; }

/* ------------------------------------------------------------------ the lead line */
typedef struct { uint16_t bar, t, d; uint8_t p, v; } lnote_t;   /* t, d: 48ths of a quarter note from the bar start */
#define LEAD_MAX 12000
static lnote_t LN[LEAD_MAX];
static int lead_n, lead_first[JB_MAXBARS + 1];
static lnote_t PICK[32]; static int pick_n;             /* the pickup: t from the start of the count-in bar */
static uint32_t lead_mask = 0xFFFFFFFFu;                /* bit c-1: the lead plays in chorus c (beyond 32: bit 31) */
static int lead_sound, lead_level = 26000, lead_oct;    /* sound: 0 synth (horn), 1 piano pack, 2 guitar pack, 3 vibes, 6 the lead pack (sax) */
static int human = 60;                                  /* 0 = exactly on the grid .. 100 = loose */
static uint32_t hrng = 0x1234567u;
static int hrand(int n) { hrng = hrng * 1664525u + 1013904223u; return n > 0 ? (int)((hrng >> 10) % (uint32_t)n) : 0; }
static int htri(int n) { return hrand(n + 1) + hrand(n + 1) - n; }      /* -n..n, mostly near 0 */
EXPORT(jbw_human) void jbw_human(int amount) { human = amount < 0 ? 0 : amount > 100 ? 100 : amount; }
EXPORT(jbw_lead_buf) lnote_t *jbw_lead_buf(void) { return LN; }
EXPORT(jbw_pick_buf) lnote_t *jbw_pick_buf(void) { return PICK; }
EXPORT(jbw_lead_max) int jbw_lead_max(void) { return LEAD_MAX; }
/* notes written at jbw_lead_buf, sorted by bar then t */
EXPORT(jbw_lead_set) void jbw_lead_set(int n, int npick) {
    lead_n = n < 0 ? 0 : n > LEAD_MAX ? LEAD_MAX : n; pick_n = npick < 0 ? 0 : npick > 32 ? 32 : npick;
    int k = 0;
    for (int b = 0; b <= JB_MAXBARS; b++) { while (k < lead_n && LN[k].bar < b) k++; lead_first[b] = k; }
}
EXPORT(jbw_lead_mask) void jbw_lead_mask(uint32_t m) { lead_mask = m; }
EXPORT(jbw_lead_sound) void jbw_lead_sound(int s, int level, int oct) { lead_sound = s < 0 || s > 6 ? 0 : s; lead_level = level; lead_oct = oct; }

/* A player's touch for the band. Each instrument sits in its own place around the beat (the ride and the bass on
 * top of it, the piano and the guitar a little behind), moves a few milliseconds from note to note, and never hits
 * two notes equally hard. A voice can only start later than its tick, so "ahead" means "delayed the least". */
static void web_trigger(void *e_, const void *ev_, int bpt_q8) {
    jb_t *e = (jb_t *)e_; const ev_t *ev = (const ev_t *)ev_;
    if (!human) { trigger(e, ev, bpt_q8); return; }
    static const int16_t BASE[4] = {0, 110, 380, 230}, VAR[4] = {70, 170, 330, 170}, VELJ[4] = {7, 6, 8, 6};   /* samples at 44.1 kHz */
    static int last_tick = -1, last_bar = -1, off[4];
    if (e->tick != last_tick || e->bar != last_bar) {       /* one placement per instrument per tick: chords stay together */
        last_tick = e->tick; last_bar = e->bar;
        for (int i = 0; i < 4; i++) off[i] = (BASE[i] + hrand(VAR[i])) * human / 60;
    }
    ev_t x = *ev;
    int inst = x.inst > 3 ? 3 : x.inst;
    int v = x.vel + htri(VELJ[inst]) * human / 60;
    x.vel = (uint8_t)(v < 12 ? 12 : v > 127 ? 127 : v);
    uint8_t a0[JB_VOICES]; int32_t g0[JB_VOICES]; const uint8_t *d0[JB_VOICES];
    for (int i = 0; i < JB_VOICES; i++) { a0[i] = e->v[i].active; g0[i] = e->v[i].g; d0[i] = e->v[i].pk_d; }
    trigger(e, &x, bpt_q8);
    int d = off[inst];
    if (inst == I_DRUMS) d = x.note == D_RIDE ? 0 : hrand(60) * human / 60;        /* the ride leads the band */
    if (inst == I_PIANO) d += hrand(90) * human / 60;                              /* fingers do not land as one */
    for (int i = 0; i < JB_VOICES; i++) {
        voice_t *vv = &e->v[i];
        if (vv->active && (!a0[i] || vv->g != g0[i] || vv->pk_d != d0[i])) vv->wait += d;
    }
}

/* synth lead voices: float, mono, panned slightly left */
#ifndef LEAD_PK_G
#define LEAD_PK_G 1600                                  /* a sustained note sits a little under the synth lead (web/test) */
#endif
typedef struct {
    int on, wait, len, age, rel;                          /* samples: before start, gate, since start; releasing */
    float f, ph, ph2, env, lp, bp, vel, vib, glide;
    int kind;
} lvoice_t;
#define NLV 4
static lvoice_t LV[NLV];
static float FREQ[128];
static uint32_t lrng = 12345;
static float lnoise(void) { lrng = lrng * 1664525u + 1013904223u; return (float)(int32_t)lrng * (1.0f / 2147483648.0f); }
static float fsin(float ph) {                             /* ph in turns */
    ph -= (float)(int)ph; if (ph < 0) ph += 1;
    float x = ph * 8192.0f; int i = (int)x; float fr = x - i;
    return (host_sine[i & 8191] + (host_sine[(i + 1) & 8191] - host_sine[i & 8191]) * fr) * (1.0f / 32767.0f);
}
static void lead_tables(void) {
    double f = 440.0, r = 1.0594630943592953;
    FREQ[69] = 440.0f;
    for (int n = 70; n < 128; n++) { f *= r; FREQ[n] = (float)f; }
    f = 440.0; for (int n = 68; n >= 0; n--) { f /= r; FREQ[n] = (float)f; }
}
static void lead_note_on(jb_t *e, int note, int vel, int wait, int len_samples, int bpt_q8, int dur_ticks) {
    if (note < 0) note = 0; if (note > 127) note = 127;
    if (lead_sound == 1 || lead_sound == 2) {             /* a band voice from the piano or guitar pack, at the lead level */
        int inst = lead_sound == 1 ? I_PIANO : I_GUITAR, li = inst == I_PIANO ? 2 : 3;
        int32_t keep = e->lvl[li];
        e->lvl[li] = (lead_level * 40960) >> 15;
        int sn = e->strum_n; e->strum_n = 0;
        ev_t ev = { 0, (uint8_t)inst, (uint8_t)note, (uint8_t)vel, (uint8_t)(dur_ticks < 1 ? 1 : dur_ticks > 255 ? 255 : dur_ticks) };
        int32_t g0[JB_VOICES]; const uint8_t *d0[JB_VOICES]; uint8_t a0[JB_VOICES];
        for (int i = 0; i < JB_VOICES; i++) { a0[i] = e->v[i].active; g0[i] = e->v[i].g; d0[i] = e->v[i].pk_d; }
        trigger(e, &ev, bpt_q8);
        for (int i = 0; i < JB_VOICES; i++) {            /* the new voice: start it inside the tick, in the centre */
            voice_t *v = &e->v[i];
            if (v->active && (!a0[i] || v->g != g0[i] || v->pk_d != d0[i])) { v->wait = wait; v->pl = v->pr = 23170; }
        }
        e->strum_n = sn; e->lvl[li] = keep;
        (void)len_samples;
        return;
    }
    if (lead_sound == 6) {                                 /* the recorded lead instrument (4 and 5 are left to the site's own build) */
        static voice_t *prev; static const uint8_t *prev_d;
        int vel2 = vel + htri(6) * human / 60; if (vel2 < 20) vel2 = 20; if (vel2 > 127) vel2 = 127;
        const jbzone_t *pz = pk_zone(PK_LEAD, note, vel2);
        if (pz) {
            if (prev && prev->active && prev->type == VT_PK && prev->pk_d == prev_d) { prev->len = 0; prev->rel = 29500; }   /* one line: the last note lets go */
            voice_t *pv = alloc_voice(e, JB_GTRV, JB_VOICES);
            for (int i = 0; i < 7; i++) { pv->ph[i] = 0; pv->inc[i] = 0; }
            for (int i = 0; i < 4; i++) { pv->pa[i] = 0; pv->pd[i] = 32767; }
            pv->hx = pv->hy = pv->hx2 = pv->hy2 = 0; pv->np = 0; pv->pitch = 0;
            pv->active = 1; pv->dec = 32767; pv->rel = 30600; pv->pl = 24300; pv->pr = 22000;
            int d = note - pz->key; if (d < -48) d = -48; if (d > 48) d = 48;
            const jbpack_t *pk = PKS.ok[PK_LEAD];
            pv->type = VT_PK; pv->pk_d = pk_data(pk) + pz->off; pv->pk_len = (int32_t)pz->len; pv->pk_pos = 0;
            pv->pk_codec = pz->codec; pv->pk_pred = pz->pred; pv->pk_idx = pz->idx;
            pv->pk_inc = ((JB_RATIO[d + 48] >> 2) * (pz->step >> 2)) >> 12;
            pv->pk_s0 = pk_next(pv); pv->pk_s1 = pk_next(pv); pv->pk_frac = 0;
            int dyn = 40 + ((vel2 * 88) >> 7);
            pv->g = mul30(dyn * G(LEAD_PK_G), (lead_level * 40960) >> 15); pv->gprev = 0;
            pv->len = len_samples / OP1_FX_BLOCK; if (pv->len < 6) pv->len = 6;
            pv->wait = wait + (150 + hrand(260)) * human / 60;        /* a horn lays back */
            prev = pv; prev_d = pv->pk_d;
            return;
        }
    }
    static int rr;
    lvoice_t *v = &LV[0];                                  /* horn: monophonic, the newest note takes the voice (legato) */
    if (lead_sound == 3) { rr = (rr + 1) % NLV; v = &LV[rr]; }   /* vibes: notes ring over each other */
    int legato = lead_sound == 0 && v->on && !v->rel && v->age > 0;
    v->kind = lead_sound;
    v->wait = wait; v->len = len_samples; v->rel = 0;
    v->vel = 0.35f + 0.65f * vel / 127.0f;
    if (legato && lead_sound == 0) { v->glide = v->f; }
    else { v->glide = FREQ[note]; v->age = 0; v->env = 0; v->ph = 0; v->ph2 = 0; v->lp = v->bp = 0; v->vib = 0; }
    v->f = FREQ[note];
    v->on = 1;
}
static void web_tick(void *ev_, int bpt_q8) {
    jb_t *e = (jb_t *)ev_;
    if (e != &S[POS] || lead_level <= 0) return;
    const lnote_t *a = 0; int n = 0;
    if (e->state == ST_COUNT) { a = PICK; n = pick_n; }
    else if (e->state == ST_PLAY && e->kind == BK_CHART && e->bar >= 0 && e->bar < e->nbars) {
        int c = e->chorus < 1 ? 1 : e->chorus > 32 ? 32 : e->chorus;
        if (!((lead_mask >> (c - 1)) & 1)) return;
        a = LN + lead_first[e->bar]; n = lead_first[e->bar + 1] - lead_first[e->bar];
    } else return;
    int lo = e->tick * 4, hi = lo + 4;
    int32_t spt_s = e->spt >> 14;                         /* samples per engine tick */
    for (int i = 0; i < n; i++) {
        if (a[i].t < lo || a[i].t >= hi) continue;
        int wait = (a[i].t - lo) * spt_s / 4;
        int len = (int)((int64_t)a[i].d * spt_s / 4);
        int note = a[i].p + e->transpose + e->shift + 12 * lead_oct;
        lead_note_on(e, note, a[i].v, wait, len, bpt_q8, (a[i].d + 2) / 4);
    }
}
static void lead_render(float *L, float *R, int frames) {
    float lvl = lead_level / 32768.0f;
    for (int vi = 0; vi < NLV; vi++) {
        lvoice_t *v = &LV[vi];
        if (!v->on) continue;
        for (int n = 0; n < frames; n++) {
            if (v->wait > 0) { v->wait--; continue; }
            float target = v->rel ? 0.0f : 1.0f;
            float s;
            if (v->kind == 3) {                           /* vibes: two-operator FM bell, motor tremolo */
                if (v->age == 0) v->env = 1.0f;
                v->env *= v->rel ? 0.99965f : 0.99996f;
                float mod = fsin(v->ph2) * 1.2f * v->env;
                s = fsin(v->ph + mod * 0.15f) * v->env * (0.8f + 0.2f * fsin(v->vib));
                v->ph += v->f / 44100.0f; v->ph2 += v->f * 4.0f / 44100.0f; v->vib += 5.5f / 44100.0f;
                if (v->ph > 1) v->ph -= 1; if (v->ph2 > 1) v->ph2 -= 1; if (v->vib > 1) v->vib -= 1;
                if (v->env < 0.0005f) { v->on = 0; break; }
            } else {                                      /* horn: band-limited saw through a resonant low-pass */
                float a = v->rel ? 0.0009f : (v->age < 1200 ? 0.0016f : 0.0004f);
                v->env += (target * (v->age < 9000 ? 1.0f : 0.82f) - v->env) * a;
                v->glide += (v->f - v->glide) * 0.004f;
                float vib = v->age > 11000 ? fsin(v->vib) * 0.006f * (v->age < 30000 ? (v->age - 11000) / 19000.0f : 1.0f) : 0.0f;
                v->vib += 5.2f / 44100.0f; if (v->vib > 1) v->vib -= 1;
                float inc = v->glide * (1.0f + vib) / 44100.0f;
                v->ph += inc; if (v->ph >= 1) v->ph -= 1;
                float t = v->ph, saw = 2 * t - 1;
                if (t < inc) { float x = t / inc; saw -= x + x - x * x - 1; }                 /* polyBLEP */
                else if (t > 1 - inc) { float x = (t - 1) / inc; saw -= x * x + x + x + 1; }
                float x = saw * 0.8f + lnoise() * 0.04f * v->env;
                float fc = (350.0f + v->glide * 1.6f) * (0.35f + 1.3f * v->env * v->vel);
                if (fc > 9000) fc = 9000;
                float g = fc * (6.2831853f / 44100.0f); if (g > 0.9f) g = 0.9f;
                v->lp += g * v->bp; float hp = x - v->lp - 0.9f * v->bp; v->bp += g * hp;   /* state-variable filter */
                s = v->lp * v->env * v->vel * 0.9f;
                if (v->rel && v->env < 0.0005f) { v->on = 0; break; }
            }
            v->age++;
            if (!v->rel && --v->len <= 0) v->rel = 1;
            L[n] += s * lvl * 0.62f; R[n] += s * lvl * 0.5f;
        }
    }
}

/* ------------------------------------------------------------------ audio */
EXPORT(jbw_render) void jbw_render(void) {              /* 128 frames into OUTL / OUTR, floats */
    static int tables;
    if (!tables) { lead_tables(); tables = 1; }
    for (int h = 0; h < 2; h++) {
        int16_t l[OP1_FX_BLOCK] = {0}, r[OP1_FX_BLOCK] = {0};
        FX_jazzband.render_stereo(POS, l, r, knobs);
        for (int n = 0; n < OP1_FX_BLOCK; n++) { OUTL[h * 64 + n] = l[n] * (1.0f / 32768.0f); OUTR[h * 64 + n] = r[n] * (1.0f / 32768.0f); }
    }
    lead_render(OUTL, OUTR, 128);
}

/* memcpy / memset for struct copies (no libc) */
void *memcpy(void *d, const void *s, unsigned long n) { uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, unsigned long n) { uint8_t *a = d; while (n--) *a++ = (uint8_t)c; return d; }
void *memmove(void *d, const void *s, unsigned long n) {
    uint8_t *a = d; const uint8_t *b = s;
    if (a < b) while (n--) *a++ = *b++; else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
