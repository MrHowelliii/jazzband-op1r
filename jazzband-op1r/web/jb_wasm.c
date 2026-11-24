/* jazzband in the browser: the plugin source (plugins/jazzband/jazzband.c), compiled unchanged to WebAssembly with the
 * shim in web/op1_plugin.h, plus a small control surface for the companion's play-along. The band, the players, the
 * feels, loops, key cycles, intros, turnarounds, endings and the bar-line rules are the plugin's own code; this file
 * only does what the OP-1's knobs and FX table do there:
 *   - the songbook arrives as a jbsongs plugin in op1_fxs[] (the same text the companion sends over USB),
 *   - sound packs arrive as jbpack plugins in op1_fxs[] (the .op1p files the companion installs),
 *   - touch controls call the same functions the knob handlers call (setup_turn, setup_act, load_song, start, stop...).
 * Everything runs on the audio thread, between blocks, so no locking is needed.
 * Build: make web (clang --target=wasm32, no libc). Copyright (C) 2026 Brandon. GPL-3.0-or-later. */
#include "op1_plugin.h"
int host_lines_n;                                         /* seq_text.h (the OP-1 screen, unused here) counts lines */
#include "../plugins/jazzband/jazzband.c"

#define EXPORT __attribute__((visibility("default")))

#ifndef JBW_NATIVE
/* ---- freestanding runtime: the compiler may call these for struct copies and zeroing */
void *memset(void *d, int c, unsigned long n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned long n) { uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d; }
void *memmove(void *d, const void *s, unsigned long n) {
    uint8_t *p = d; const uint8_t *q = s;
    if (p < q) while (n--) *p++ = *q++; else { p += n; q += n; while (n--) *--p = *--q; }
    return d;
}

/* ---- a bump heap for sound packs (they stay for the life of the page) */
extern unsigned char __heap_base;
static uint32_t heap_top;
EXPORT void *jbw_alloc(uint32_t n) {
    if (!heap_top) heap_top = (uint32_t)(uintptr_t)&__heap_base;
    heap_top = (heap_top + 15u) & ~15u;
    uint32_t p = heap_top, end = heap_top + n, need = (end + 65535u) >> 16, have = (uint32_t)__builtin_wasm_memory_size(0);
    if (need > have && __builtin_wasm_memory_grow(0, need - have) < 0) return 0;
    heap_top = end;
    return (void *)(uintptr_t)p;
}
#else
static uint8_t native_heap[8u << 20]; static uint32_t native_top;   /* test/web_test.c: the same code on the PC */
EXPORT void *jbw_alloc(uint32_t n) { native_top = (native_top + 15u) & ~15u; if (native_top + n > sizeof native_heap) return 0; void *p = native_heap + native_top; native_top += n; return p; }
#endif

#define POS 2                                             /* the master FX position */
static jb_t *const E = &S[POS];
static int16_t P[8];                                      /* the four knob words and the push word (7) */
static float OUTL[OP1_FX_BLOCK * 32], OUTR[OP1_FX_BLOCK * 32];
static jbsongs_t BOOK;                                    /* op1_fxs[1]: the songbook */
#define FX_PACK0 2                                        /* op1_fxs[2..6]: sound packs, by instrument */

EXPORT int16_t *jbw_sine(void) { return host_sine; }
EXPORT float *jbw_out_l(void) { return OUTL; }
EXPORT float *jbw_out_r(void) { return OUTR; }
EXPORT char *jbw_book_text(void) { return BOOK.text; }
EXPORT int jbw_book_max(void) { return BOOK_MAX; }

EXPORT void jbw_init(void) {
    for (int i = 0; i < 8; i++) { P[i] = 0; host_sw[i] = 0; }
    P[1] = 26000;                                         /* green: band volume */
    for (int i = 0; i < 4; i++) host_sw[i] = LVL_DEFAULT[i];
    host_sw[7] = 1;                                       /* not all zero: the levels are read from words 0..3 */
    op1_fx_count = FX_PACK0 + PK_N;
    for (int i = 0; i < 64; i++) op1_fxs[i] = 0;
    FX_jazzband.init(POS);
}

/* ---- the notes the band plays, for MIDI out (the companion can send one part to the OP-1): inst, note, vel, dur
 * (ticks), and the block it starts in. A tick lasts at least 689 samples (320 BPM), so a block processes one at most:
 * the tick that played is the one the band was on before the block, and e->ev still holds its bar's notes. */
#define EVQ_MAX 192
static uint8_t EVQ[EVQ_MAX * 6]; static int evq_n, evq_ticks;
EXPORT uint8_t *jbw_events(void) { return EVQ; }
EXPORT int jbw_event_count(void) { return evq_n; }
EXPORT int jbw_tick_count(void) { return evq_ticks; }
EXPORT void jbw_events_clear(void) { evq_n = 0; evq_ticks = 0; }
static void note_events(int s0, int t0, int block) {
    int played;
    if (s0 >= ST_COUNT && E->tick != t0) played = t0;                /* the tick that played in this block */
    else if (s0 < ST_COUNT && E->state >= ST_COUNT && E->tick == 1) played = 0;   /* started in this block */
    else return;
    evq_ticks++;
    for (int i = 0; i < E->nev && evq_n < EVQ_MAX; i++) if (E->ev[i].tick == played) {
        uint8_t *q = &EVQ[evq_n++ * 6];
        q[0] = E->ev[i].inst; q[1] = E->ev[i].note; q[2] = E->ev[i].vel; q[3] = E->ev[i].dur; q[4] = (uint8_t)block; q[5] = 0;
    }
}

/* ---- audio: n blocks of 64 frames into OUTL / OUTR (float, -1..1) */
EXPORT int jbw_render(int nblocks) {
    if (nblocks > 32) nblocks = 32;
    for (int b = 0; b < nblocks; b++) {
        int16_t l[OP1_FX_BLOCK], r[OP1_FX_BLOCK];
        for (int n = 0; n < OP1_FX_BLOCK; n++) l[n] = r[n] = 0;
        int s0 = E->state, t0 = E->tick;
        FX_jazzband.render_stereo(POS, l, r, P);
        note_events(s0, t0, b);
        for (int n = 0; n < OP1_FX_BLOCK; n++) { OUTL[b * OP1_FX_BLOCK + n] = l[n] * (1.0f / 32768); OUTR[b * OP1_FX_BLOCK + n] = r[n] * (1.0f / 32768); }
    }
    return nblocks * OP1_FX_BLOCK;
}

/* ---- the songbook: TITLE;key;minor;feel;chart;tempo[;original] lines and @SET|n n n lines, as written to jbsongs.
 * Installed like a new jbsongs at boot: checked, indexed, every song's session settings cleared, the band stopped. */
static void book_loaded(void) {
    for (int i = 0; i < 130; i++) { SS[i].used = 0; TADJ[i] = 0; }
    stop(E);
    E->loaded = 0; E->pend = -1; E->set = E->s_set = -1; E->view = VIEW_ALL; E->row = 0;
    load_song(E, 0);
}
EXPORT int jbw_book_set(uint32_t len, uint32_t count) {
    if (len == 0 || len > BOOK_MAX) return -1;
    static const op1_fx_t blank;
    BOOK.fx = blank;
    for (int k = 0; k < 4; k++) BOOK.fx.press[k] = JBS_PRESS[k];
    BOOK.magic[0] = JBS_MAGIC0; BOOK.magic[1] = JBS_MAGIC1;
    uint32_t h = 2166136261u; for (uint32_t i = 0; i < len; i++) h = fnv(h, BOOK.text[i]);
    BOOK.len = len; BOOK.sum = h; BOOK.count = count;
    op1_fxs[1] = &BOOK.fx;
    B.usb_state = 0; B.ok = 0; B.n = 0; B.phase = 0; B.sb = 0; B.hash = 0; SETS.n = 0;
    for (int guard = 0; guard < 1000 && B.usb_state < 2; guard++) if (!usb_scan()) break;
    if (B.usb_state != 2) return -2;
    book_loaded();
    return B.n;
}

/* ---- sound packs: a jbpack_t (its op1_fx_t first) with its audio after it, copied into jbw_alloc() memory.
 * Returns the pack's instrument (0 drums, 1 bass, 2 piano, 3 guitar, 4 nylon) once its checksum is good, else -1. */
EXPORT int jbw_pack_add(const uint8_t *at, uint32_t len) {
    const jbpack_t *p = (const jbpack_t *)at;
    if (len < sizeof(jbpack_t) - sizeof p->zone || p->fx.press[0] != JBP_PRESS[0] || p->fx.press[1] != JBP_PRESS[1] ||
        p->fx.press[2] != JBP_PRESS[2] || p->fx.press[3] != JBP_PRESS[3] || p->magic[0] != JBP_MAGIC0 || p->magic[1] != JBP_MAGIC1 ||
        !pk_valid(p) || p->data_off + p->data_len > len) return -1;
    op1_fxs[FX_PACK0 + p->inst] = &p->fx;
    for (int i = 0; i < PK_N; i++) { PKS.found[i] = 0; PKS.ok[i] = 0; }
    PKS.state = 0; PKS.cur = -1; PKS.pos = PKS.total = 0;
    for (int guard = 0; guard < 4096 && PKS.state != 2; guard++) pk_scan();
    return PKS.ok[p->inst] == p ? (int)p->inst : -1;
}

/* ---- controls (each is what a knob does on the OP-1; see band_block) */
static int playing(void) { return E->state >= ST_COUNT; }
static void replan(void) { if (E->state == ST_PLAY) plan_next(E); }

EXPORT void jbw_play(void) { if (E->state == ST_STOP) { E->state = E->s_state = ST_ARMED; } }
EXPORT void jbw_finish(void) { if (E->state == ST_PLAY && E->kind == BK_CHART && !E->ending) { E->ending = 1; replan(); } }
EXPORT void jbw_stop(void) { if (E->state != ST_STOP) stop(E); }
EXPORT void jbw_transport(void) { P[7] = (int16_t)(P[7] ^ (int16_t)0x8000); }    /* the red press itself */

EXPORT void jbw_tempo(int b) {                            /* red turn on PLAY: the tempo at once, kept for the song */
    b = clampi(b, 40, 320);
    if (E->song >= 0 && E->song < 130) TADJ[E->song] = (int16_t)(b == song_bpm(E->song) ? 0 : b);
    set_bpm(E, b);
}
EXPORT void jbw_key(int want) {                           /* blue on PLAY: the actual key offset -5..+6 */
    want = wrapk(want);
    int actual = wrapk(E->key_off + E->shift);
    if (E->kstep != KS_OFF) { E->kstep = KS_OFF; E->kmsg = 1378; E->pass = 0; replan(); }
    if (!playing()) { E->key_off = want - E->shift; E->key_pend = 0; E->transpose = E->s_transpose = E->key_off; }
    else if (want == actual) E->key_pend = 0;
    else { E->key_pend = 1; E->key_tgt = want; }
}
EXPORT void jbw_volume(int q15) { P[1] = (int16_t)clampi(q15, 0, 32767); }
EXPORT void jbw_mute(int on) { op1_fx_page[POS][1] = (uint8_t)(on != 0); }
EXPORT void jbw_level(int inst, int q15) { if (inst >= 0 && inst < 4) host_sw[inst] = (int16_t)clampi(q15, 0, 32767); }

/* SETUP rows (R_*): read a value, set it (as red turns from where it is), or act (white press) */
EXPORT int jbw_get(int r) {
    switch (r) {
    case R_LOOP: return E->loop; case R_FROM: return E->lfrom; case R_TO: return E->lto; case R_KSTEP: return E->kstep;
    case R_CHORUS: return E->choruses; case R_FEEL: return E->ballad + 2 * E->bossa; case R_INTRO: return E->intro;
    case R_TURN: return E->turn; case R_END: return E->endtype;
    case R_DPLAY: case R_BPLAY: case R_PPLAY: case R_GPLAY: return E->sty[r - R_DPLAY];
    }
    return 0;
}
EXPORT void jbw_set(int r, int v) {
    int d = v - jbw_get(r);
    if (r == R_END) d = (v & 1) != E->endtype;
    if (d && setup_turn(E, r, d)) replan();
}
EXPORT void jbw_act(int r) { if (setup_act(E, r)) replan(); }

/* SONGS: choose a song (stopped: it loads, ready; playing: queued for the next bar line; the song that plays: cancels) */
EXPORT void jbw_song(int idx, int set, int spos) {
    if (idx < 0 || idx >= nsongs()) return;
    if (!playing()) { if (idx != E->song) load_song(E, idx); E->set = set; E->spos = spos; }
    else if (idx != E->song) { E->pend = idx; E->pend_set = set; E->pend_pos = spos; E->jump = -1; E->bcur = -1; }
    else { E->pend = -1; E->set = set; E->spos = spos; }
    E->s_pend = E->pend;
}
EXPORT void jbw_cancel_queue(void) { E->pend = -1; E->s_pend = -1; }

/* PLAY bar actions: 0 START HERE (stopped), 1 JUMP at the next bar line (playing), 2 CANCEL a jump, 3 forget the start
 * bar, 4 set the start bar without starting (the companion's "start here next time") */
EXPORT int jbw_bar(int bar, int act) {
    if (bar < 0 || bar >= E->nbars) bar = 0;
    if (act == 0) { if (E->state != ST_STOP) return 0; E->sbar = bar; E->bcur = -1; E->state = E->s_state = ST_ARMED; return 1; }
    if (act == 1) {
        if (!((E->state == ST_PLAY || E->state == ST_COUNT) && !E->ending && (E->kind == BK_CHART || E->kind == BK_INTRO))) return 0;
        E->jump = bar; clear_pend(E); replan(); return 1;
    }
    if (act == 2) { E->bcur = -1; if (E->jump >= 0) { E->jump = -1; replan(); } return 1; }
    if (act == 4) { if (E->state != ST_STOP) return 0; E->sbar = bar; return 1; }
    E->sbar = -1; return 1;
}

/* ---- per-song session settings, so the companion can keep them between visits (the plugin cannot write files) */
EXPORT void jbw_keep(void) { save_song(E); }
EXPORT int jbw_ss_get(int i, uint8_t *o) {
    if (i < 0 || i >= 130 || !SS[i].used) return 0;
    const songset_t *x = &SS[i];
    o[0] = x->loop; o[1] = x->lfrom; o[2] = x->lto; o[3] = x->kstep; o[4] = x->choruses; o[5] = x->intro; o[6] = x->turn;
    o[7] = x->endtype; o[8] = x->ballad; o[9] = x->bossa; o[10] = (uint8_t)x->key;
    return 1;
}
EXPORT void jbw_ss_set(int i, const uint8_t *o) {
    if (i < 0 || i >= 130) return;
    songset_t *x = &SS[i];
    x->used = 1; x->loop = o[0]; x->lfrom = o[1]; x->lto = o[2]; x->kstep = o[3]; x->choruses = o[4]; x->intro = o[5]; x->turn = o[6];
    x->endtype = o[7]; x->ballad = o[8]; x->bossa = o[9]; x->key = (int8_t)o[10];
}
EXPORT int jbw_tadj(int i) { return i >= 0 && i < 130 ? TADJ[i] : 0; }
EXPORT void jbw_set_tadj(int i, int b) { if (i >= 0 && i < 130) TADJ[i] = (int16_t)(b >= 40 && b <= 320 ? b : 0); }
EXPORT void jbw_reload(void) { if (!playing()) { int s = E->song; E->loaded = 0; load_song(E, s); } }

/* ---- what the screen shows */
enum { W_STATE, W_BAR, W_TICK, W_BEAT, W_CHORUS, W_PASS, W_ENDING, W_KIND, W_SHIFT, W_KEYOFF, W_KPEND, W_KTGT, W_BPM, W_OBPM,
       W_SONG, W_PEND, W_LOOP, W_LFROM, W_LTO, W_KSTEP, W_CHORUSES, W_INTRO, W_TURN, W_END, W_FEEL, W_STY, W_NB = W_STY + 4,
       W_NBARS, W_JUMP, W_SBAR, W_INTROI, W_NSEC, W_TAGI, W_NXBAR, W_NXKIND, W_CUR, W_NEXT = W_CUR + 8, W_PKOK = W_NEXT + 2,
       W_PKFOUND, W_SBPM, W_KMSG, W_TPBAR, W_ACTUAL, W_NSONGS, W_VSEC, W_NXSEC, W_SPT, W_LOOPLO, W_LOOPHI, W_N };
static int32_t ST[W_N];
EXPORT int jbw_state_len(void) { return W_N; }
EXPORT int32_t *jbw_state(void) {
    jb_t *e = E;
    ST[W_STATE] = e->state; ST[W_BAR] = e->bar; ST[W_TICK] = e->tick; ST[W_BEAT] = e->tick / JB_TPB; ST[W_CHORUS] = e->chorus;
    ST[W_PASS] = e->pass; ST[W_ENDING] = e->ending; ST[W_KIND] = e->kind; ST[W_SHIFT] = e->shift; ST[W_KEYOFF] = e->key_off;
    ST[W_KPEND] = e->key_pend; ST[W_KTGT] = e->key_tgt; ST[W_BPM] = e->bpm; ST[W_OBPM] = e->orig_bpm; ST[W_SONG] = e->song;
    ST[W_PEND] = e->pend; ST[W_LOOP] = e->loop; ST[W_LFROM] = e->lfrom; ST[W_LTO] = e->lto; ST[W_KSTEP] = e->kstep;
    ST[W_CHORUSES] = e->choruses; ST[W_INTRO] = e->intro; ST[W_TURN] = e->turn; ST[W_END] = e->endtype;
    ST[W_FEEL] = e->ballad + 2 * e->bossa;
    for (int i = 0; i < 4; i++) ST[W_STY + i] = e->sty[i];
    ST[W_NB] = e->nb; ST[W_NBARS] = e->nbars; ST[W_JUMP] = e->jump; ST[W_SBAR] = e->sbar; ST[W_INTROI] = e->intro_i;
    ST[W_NSEC] = e->nsec; ST[W_TAGI] = e->tag_i; ST[W_NXBAR] = e->nx_bar; ST[W_NXKIND] = e->nx_kind;
    for (int k = 0; k < 4; k++) { chord_t c = cur_chord(e, k); ST[W_CUR + 2 * k] = c.root; ST[W_CUR + 2 * k + 1] = c.q; }
    { chord_t c = next_chord(e); ST[W_NEXT] = c.root; ST[W_NEXT + 1] = c.q; }
    int ok = 0, found = 0; for (int i = 0; i < PK_N; i++) { ok |= (PKS.ok[i] != 0) << i; found |= (op1_fxs[FX_PACK0 + i] != 0) << i; }
    ST[W_PKOK] = ok; ST[W_PKFOUND] = found; ST[W_SBPM] = song_bpm(e->song); ST[W_KMSG] = e->kmsg; ST[W_TPBAR] = e->tpbar;
    ST[W_ACTUAL] = wrapk(e->key_off + e->shift); ST[W_NSONGS] = nsongs(); ST[W_VSEC] = e->vb.section; ST[W_NXSEC] = e->vbn.section;
    ST[W_SPT] = e->spt; ST[W_LOOPLO] = looping(e) ? loop_lo(e) : -1; ST[W_LOOPHI] = looping(e) ? loop_hi(e) : -1;
    return ST;
}
/* the chart as written (before the key): per bar 4 x (root, quality), then the section letter (0: none) */
static int8_t BARS[JB_MAXBARS * 9];
EXPORT int8_t *jbw_bars(void) {
    for (int b = 0; b < E->nbars; b++) {
        for (int k = 0; k < 4; k++) { BARS[b * 9 + 2 * k] = E->bars[b].beat[k].root; BARS[b * 9 + 2 * k + 1] = (int8_t)E->bars[b].beat[k].q; }
        BARS[b * 9 + 8] = E->bars[b].section;
    }
    return BARS;
}
/* the section starts (LOOP: 1..nsec are these sections, nsec + 1 is BARS) */
EXPORT int jbw_section(int i) { return i >= 0 && i < E->nsec ? E->sec_at[i] | (E->sec_ch[i] << 8) : -1; }
