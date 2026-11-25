/* jazzband 1.5: a jazz backing band as an op1r master FX.
 *
 * Put it on the master FX (mixer). It adds a band to whatever you play: drums, bass, piano and guitar, at the song's
 * tempo (a songbook line without one: the OP-1 tempo). The screen shows the current chord big, the next chord, the beat, the bar, the key and the chart line.
 *
 *   blue  : key (a semitone a click); PRESS = key / tempo mode (tempo: 1 BPM a click)
 *   green : band volume; PRESS = band mute; with the song list open: the SETTINGS page of the song in use
 *           (loop, key each pass, choruses, turnaround, ending, tempo); on the settings page: close it
 *   white : play screen: turn = move through the bars (stopped: red starts there; playing: the band jumps there
 *           1.5 s after the last click, at a bar line); PRESS = the song list
 *           song list: turn = scroll (stopped, the song follows; playing, the band keeps going), PRESS = choose the highlighted song (playing: at the next bar line); with the list closed: open it (in a
 *           set list: on the next song of the set). Set lists from the companion: a list of sets first, then its songs
 *           songs: from jbsongs (sent over USB by the companion), else the slot 5 songbook, else 2 built-in songs
 *   red   : feel (left swing, right ballad); PRESS = play; playing: finish (ending), again: stop
 *   Shift + turn, layer 1 : levels: drums, bass, piano, guitar
 *   (1.4 / 1.5: the controls above are the 1.3 map; see README.md. Players are chosen in SETUP BAND: 12 drummers,
 *    10 bassists, 10 pianists, 10 guitarists (STY_NAME), samples only; FEEL is swing, ballad or bossa nova.)
 *
 * Sounds from the OP-1's own sampler: synth slots 6 / 7 / 8 hold piano / bass / guitar multisamples made by the
 * Jazz Band Mac app (several notes per slot, a small header with each note's pitch). An empty slot, or a sample
 * without that header, falls back to SYNTH A. Slot 5 can hold a songbook (the app writes song charts as sample
 * data); without one the built-in songs play.
 *
 * Integer only (the Blackfin has no FPU): every pitch / decay constant comes from jb_tables.h (gen_tables.py).
 * No division or modulo per sample; envelopes run per 64-sample block with per-sample linear ramps.
 * Copyright (C) 2026 Brandon. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#include "op1_plugin.h"
#include "jb_tables.h"

/* ------------------------------------------------------------------ data */

enum { Q_MAJ7, Q_MIN7, Q_DOM7, Q_HALFDIM, Q_DIM7, Q_MIN6, Q_MAJ6, Q_DOM7B9, Q_MINMAJ7, Q_NUM };
enum { I_DRUMS, I_BASS, I_PIANO, I_GUITAR };
enum { D_KICK, D_SNARE, D_RIDE, D_HAT, D_CRASH, D_STICK, D_TOMH, D_TOML, D_SHAKER, D_CONGA, D_N };
enum { VT_KICK, VT_SNARE, VT_RIDE, VT_HAT, VT_CRASH, VT_STICK, VT_BASS, VT_PIANO, VT_GUITAR, VT_SMP, VT_EP, VT_FMB, VT_PK };
enum { ST_STOP, ST_ARMED, ST_COUNT, ST_PLAY };
enum { SRC_SAMPLE, SRC_SYNTH_A, SRC_SYNTH_B };

#define JB_TPB      12            /* ticks per beat: triplet grid, the swung 8th is tick 8 */
#define JB_TPBAR    48
#ifndef JB_MAXBARS
#define JB_MAXBARS  128
#endif
#define JB_MAXEV    96
#define JB_VOICES   30
#define JB_GTRV     6             /* voices 0..5 are guitar strings (own delay lines) */
#define JB_KSLEN    1024
#define SLOT_BOOK   4             /* OP-1 synth slot 5 */
#define JB_BROWSE_BLOCKS 2756     /* the song list stays up 4 s after the last white click */
#define JB_SET_BLOCKS 8268        /* the settings page: 12 s */
#define JB_JUMP_BLOCKS 1034       /* playing: the band jumps to the bar cursor 1.5 s after the last white click */
static const int8_t SLOT_INST[3] = {5, 6, 7};   /* piano: slot 6, bass: slot 7, guitar: slot 8 (index = inst - 1) */
#define INST_IDX(inst) ((inst) == I_PIANO ? 0 : (inst) == I_BASS ? 1 : 2)

typedef struct { int8_t root; uint8_t q; } chord_t;
typedef struct { chord_t beat[4]; char section; } bar_t;
typedef struct { const char *title; int8_t key, minor; int16_t bpm; const char *chart; } song_t;

/* built-in songs (used when slot 5 has no songbook). Chord changes only; iReal Pro spellings. */
static const song_t SONGS[] = {
    { "AUTUMN LEAVES", 7, 1, 140,
      "*A C-7 | F7 | Bb^7 | Eb^7 | Ah7 | D7b9 | G-6 | G-6 |"
      "*A C-7 | F7 | Bb^7 | Eb^7 | Ah7 | D7b9 | G-6 | G-6 |"
      "*B Ah7 | D7b9 | G-6 | G-6 | C-7 | F7 | Bb^7 | Eb^7 |"
      "*C Ah7 | D7b9 | G-7 C7 | F-7 Bb7 | Ah7 | D7b9 | G-6 | G-6 |" },
    { "BLUES IN F", 5, 0, 120,
      "*A F7 | Bb7 | F7 | C-7 F7 | Bb7 | Bo7 | F7 | D7b9 | G-7 | C7 | F7 D7b9 | G-7 C7 |" },
};
#define JB_NSONGS ((int)(sizeof SONGS / sizeof SONGS[0]))

typedef struct { uint8_t tick, inst, note, vel, dur; } ev_t;

typedef struct {
    uint8_t active, type;
    int16_t dec, rel, pl, pr;          /* Q15: per-block decay, per-block release, pan gains */
    int32_t len;                       /* blocks until release */
    int32_t g;                         /* gain, Q30 */
    int32_t gprev;                     /* gain at the start of the block, Q15 */
    int32_t wait;                      /* samples before the voice starts (guitar strum) */
    uint32_t ph[7], inc[7];
    int32_t pa[4]; int16_t pd[4]; int8_t np;
    int32_t hx, hy, hx2, hy2, pitch;
    int16_t *ks; int16_t ksn, kspos, apc, ksfb; int32_t apx, apy;
    const int16_t *sd; int32_t spos, send, sgain; uint32_t sfrac, sinc;   /* sample playback */
    const uint8_t *pk_d; int32_t pk_pos, pk_len, pk_s0, pk_s1, pk_pred; uint32_t pk_frac, pk_inc; uint8_t pk_idx, pk_codec;   /* sound pack */
#ifdef JB_WEB
    int32_t pk_loop;                   /* > 0: at the end of the zone, carry on from this sample (a held lead note) */
#endif
} voice_t;

typedef struct {
    bar_t bars[JB_MAXBARS]; int nbars, song, transpose, ballad;
    int nb, tpbar;                     /* beats per bar (4, or 3 for a T34 chart) and ticks per bar */
    int state, bar, tick, latch_prev, book_gen, cursor, pend, white_prev, bpm;   /* bpm: the song's (0: the OP-1's) */
    int32_t blen;                      /* the song's beat length, 1/16384 samples */
    int32_t op1len; int op1bpm10, sync; /* the OP-1 clock's beat length, its tempo x 10; sync: it matches the song's */
    int view, row, kprev;              /* browser: VIEW_MENU (the set lists), VIEW_ALL (every song) or a set; cursor row; white step */
    int set, spos, pend_set, pend_pos; /* the set the song was chosen in (-1 none) and its place there; same for a queued song */   /* cursor: song under the white knob; pend: song to switch to at the bar line */
    volatile int browse;               /* blocks left with the song browser on screen */
    int32_t acc, spt, band_gain;       /* clock: 1/16384 samples; smoothed output gain: Q15 */
    ev_t ev[JB_MAXEV]; int nev;
    int bass_last, pv[4], pv_ok, skip_down, crash_next, strum_n, strum_sp, gv;
    uint8_t sty[4];                    /* drums, bass, piano, guitar: style 0..4 */
    int32_t lvl[4];                    /* drums, bass, piano, guitar: Q15 (32768 = 1.0) */
    uint8_t src[3];                    /* piano, bass, guitar: SRC_* asked for */
    uint8_t dsrc;                      /* drums: SRC_SAMPLE (the drum pack, if there is one) or SRC_SYNTH_A */
    uint32_t rng, nz;
    voice_t v[JB_VOICES];
    int16_t ks[JB_GTRV][JB_KSLEN];
    int32_t b1l, b1b, b2l, b2b, blp;   /* guitar body filters */
    volatile int s_state, s_bar, s_beat, s_transpose, s_song, s_volume, s_muted, s_cursor, s_pend, s_bpm, s_op1bpm10, s_sync,
                 s_view, s_row, s_set, s_spos, s_shift, s_kind, s_chorus, s_pass, s_ending;
    /* 1.2: blue key / tempo, settings page, loops, choruses, endings, turnarounds */
    int key_off, bprev, blue_prev, bmode;      /* blue: key offset (relative clicks), its last step, push flag, 0 key / 1 tempo */
    int orig_bpm;                              /* the chart's own tempo (songbook field 6, else 5) */
    int nsec; uint8_t sec_at[16]; char sec_ch[16];   /* sections: first bar and letter */
    int loop, lfrom, lto, kstep, choruses, turn, endtype, intro;   /* settings (see the settings page) */
    int intro_i;                               /* the intro bar playing (0..3) */
    int bcur, bidle, sbar, jump;               /* white on the play screen: bar cursor (-1 off), blocks since its last
                                                * click, the bar red starts from (-1: bar 1 / the loop), a jump queued */
    int kind, shift, chorus, pass, ending, tag_i;       /* where the band is: BK_*, key shift of this pass, counters */
    int nx_bar, nx_kind, nx_shift;             /* the bar planned next */
    bar_t vb, vbn;                             /* this bar and the next as played (shift, turnarounds, tag, final) */
    int sview, srow, sedit, sprev_white;       /* settings page: open (blocks left), row, editing */
    int green_prev, mute_inv;                  /* (1.2, unused since 1.4) */
    /* 1.4 navigation: three screens and a switcher; explicit actions only */
    int screen, sw, sw_row;                    /* SCR_*; the switcher open (blue press), its highlighted row */
    int bm, bm_row;                            /* PLAY: the bar action menu open, its row */
    int scat;                                  /* SETUP: -1 the category list, else the category open (srow: its row) */
    int red_prev, rprev;                       /* red push flag (bit 15 of word 7), red step (tempo / setup value) */
    int bidx_prev, kbase, key_pend, key_tgt;   /* blue: knob position (12), the position that means the actual key; a
                                                * key change queued for the next bar (key_tgt: actual offset -5..+6) */
    int kmsg;                                  /* blocks left showing KEY CYCLE OFF */
    int loaded, band_init;                     /* a song loaded (its settings may be saved); players read from Shift words */
    int bossa, ppat;                           /* 1.5: the BOSSA feel (ballad and bossa never both); piano: last pattern (vamps) */
} jb_t;
enum { BK_CHART, BK_TAG, BK_FINAL, BK_HOLD, BK_END, BK_INTRO };
enum { IN_COUNT, IN_LAST4, IN_TURN, IN_N };        /* intro: the count-in, the last 4 bars, I-VI-II-V twice */
enum { KS_OFF, KS_UP, KS_DOWN, KS_FOURTHS, KS_N };
enum { TA_OFF, TA_IIV, TA_IIIVIIIV, TA_N };
/* settings that belong to a song, kept per song for this session (the plugin cannot write files): key, practice and
 * arrangement. Cleared when a new songbook arrives (its song numbers mean other songs). */
typedef struct { uint8_t used, loop, lfrom, lto, kstep, choruses, intro, turn, endtype, ballad, bossa; int8_t key; } songset_t;
static songset_t SS[130];
enum { SCR_PLAY, SCR_SONGS, SCR_SETUP, SCR_N };
static int16_t TADJ[130];                     /* index: song (128 book songs at most, or the 2 built-in) */      /* tempo turned on the OP-1, per song (0: none); until the OP-1 restarts */
static jb_t S[3];

/* ------------------------------------------------------------- helpers */

static int pcof(int n) { n %= 12; return n < 0 ? n + 12 : n; }
static int iabs(int x) { return x < 0 ? -x : x; }
static int ceq(chord_t a, chord_t b) { return a.root == b.root && a.q == b.q; }
static uint32_t rnd(jb_t *e) { uint32_t x = e->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return e->rng = x; }
static int irand(jb_t *e, int n) { return (int)((rnd(e) >> 8) % (uint32_t)n); }
static int chance(jb_t *e, int permille) { return irand(e, 1000) < permille; }
static int32_t noise(jb_t *e) { e->nz = e->nz * 1664525u + 1013904223u; return (int32_t)e->nz >> 16; }
static int32_t mul30(int32_t g, int32_t f) { return ((g >> 15) * f) + (((g & 0x7FFF) * f) >> 15); }   /* Q30 x Q15 */

static int third(int q) { return (q == Q_MIN7 || q == Q_HALFDIM || q == Q_DIM7 || q == Q_MIN6 || q == Q_MINMAJ7) ? 3 : 4; }
static int fifth(int q) { return (q == Q_HALFDIM || q == Q_DIM7) ? 6 : 7; }
static int seventh(int q) {
    if (q == Q_MAJ7 || q == Q_MINMAJ7) return 11;
    if (q == Q_MIN6 || q == Q_MAJ6 || q == Q_DIM7) return 9;
    return 10;
}
static const int8_t SCALES[Q_NUM][7] = {
    {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 9, 10}, {0, 2, 4, 5, 7, 9, 10}, {0, 1, 3, 5, 6, 8, 10}, {0, 2, 3, 5, 6, 8, 9},
    {0, 2, 3, 5, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 11}, {0, 1, 4, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 11}};
/* rootless voicings: A form (3rd at the bottom), B form (7th at the bottom) */
static const int8_t VA[Q_NUM][4] = {{4, 7, 11, 14}, {3, 7, 10, 14}, {4, 9, 10, 14}, {3, 6, 10, 12}, {3, 6, 9, 12},
                                    {3, 7, 9, 14}, {4, 7, 9, 14}, {4, 9, 10, 13}, {3, 7, 11, 14}};
static const int8_t VB[Q_NUM][4] = {{11, 14, 16, 19}, {10, 14, 15, 19}, {10, 14, 16, 21}, {10, 12, 15, 18}, {9, 12, 15, 18},
                                    {9, 14, 15, 19}, {9, 14, 16, 19}, {10, 13, 16, 21}, {11, 14, 15, 19}};

static int in_scale(chord_t c, int n) { int iv = pcof(n - c.root); for (int i = 0; i < 7; i++) if (SCALES[c.q][i] == iv) return 1; return 0; }
static int scale_step(chord_t c, int from, int dir) { int n = from + dir; for (int k = 0; k < 4 && !in_scale(c, n); k++) n += dir; return n; }
static int nearest(int pc, int ref, int lo, int hi) {
    int best = lo, bd = 999;
    for (int n = lo; n <= hi; n++) if (pcof(n) == pc) { int d = iabs(n - ref); if (d < bd) { bd = d; best = n; } }
    return best;
}

/* ------------------------------------------------- data in sampler slots
 * The Mac app writes data as audio: 16 preamble samples (+A, -A, ...), then 4 samples per byte, 2 bits each (MSB
 * first) as one of four levels -A, -A/3, +A/3, +A. The levels are read against the preamble, so a level or
 * polarity change on the way does not matter. */
typedef struct { const int16_t *d; int32_t len, pos, t, amp; int pol; } jstream_t;

static int32_t slot_len(const uint8_t *sb) {       /* recorded length from the peak table (as granular) */
    const int16_t *p = op1_sample_peaks(sb);
    for (int i = OP1_PEAK_BUCKETS - 1; i >= 0; i--)
        if (p[i] > 96) { int32_t n = (i + 1) * 256; return n > OP1_SAMPLE_MAX ? OP1_SAMPLE_MAX : n; }
    return 0;
}
static uint32_t slot_hash(const uint8_t *sb) {
    const int16_t *p = op1_sample_peaks(sb); uint32_t h = 2166136261u;
    for (int i = 0; i < OP1_PEAK_BUCKETS; i++) h = (h ^ (uint16_t)p[i]) * 16777619u;
    return h;
}
static int js_open(jstream_t *s, const int16_t *d, int32_t len) {
    if (!d || len < 64) return 0;
    int32_t sum = 0, mn = 0x7FFF, mx = 0;
    int pol = d[0] > 0 ? 1 : -1;
    for (int i = 0; i < 16; i++) {
        int32_t x = d[i] * pol, a = (i & 1) ? -x : x;
        if (a < 2000) return 0;                       /* not alternating, or too quiet */
        sum += a; if (a < mn) mn = a; if (a > mx) mx = a;
    }
    if (mn < mx / 2) return 0;
    s->d = d; s->len = len; s->pos = 16; s->pol = pol; s->amp = sum >> 4; s->t = (s->amp * 2) / 3;
    return 1;
}
static int js_byte(jstream_t *s) {
    if (s->pos + 4 > s->len) return -1;
    int v = 0;
    for (int k = 0; k < 4; k++) {
        int32_t x = s->d[s->pos++] * s->pol;
        v = (v << 2) | (x < -s->t ? 0 : x < 0 ? 1 : x < s->t ? 2 : 3);
    }
    return v;
}
static uint32_t js_u32(jstream_t *s) { uint32_t v = 0; for (int k = 0; k < 4; k++) v |= (uint32_t)(js_byte(s) & 255) << (8 * k); return v; }
static uint32_t fnv(uint32_t h, int b) { return (h ^ (uint8_t)b) * 16777619u; }

/* instrument multisamples: "JBMS" v1, inst, zone count, per zone: root note, u32 start, u32 length; fnv32 */
typedef struct { uint8_t root; int32_t start, len; } zone_t;
typedef struct {
    const uint8_t *sb; uint32_t hash; volatile int ok; int nz; zone_t z[16];
    const int16_t *d; int32_t gain;
} islot_t;
static islot_t IS[3];

static void scan_inst(int k) {
    islot_t *is = &IS[k];
    const uint8_t *sb = op1_sample_buffer(SLOT_INST[k]);
    uint32_t h = sb ? slot_hash(sb) : 0;
    if (sb == is->sb && h == is->hash) return;
    is->ok = 0; is->sb = sb; is->hash = h;
    if (!sb) return;
    int32_t len = slot_len(sb);
    const int16_t *d = op1_sample_data(sb);
    jstream_t s;
    if (!js_open(&s, d, len)) return;
    uint32_t c = 2166136261u; int b[4];
    for (int i = 0; i < 4; i++) { b[i] = js_byte(&s); c = fnv(c, b[i]); }
    if (b[0] != 'J' || b[1] != 'B' || b[2] != 'M' || b[3] != 'S') return;
    int ver = js_byte(&s), inst = js_byte(&s), nz = js_byte(&s);
    c = fnv(fnv(fnv(c, ver), inst), nz);
    if (ver != 1 || nz < 1 || nz > 16) return;
    for (int i = 0; i < nz; i++) {
        int r = js_byte(&s); c = fnv(c, r);
        uint32_t st = 0, ln = 0;
        for (int q = 0; q < 4; q++) { int x = js_byte(&s); c = fnv(c, x); st |= (uint32_t)(x & 255) << (8 * q); }
        for (int q = 0; q < 4; q++) { int x = js_byte(&s); c = fnv(c, x); ln |= (uint32_t)(x & 255) << (8 * q); }
        if (r < 0 || r > 127 || st < 16 || ln < 64 || st + ln > (uint32_t)len) return;
        is->z[i].root = (uint8_t)r; is->z[i].start = (int32_t)st; is->z[i].len = (int32_t)ln;
    }
    if (js_u32(&s) != c) return;
    is->nz = nz; is->d = d;
    is->gain = (int32_t)((24576 << 15) / (s.amp > 1000 ? s.amp : 1000));   /* the app writes audio at the preamble's level */
    if (is->gain > 65536) is->gain = 65536;
    is->ok = 1;
}

#ifndef OP1_HOST
_Static_assert(sizeof(op1_fx_t) == 16, "jbsongs_t / jbpack_t: the Blackfin layout (16-byte op1_fx_t) the companion and the packer fill");
extern const op1_fx_t *op1_fxs[];       /* op1r core exports: every FX by number, built-in and plugins (0 = unused) */
extern int op1_fx_count;
#endif
#if defined(OP1_HOST) && !defined(JB_HOST_FXS)   /* op1r's own host harness: no FX table, so no jbsongs or packs */
static const op1_fx_t *op1_fxs[1];
static int op1_fx_count;
#endif
/* ---- sound packs: jbpiano, jbbass, jbguitar, jbdrums, jbnylon (plugins/jbpack/jbpack.h, kept identical here), data-only op1r
 * FX plugins the companion sends over USB. Found once at boot through op1_fxs[] (a signature in the knob press bytes,
 * then the magic), checked with FNV-1a (4 KB a block), then played in place: IMA ADPCM or mu-law zones. */
#define JBP_MAGIC0 0x4B50424Au
#define JBP_MAGIC1 0x31444E53u
#define JBP_MAXZ   160
#define PK_NYLON   4                   /* 1.5: jbnylon, a nylon-string guitar for Joao Gilberto (pack inst 4) */
#ifdef JB_WEB                          /* web build: one more pack (the lead instrument), 16-bit PCM zones, big packs */
#define PK_LEAD    5
#define PK_N       6
#define PK_MAXDATA (64u << 20)
#define PK_BUDGET  65536u
#define PK_CODECS  2
#else
#define PK_N       5
#define PK_MAXDATA (4u << 20)
#define PK_BUDGET  4096u
#define PK_CODECS  1
#endif
static const uint8_t JBP_PRESS[4] = { 0xCA, 0xC2, 0xD0, 0xCB };
typedef struct { uint8_t key, vlo, vhi, rr; uint32_t step; int16_t pred; uint8_t idx, codec; uint32_t off, len; } jbzone_t;
typedef struct {
    op1_fx_t fx; uint32_t magic[2]; uint32_t inst, nzones, data_off, data_len, sum, version; char credit[96];
    jbzone_t zone[JBP_MAXZ];
} jbpack_t;
_Static_assert(sizeof(jbzone_t) == 20, "jbzone_t: 20 bytes, as the packer writes it");
static struct {
    const jbpack_t *found[PK_N], *ok[PK_N];   /* by instrument (I_DRUMS..I_GUITAR, PK_NYLON): found, and checked */
    int state, cur; uint32_t pos, c, total; uint8_t rr[PK_N];
} PKS;
static const uint8_t *pk_data(const jbpack_t *p) { return (const uint8_t *)p + p->data_off; }
static int pk_valid(const jbpack_t *p) {                 /* header and zone bounds (before the checksum) */
    if (p->inst >= PK_N || p->nzones < 1 || p->nzones > JBP_MAXZ || p->data_len == 0 || p->data_len > PK_MAXDATA) return 0;
    for (uint32_t i = 0; i < p->nzones; i++) {
        const jbzone_t *z = &p->zone[i];
        uint32_t bytes = z->codec == 2 ? z->len * 2 : z->codec ? z->len : (z->len + 1) >> 1;
        if (z->len < 2 || z->codec > PK_CODECS || z->idx > 88 || z->step == 0 || z->step > (4u << 16) || z->vlo > z->vhi ||
            z->off > p->data_len || bytes > p->data_len - z->off) return 0;
    }
    return 1;
}
static void pk_scan(void) {                             /* audio thread, every block until done */
    if (PKS.state == 2) return;
    if (PKS.state == 0) {
        for (int i = 1; i < op1_fx_count && i < 64; i++) {
            const op1_fx_t *fx = op1_fxs[i];
            if (!fx || fx->press[0] != JBP_PRESS[0] || fx->press[1] != JBP_PRESS[1] || fx->press[2] != JBP_PRESS[2] ||
                fx->press[3] != JBP_PRESS[3]) continue;   /* only then read past its op1_fx_t */
            const jbpack_t *b = (const jbpack_t *)fx;
            if (b->magic[0] == JBP_MAGIC0 && b->magic[1] == JBP_MAGIC1 && pk_valid(b) && !PKS.found[b->inst]) PKS.found[b->inst] = b;
        }
        PKS.state = 1; PKS.cur = -1;
    }
    for (uint32_t budget = PK_BUDGET; budget;) {
        if (PKS.cur < 0 || PKS.pos >= PKS.total) {
            if (PKS.cur >= 0) { const jbpack_t *b = PKS.found[PKS.cur]; if (PKS.c == b->sum) PKS.ok[PKS.cur] = b; }
            do PKS.cur++; while (PKS.cur < PK_N && !PKS.found[PKS.cur]);
            if (PKS.cur >= PK_N) { PKS.state = 2; return; }
            const jbpack_t *b = PKS.found[PKS.cur];
            PKS.pos = 0; PKS.c = 2166136261u; PKS.total = b->nzones * (uint32_t)sizeof(jbzone_t) + b->data_len;
        }
        const jbpack_t *b = PKS.found[PKS.cur];
        uint32_t tbl = b->nzones * (uint32_t)sizeof(jbzone_t);
        const uint8_t *src = PKS.pos < tbl ? (const uint8_t *)b->zone + PKS.pos : pk_data(b) + (PKS.pos - tbl);
        uint32_t n = (PKS.pos < tbl ? tbl : PKS.total) - PKS.pos; if (n > budget) n = budget;
        for (uint32_t k = 0; k < n; k++) PKS.c = fnv(PKS.c, src[k]);
        PKS.pos += n; budget -= n;
    }
}
/* the zone for a note (pitched: the nearest root with this velocity) or a drum (its id); round robin over takes.
 * inst: the pack (I_DRUMS..I_GUITAR, or PK_NYLON) */
static const jbzone_t *pk_zone(int inst, int key, int vel) {
    const jbpack_t *p = PKS.ok[inst];
    if (!p) return 0;
    int bd = 999, root = -1;
    for (uint32_t i = 0; i < p->nzones; i++) {
        const jbzone_t *z = &p->zone[i];
        if (vel < z->vlo || vel > z->vhi) continue;
        int d = inst == I_DRUMS ? (z->key == key ? 0 : 999) : iabs(key - z->key);
        if (d < bd) { bd = d; root = z->key; }
    }
    if (root < 0 || bd > 12) return 0;
    int n = 0; for (uint32_t i = 0; i < p->nzones; i++) { const jbzone_t *z = &p->zone[i]; n += z->key == root && vel >= z->vlo && vel <= z->vhi; }
    int pick = PKS.rr[inst]++ % n;
    for (uint32_t i = 0; i < p->nzones; i++) {
        const jbzone_t *z = &p->zone[i];
        if (z->key == root && vel >= z->vlo && vel <= z->vhi && pick-- == 0) return z;
    }
    return 0;
}
static const int16_t PK_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
    3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t PK_IDX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static const int16_t PK_MULAW[256] = {
    -32124, -31100, -30076, -29052, -28028, -27004, -25980, -24956, -23932, -22908, -21884, -20860, -19836, -18812, -17788, -16764,
    -15996, -15484, -14972, -14460, -13948, -13436, -12924, -12412, -11900, -11388, -10876, -10364, -9852, -9340, -8828, -8316,
    -7932, -7676, -7420, -7164, -6908, -6652, -6396, -6140, -5884, -5628, -5372, -5116, -4860, -4604, -4348, -4092,
    -3900, -3772, -3644, -3516, -3388, -3260, -3132, -3004, -2876, -2748, -2620, -2492, -2364, -2236, -2108, -1980,
    -1884, -1820, -1756, -1692, -1628, -1564, -1500, -1436, -1372, -1308, -1244, -1180, -1116, -1052, -988, -924,
    -876, -844, -812, -780, -748, -716, -684, -652, -620, -588, -556, -524, -492, -460, -428, -396,
    -372, -356, -340, -324, -308, -292, -276, -260, -244, -228, -212, -196, -180, -164, -148, -132,
    -120, -112, -104, -96, -88, -80, -72, -64, -56, -48, -40, -32, -24, -16, -8, 0,
    32124, 31100, 30076, 29052, 28028, 27004, 25980, 24956, 23932, 22908, 21884, 20860, 19836, 18812, 17788, 16764,
    15996, 15484, 14972, 14460, 13948, 13436, 12924, 12412, 11900, 11388, 10876, 10364, 9852, 9340, 8828, 8316,
    7932, 7676, 7420, 7164, 6908, 6652, 6396, 6140, 5884, 5628, 5372, 5116, 4860, 4604, 4348, 4092,
    3900, 3772, 3644, 3516, 3388, 3260, 3132, 3004, 2876, 2748, 2620, 2492, 2364, 2236, 2108, 1980,
    1884, 1820, 1756, 1692, 1628, 1564, 1500, 1436, 1372, 1308, 1244, 1180, 1116, 1052, 988, 924,
    876, 844, 812, 780, 748, 716, 684, 652, 620, 588, 556, 524, 492, 460, 428, 396,
    372, 356, 340, 324, 308, 292, 276, 260, 244, 228, 212, 196, 180, 164, 148, 132,
    120, 112, 104, 96, 88, 80, 72, 64, 56, 48, 40, 32, 24, 16, 8, 0};
static int32_t pk_next(voice_t *v) {
    int32_t i = v->pk_pos++;
#ifdef JB_WEB
    if (v->pk_codec == 2) return (int16_t)(v->pk_d[2 * i] | v->pk_d[2 * i + 1] << 8);   /* 16-bit PCM, little endian */
#endif
    if (v->pk_codec) return PK_MULAW[v->pk_d[i]];
    int nib = (v->pk_d[i >> 1] >> ((i & 1) << 2)) & 15, step = PK_STEP[v->pk_idx], diff = step >> 3;
    if (nib & 4) diff += step;
    if (nib & 2) diff += step >> 1;
    if (nib & 1) diff += step >> 2;
    int32_t y = v->pk_pred + ((nib & 8) ? -diff : diff);
    if (y > 32767) y = 32767; else if (y < -32768) y = -32768;
    int ix = v->pk_idx + PK_IDX[nib & 7];
    v->pk_pred = y; v->pk_idx = (uint8_t)(ix < 0 ? 0 : ix > 88 ? 88 : ix);
    return y;
}

/* songbook: "JBSB" v1, u32 length, text, fnv32. Text: one song per line: TITLE;key;minor;ballad;chart */
#define BOOK_MAX 65536
#define BOOK_SONGS 128
/* The songbook comes from, in this order: jbsongs (a data plugin the companion sends over USB, found once at boot),
 * the slot 5 sample, the 2 built-in songs. jbsongs layout: plugins/jbsongs/jbsongs.h (kept identical here). */
#define JBS_MAGIC0 0x534A424Au
#define JBS_MAGIC1 0x31474E4Fu
static const uint8_t JBS_PRESS[4] = { 0xCA, 0xC2, 0xD3, 0xC7 };   /* signature inside op1_fx_t (jbsongs.h) */
typedef struct { op1_fx_t fx; uint32_t magic[2]; uint32_t len, sum, count; char text[BOOK_MAX]; } jbsongs_t;
enum { BOOK_BUILTIN, BOOK_SLOT, BOOK_USB };
static struct {
    const uint8_t *sb; uint32_t hash; jstream_t s; uint32_t want, got, c;
    int phase;                          /* 0 idle / invalid, 1 decoding, 2 ready */
    volatile int ok, n, gen, src;       /* src: BOOK_* the songs come from */
    const char *bt;                     /* the songbook text: text[] (slot 5) or the jbsongs plugin's */
    const jbsongs_t *usb; int usb_state; uint32_t usb_pos, usb_c;   /* usb_state: 0 look, 1 checking, 2 in use, 3 none / bad */
    char text[BOOK_MAX];
    uint32_t line[BOOK_SONGS];
} B;

/* set lists (organisation only: nothing plays on by itself): book lines "@NAME|3 0 7" (song numbers in the book, from 0).
 * No ';' in them, so older plugins skip these lines. */
#define SET_MAX 16
#define SET_NAME 24
static struct { int n; uint32_t name[SET_MAX]; uint8_t nlen[SET_MAX], count[SET_MAX], song[SET_MAX][BOOK_SONGS]; } SETS;
enum { VIEW_ALL = -2, VIEW_MENU = -1 };

static void book_index(void) {
    int n = 0; uint32_t i = 0;
    while (i < B.got && n < BOOK_SONGS) {
        uint32_t j = i; int semis = 0;
        while (j < B.got && B.bt[j] != '\n') { if (B.bt[j] == ';') semis++; j++; }
        if (semis >= 4) B.line[n++] = i;
        i = j + 1;
    }
    SETS.n = 0;
    for (i = 0; i < B.got && SETS.n < SET_MAX; ) {
        uint32_t j = i; int semis = 0;
        while (j < B.got && B.bt[j] != '\n') { if (B.bt[j] == ';') semis++; j++; }
        if (B.bt[i] == '@' && !semis) {
            int k = SETS.n; uint32_t q = i + 1, c = 0;
            while (q < j && B.bt[q] != '|') q++;
            SETS.name[k] = i + 1; SETS.nlen[k] = (uint8_t)(q - i - 1 > SET_NAME ? SET_NAME : q - i - 1);
            for (q++; q < j && c < BOOK_SONGS - 1; ) {
                while (q < j && (B.bt[q] < '0' || B.bt[q] > '9')) q++;
                if (q >= j) break;
                int v = 0; while (q < j && B.bt[q] >= '0' && B.bt[q] <= '9') { v = v * 10 + B.bt[q] - '0'; if (v > 999) v = 999; q++; }
                if (v < n) SETS.song[k][c++] = (uint8_t)v;
            }
            SETS.count[k] = (uint8_t)c;
            if (c && SETS.nlen[k]) SETS.n++;
        }
        i = j + 1;
    }
    B.n = n; B.ok = n > 0; B.gen++;
}
/* Peaks alone do not identify a data sample: equal-size books use the same
 * four PCM levels and usually have identical peak tables. Include the encoded
 * length and trailing checksum so replacing a slot in place detects edits. */
static uint32_t book_hash(const uint8_t *sb) {
    uint32_t h = slot_hash(sb);
    jstream_t s;
    if (!js_open(&s, op1_sample_data(sb), slot_len(sb))) return h;
    for (int i = 0; i < 5; i++) h = fnv(h, js_byte(&s));
    uint32_t length = js_u32(&s);
    h = (h ^ length) * 16777619u;
    if (length <= BOOK_MAX && (uint32_t)s.pos + length * 4 + 16 <= (uint32_t)s.len) {
        s.pos += (int32_t)(length * 4);
        h = (h ^ js_u32(&s)) * 16777619u;
    }
    return h;
}
static const jbsongs_t *usb_find(void) {
    for (int i = 1; i < op1_fx_count && i < 64; i++) {
        const op1_fx_t *fx = op1_fxs[i];
        if (!fx || fx->press[0] != JBS_PRESS[0] || fx->press[1] != JBS_PRESS[1] || fx->press[2] != JBS_PRESS[2] ||
            fx->press[3] != JBS_PRESS[3]) continue;                 /* only then read past its op1_fx_t */
        const jbsongs_t *b = (const jbsongs_t *)fx;
        if (b->magic[0] == JBS_MAGIC0 && b->magic[1] == JBS_MAGIC1) return b;
    }
    return 0;
}
/* jbsongs: checked once (FNV-1a over the text, 2 KB a block), then used in place; returns 1 while it decides or is used */
static int usb_scan(void) {
    if (B.usb_state == 2) return 1;
    if (B.usb_state == 3) return 0;
    if (B.usb_state == 0) {
        const jbsongs_t *b = usb_find();
        if (!b || b->len == 0 || b->len > BOOK_MAX) { B.usb_state = 3; return 0; }
        B.usb = b; B.usb_pos = 0; B.usb_c = 2166136261u; B.usb_state = 1;
        return 1;
    }
    const jbsongs_t *b = B.usb;
    for (int k = 0; k < 2048 && B.usb_pos < b->len; k++) B.usb_c = fnv(B.usb_c, b->text[B.usb_pos++]);
    if (B.usb_pos < b->len) return 1;
    if (B.usb_c != b->sum) { B.usb_state = 3; return 0; }
    B.phase = 0; B.sb = 0; B.hash = 0;                 /* the slot 5 songbook is not used while jbsongs is there */
    B.bt = b->text; B.got = b->len; B.src = BOOK_USB; B.usb_state = 2;
    book_index();
    return 1;
}
static void book_scan(int check) {                  /* audio thread: decode in steps every block; change check when asked */
    if (usb_scan()) return;
    if (B.phase == 1) {
        for (int k = 0; k < 512 && B.got < B.want; k++) {
            int x = js_byte(&B.s);
            if (x < 0) { B.phase = 0; return; }
            B.text[B.got++] = (char)x; B.c = fnv(B.c, x);
        }
        if (B.got == B.want) { B.phase = js_u32(&B.s) == B.c ? 2 : 0; if (B.phase == 2) { B.bt = B.text; B.src = BOOK_SLOT; book_index(); } }
        return;
    }
    if (!check) return;
    const uint8_t *sb = op1_sample_buffer(SLOT_BOOK);
    uint32_t h = sb ? book_hash(sb) : 0;
    if (sb == B.sb && h == B.hash) return;
    B.sb = sb; B.hash = h; B.phase = 0;
    if (B.ok) { B.ok = 0; B.n = 0; SETS.n = 0; B.src = BOOK_BUILTIN; B.gen++; }
    if (!sb || !js_open(&B.s, op1_sample_data(sb), slot_len(sb))) return;
    B.c = 2166136261u; int m[5];
    for (int i = 0; i < 5; i++) { m[i] = js_byte(&B.s); B.c = fnv(B.c, m[i]); }
    if (m[0] != 'J' || m[1] != 'B' || m[2] != 'S' || m[3] != 'B' || m[4] != 1) return;
    uint32_t n = 0;
    for (int q = 0; q < 4; q++) { int x = js_byte(&B.s); B.c = fnv(B.c, x); n |= (uint32_t)(x & 255) << (8 * q); }
    if (n == 0 || n > BOOK_MAX) return;
    B.want = n; B.got = 0; B.phase = 1;
}

/* ---------------------------------------------------------------- songs */

static int nsongs(void) { return B.ok ? B.n : JB_NSONGS; }
static int field(const char *p, const char *end, int k, const char **fs) {   /* k-th ';' field of a book line */
    for (int i = 0; i < k && p < end; i++) { while (p < end && *p != ';') p++; if (p < end) p++; }
    const char *q = p; while (q < end && *q != ';' && *q != '\n') q++;
    *fs = p; return (int)(q - p);
}
static const char *line_end(const char *p) { const char *e = B.bt + B.got; while (p < e && *p != '\n') p++; return p; }
static int atoi_n(const char *p, int n) { int v = 0; for (int i = 0; i < n && p[i] >= '0' && p[i] <= '9'; i++) v = v * 10 + p[i] - '0'; return v; }
/* song i: title (not 0-terminated for book songs), key, minor, chart (pointer + length) */
static void song_info(int i, const char **title, int *tlen, int *key, int *minor, const char **chart, int *clen) {
    if (B.ok && i < B.n) {
        const char *p = B.bt + B.line[i], *end = B.bt + B.got, *f;
        *tlen = field(p, end, 0, title);
        int n = field(p, end, 1, &f); *key = atoi_n(f, n) % 12;
        n = field(p, end, 2, &f); *minor = n > 0 && f[0] == '1';
        *clen = field(p, end, 4, chart);
        return;
    }
    if (i >= JB_NSONGS) i = 0;
    *title = SONGS[i].title; *tlen = 0; while ((*title)[*tlen]) (*tlen)++;
    *key = SONGS[i].key; *minor = SONGS[i].minor; *chart = SONGS[i].chart; *clen = 0; while ((*chart)[*clen]) (*clen)++;
}

/* song i's feel: songbook field 3 (0 swing, 1 ballad, 2 bossa nova); the built-in songs swing */
static int song_ballad(int i) {
    if (B.ok && i < B.n) {
        const char *p = B.bt + B.line[i], *end = line_end(p), *f;
        int n = field(p, end, 3, &f); return n > 0 && f[0] == '1' ? 1 : n > 0 && f[0] == '2' ? 2 : 0;
    }
    return 0;
}
/* song i's tempo: songbook field 5 (lines without it, or out of 40..320: 0 = follow the OP-1) */
#ifdef JB_HOST_OP1_TEMPO
static int jb_host_op1_tempo = 1;   /* host tests: the OP-1 clock drives the band unless a test turns this off */
#endif
static int song_bpm(int i) {
#ifdef JB_HOST_OP1_TEMPO
    if (jb_host_op1_tempo) return 0;
#endif
    if (B.ok && i < B.n) {
        const char *p = B.bt + B.line[i], *end = line_end(p), *f;
        int n = field(p, end, 5, &f), v = atoi_n(f, n);
        if (n > 0) { const char *q = f - 1; if (q < p || *q != ';') return 0; }   /* field 5 really exists on this line */
        return v >= 40 && v <= 320 ? v : 0;
    }
    return SONGS[i < JB_NSONGS ? i : 0].bpm;
}
/* the chart's own tempo: field 6 (the companion writes it when a practice tempo is set in field 5), else field 5 */
static int song_orig_bpm(int i) {
    int bpm = song_bpm(i);
    if (bpm && B.ok && i < B.n) {
        const char *p = B.bt + B.line[i], *end = line_end(p), *f;
        int n = field(p, end, 6, &f), v = atoi_n(f, n);
        if (n > 0 && f - 1 >= p && f[-1] == ';' && v >= 40 && v <= 320) return v;
    }
    return bpm;
}
/* the tempo the band plays a song at: one turned on the OP-1 this session, else the songbook's */
static int play_bpm(int i) { int t = i >= 0 && i < 130 ? TADJ[i] : 0; return t ? t : song_bpm(i); }
static int streq(const char *a, int n, const char *b) { int i = 0; for (; i < n && b[i]; i++) if (a[i] != b[i]) return 0; return i == n && !b[i]; }
static uint8_t parse_q(const char *q, int n) {
    if (streq(q, n, "^7") || streq(q, n, "^")) return Q_MAJ7;
    if (streq(q, n, "-7")) return Q_MIN7;
    if (streq(q, n, "h7") || streq(q, n, "h")) return Q_HALFDIM;
    if (streq(q, n, "o7") || streq(q, n, "o")) return Q_DIM7;
    if (streq(q, n, "-6") || streq(q, n, "-")) return Q_MIN6;
    if (streq(q, n, "-^7")) return Q_MINMAJ7;
    if (streq(q, n, "7b9")) return Q_DOM7B9;
    if (n == 0 || streq(q, n, "6")) return Q_MAJ6;
    if (q[0] == '7' || q[0] == '9') return Q_DOM7;
    return Q_MAJ7;
}
static void load_chart(jb_t *e, const char *p, int plen) {
    static const int8_t pcs[7] = {9, 11, 0, 2, 4, 5, 7};
    static const int8_t map[5][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 1, 1}, {0, 0, 1, 2}, {0, 1, 2, 3}};
    static const int8_t map3[5][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 1, 1}, {0, 1, 2, 2}, {0, 1, 2, 2}};   /* 3/4: 2 chords = 2 + 1 beats */
    const char *end = p + plen;
    char section = 0;
    e->nbars = 0; e->nb = 4;
    while (p < end && e->nbars < JB_MAXBARS) {
        chord_t ch[4]; int n = 0, repeat = 0;
        while (p < end && *p != '|') {
            while (p < end && *p == ' ') p++;
            const char *t = p; while (p < end && *p != ' ' && *p != '|') p++;
            int len = (int)(p - t);
            if (len <= 0) continue;
            if (t[0] == '*') section = len > 1 ? t[1] : 0;
            else if (t[0] == 'T' && len == 3 && t[1] == '3' && t[2] == '4') e->nb = 3;      /* iReal's time signature token */
            else if (t[0] == '%') repeat = 1;
            else if (t[0] >= 'A' && t[0] <= 'G' && n < 4) {
                int pc = pcs[t[0] - 'A'], i = 1;
                if (i < len && t[i] == 'b') { pc--; i++; } else if (i < len && t[i] == '#') { pc++; i++; }
                ch[n].root = (int8_t)pcof(pc); ch[n].q = parse_q(t + i, len - i); n++;
            }
        }
        if (p < end && *p == '|') p++;
        if (repeat && e->nbars > 0 && n == 0) {
            for (int k = 0; k < 4; k++) e->bars[e->nbars].beat[k] = e->bars[e->nbars - 1].beat[k];   /* no struct copy: memcpy */
            e->bars[e->nbars].section = section; section = 0; e->nbars++;
        } else if (n) {
            bar_t *b = &e->bars[e->nbars++];
            for (int k = 0; k < 4; k++) b->beat[k] = ch[e->nb == 3 ? map3[n][k] : map[n][k]];
            b->section = section; section = 0;
        }
    }
    if (e->nbars == 0) {   /* nothing usable: one bar of C6 so the band never reads an empty chart */
        e->nbars = 1; for (int k = 0; k < 4; k++) { e->bars[0].beat[k].root = 0; e->bars[0].beat[k].q = Q_MAJ6; }
        e->bars[0].section = 0;
    }
    e->tpbar = e->nb * JB_TPB;
}
static void form_reset(jb_t *e);
static void set_bpm(jb_t *e, int bpm) {                /* a division: on a song load or a tempo click only */
    e->bpm = bpm; e->s_bpm = bpm;
    e->blen = bpm ? (int32_t)(((2646000u << 4) / (uint32_t)bpm) << 10) : 0;   /* 2646000 / bpm << 14 */
}
static int song_ballad(int i);
static void save_song(jb_t *e) {                         /* the song's own settings, kept for when it comes back */
    if (!e->loaded || e->song < 0 || e->song >= 130) return;
    songset_t *x = &SS[e->song];
    x->used = 1; x->loop = (uint8_t)e->loop; x->lfrom = (uint8_t)e->lfrom; x->lto = (uint8_t)e->lto; x->kstep = (uint8_t)e->kstep;
    x->choruses = (uint8_t)e->choruses; x->intro = (uint8_t)e->intro; x->turn = (uint8_t)e->turn; x->endtype = (uint8_t)e->endtype;
    x->ballad = (uint8_t)e->ballad; x->bossa = (uint8_t)e->bossa; x->key = (int8_t)e->key_off;
}
static void load_song(jb_t *e, int idx) {
    const char *t, *c; int tl, key, minor, cl;
    if (idx >= nsongs()) idx = 0;
    save_song(e);
    song_info(idx, &t, &tl, &key, &minor, &c, &cl);
    load_chart(e, c, cl);
    e->song = idx; e->s_song = idx; e->book_gen = B.gen;
    set_bpm(e, play_bpm(idx));
    e->orig_bpm = song_orig_bpm(idx);
    e->nsec = 0;                                         /* sections: each marked bar starts one (a chart without marks: none) */
    for (int b = 0; b < e->nbars && e->nsec < 16; b++)
        if (e->bars[b].section) { e->sec_at[e->nsec] = (uint8_t)b; e->sec_ch[e->nsec] = e->bars[b].section; e->nsec++; }
    {   /* this song's settings from earlier in the session, else its defaults (feel from the songbook) */
        const songset_t *x = idx < 130 ? &SS[idx] : 0;
        if (x && x->used) {
            e->loop = x->loop <= e->nsec + 1 ? x->loop : 0;
            e->lfrom = x->lfrom < e->nbars ? x->lfrom : 0; e->lto = x->lto < e->nbars ? x->lto : e->nbars - 1;
            e->kstep = x->kstep % KS_N; e->choruses = x->choruses; e->intro = x->intro % IN_N; e->turn = x->turn % TA_N;
            e->endtype = x->endtype & 1; e->ballad = x->ballad & 1; e->bossa = !e->ballad && (x->bossa & 1); e->key_off = x->key;
        } else {
            e->loop = 0; e->lfrom = 0; e->lto = e->nbars < 8 ? e->nbars - 1 : 7;
            e->kstep = KS_OFF; e->choruses = 0; e->intro = IN_COUNT; e->turn = TA_OFF; e->endtype = 0;
            { int fl = song_ballad(idx); e->ballad = fl == 1; e->bossa = fl == 2; } e->key_off = 0;
        }
    }
    e->shift = 0; e->pass = 0; e->chorus = 1; e->ending = 0; e->tag_i = 0; e->kind = BK_CHART;
    e->bcur = -1; e->sbar = -1; e->jump = -1; e->bidle = 0; e->key_pend = 0; e->bm = 0;
    e->transpose = e->s_transpose = e->key_off;
    if (e->bidx_prev >= 0) e->kbase = e->bidx_prev - e->key_off;   /* blue's position now means this song's key */
    e->loaded = 1;
    if (e->bar >= e->nbars || e->bar < 0) e->bar = 0;
    form_reset(e);
}
static chord_t chord_at(const jb_t *e, int bar, int beat) {   /* the chart as written, in the blue key */
    chord_t c = e->bars[bar].beat[beat]; c.root = (int8_t)pcof(c.root + e->transpose); return c;
}
/* ---- form: what the band plays next. The chart, looped (a section or a bar range, optionally a key step each pass),
 * turnarounds at the end of a chorus, a set number of choruses, an ending (the final chord, or a tag first). */
static int loop_lo(const jb_t *e) {
    if (e->loop >= 1 && e->loop <= e->nsec) return e->sec_at[e->loop - 1];
    return e->lfrom < e->nbars ? e->lfrom : 0;
}
static int loop_hi(const jb_t *e) {
    if (e->loop >= 1 && e->loop <= e->nsec) return e->loop < e->nsec ? e->sec_at[e->loop] - 1 : e->nbars - 1;
    int lo = loop_lo(e), hi = e->lto < e->nbars ? e->lto : e->nbars - 1;
    return hi < lo ? lo : hi;
}
static int looping(const jb_t *e) { return e->loop > 0 && e->loop <= e->nsec + 1; }
static int kstep_semis(int k) { return k == KS_UP ? 1 : k == KS_DOWN ? -1 : k == KS_FOURTHS ? 5 : 0; }
static int wrapk(int s) { s = pcof(s); return s > 6 ? s - 12 : s; }   /* -5..+6 */
static chord_t mkc(int root, int q) { chord_t c; c.root = (int8_t)pcof(root); c.q = (uint8_t)q; return c; }
/* the home chord: the song's key (major 6 / minor 6); a dominant first chord on the key (a blues) stays dominant */
static chord_t tonic(const jb_t *e) {
    const char *t, *ch; int tl, key, minor, cl;
    song_info(e->song, &t, &tl, &key, &minor, &ch, &cl);
    chord_t f = e->bars[0].beat[0];
    if (f.root == key && (f.q == Q_DOM7 || (minor ? f.q == Q_MIN6 || f.q == Q_MINMAJ7 : f.q == Q_MAJ6 || f.q == Q_MAJ7))) return f;
    return mkc(key, minor ? Q_MIN6 : Q_MAJ6);
}
static int minorish(int q) { return q == Q_MIN7 || q == Q_MIN6 || q == Q_MINMAJ7 || q == Q_HALFDIM || q == Q_DIM7; }
/* ii-V into chord t: in minor, half-diminished ii and V7b9 */
static void two_five(bar_t *b, chord_t t, int nb) {
    int m = minorish(t.q);
    chord_t ii = mkc(t.root + 2, m ? Q_HALFDIM : Q_MIN7), v = mkc(t.root + 7, m ? Q_DOM7B9 : Q_DOM7);
    for (int k = 0; k < 4; k++) b->beat[k] = k < (nb == 3 ? 2 : 2) ? ii : v;
    if (nb == 3) b->beat[2] = b->beat[3] = v;
}
/* does the last bar already lead home? (a V of the first chord, or its tritone sub, in it) */
static int has_turnaround(const jb_t *e) {
    int r0 = e->bars[0].beat[0].root;
    for (int k = 0; k < e->nb; k++) {
        chord_t c = e->bars[e->nbars - 1].beat[k];
        if ((c.q == Q_DOM7 || c.q == Q_DOM7B9) && (pcof(c.root - r0) == 7 || pcof(c.root - r0) == 1)) return 1;
    }
    return 0;
}
/* bar `bar` of kind `kind` as played, its roots moved by `shift`; `wraps`: the chorus goes round after it */
static void make_bar(const jb_t *e, bar_t *o, int bar, int kind, int shift, int wraps) {
    o->section = 0;
    if (kind == BK_INTRO) {                              /* bar = the intro bar 0..3, leading into the first bar played */
        int first = e->bar >= 0 && e->bar < e->nbars ? e->bar : 0;   /* the bar the intro leads into */
        chord_t t = e->bars[first].beat[0];
        if (e->intro == IN_TURN) {                       /* I - VI7 | ii - V7, twice (minor: i - vi7b5 | ii7b5 - V7b9) */
            int m = minorish(t.q);
            chord_t a = t, b = mkc(t.root + 9, m ? Q_HALFDIM : Q_DOM7);
            if (bar & 1) two_five(o, t, e->nb);
            else { for (int k = 0; k < 4; k++) o->beat[k] = k < 2 ? a : b; if (e->nb == 3) o->beat[2] = o->beat[3] = b; }
        } else {                                         /* the last 4 bars of the form (or of the loop) */
            int end = looping(e) ? loop_hi(e) : e->nbars - 1, lo = looping(e) ? loop_lo(e) : 0, n = end - lo + 1;
            int src = n >= 4 ? end - 3 + bar : lo + (bar + 4 * n - 4) % n;
            for (int k = 0; k < 4; k++) o->beat[k] = e->bars[src].beat[k];
            if (bar == 3 && e->turn && n >= 4 && !looping(e) && !has_turnaround(e)) two_five(o, t, e->nb);   /* lead in */
        }
        for (int k = 0; k < 4; k++) o->beat[k].root = (int8_t)pcof(o->beat[k].root + shift);
        return;
    }
    if (kind == BK_TAG || kind == BK_FINAL || kind == BK_HOLD || kind == BK_END) {
        chord_t t = tonic(e);
        if (kind == BK_TAG) two_five(o, t, e->nb); else for (int k = 0; k < 4; k++) o->beat[k] = t;
    } else {
        if (bar < 0 || bar >= e->nbars) bar = 0;
        for (int k = 0; k < 4; k++) o->beat[k] = e->bars[bar].beat[k];
        o->section = e->bars[bar].section;
        if (wraps && e->turn && e->nbars >= 4 && !has_turnaround(e)) {
            chord_t t = e->bars[0].beat[0];
            int last = bar == e->nbars - 1, before = bar == e->nbars - 2;
            if (last) two_five(o, t, e->nb);
            else if (before && e->turn == TA_IIIVIIIV) {          /* iii - VI7 | ii - V7 */
                chord_t iii = mkc(t.root + 4, Q_MIN7), vi = mkc(t.root + 9, Q_DOM7);
                if (minorish(t.q)) { iii = mkc(t.root + 3, Q_MAJ7); vi = mkc(t.root + 8, Q_DOM7); }   /* minor: bIII - bVI7 */
                for (int k = 0; k < 4; k++) o->beat[k] = k < 2 ? iii : vi;
                if (e->nb == 3) o->beat[2] = o->beat[3] = vi;
            }
        }
    }
    for (int k = 0; k < 4; k++) o->beat[k].root = (int8_t)pcof(o->beat[k].root + shift);
}
/* the bar after (bar, kind): sets nx_*. Called at every bar line and when a setting changes. */
static void plan_next(jb_t *e) {
    int b = e->bar, k = e->kind, nb = b, nk = BK_CHART, ns = e->shift, ending = e->ending;
    int end_kind = e->endtype ? BK_TAG : BK_FINAL;
    if (k == BK_INTRO) { if (e->intro_i < 3) { nk = BK_INTRO; nb = e->intro_i + 1; } else { nb = b; ns = 0; } }   /* then the head */
    else if (k == BK_FINAL) nk = BK_HOLD;
    else if (k == BK_HOLD || k == BK_END) nk = BK_END;
    else if (k == BK_TAG) nk = e->tag_i < 2 ? BK_TAG : BK_FINAL;
    else if (looping(e)) {
        int lo = loop_lo(e), hi = loop_hi(e);
        if (b < lo || b > hi) nb = lo;
        else if (b == hi) { if (ending) nk = end_kind; else { nb = lo; ns = wrapk(ns + kstep_semis(e->kstep)); } }
        else nb = b + 1;
    } else {
        ns = 0;                                          /* out of a loop: back to the written key at the next bar */
        if (b + 1 >= e->nbars) {
            if (ending || (e->choruses && e->chorus >= e->choruses)) nk = end_kind;
            else nb = 0;
        } else nb = b + 1;
    }
    if (e->jump >= 0 && e->jump < e->nbars && (k == BK_CHART || k == BK_INTRO) && !ending) {   /* white: go to a bar */
        nb = e->jump; nk = BK_CHART; if (!looping(e)) ns = 0;
    }
    e->nx_bar = nb; e->nx_kind = nk; e->nx_shift = nk == BK_CHART ? ns : e->shift;
    /* the next bar as played: does the chorus go round after it? (only then the turnaround) */
    int wraps = 0;
    if (nk == BK_CHART && !looping(e)) {
        int last = e->nbars - 1;
        wraps = !ending && !(e->choruses && e->chorus + (nb < b ? 1 : 0) >= e->choruses);
        if (nb != last && nb != last - 1) wraps = 0;
    }
    make_bar(e, &e->vbn, nb, nk, e->nx_shift, wraps);
}
static void begin_bar(jb_t *e) {                         /* at the bar line: the planned bar starts */
    int jumped = e->jump >= 0 && e->nx_kind == BK_CHART && e->nx_bar == e->jump;
    if (jumped) { e->jump = -1; e->bcur = -1; }          /* a jump is not a new chorus or pass */
    else if (e->kind == BK_CHART && e->nx_kind == BK_CHART && !looping(e) && e->nx_bar < e->bar) e->chorus++;
    else if (e->kind == BK_CHART && e->nx_kind == BK_CHART && looping(e) && e->nx_bar == loop_lo(e) && e->bar == loop_hi(e)) e->pass++;
    if (e->nx_kind == BK_TAG) e->tag_i = e->kind == BK_TAG ? e->tag_i + 1 : 0;
    if (e->nx_kind == BK_INTRO) e->intro_i = e->kind == BK_INTRO ? e->intro_i + 1 : 0;
    if (e->kind == BK_INTRO && e->nx_kind == BK_CHART) e->crash_next = 1;   /* into the head */
    if (e->nx_kind == BK_CHART) e->bar = e->nx_bar;
    e->kind = e->nx_kind; e->shift = e->nx_shift;
    for (int k = 0; k < 4; k++) e->vb.beat[k] = e->vbn.beat[k];
    e->vb.section = e->vbn.section;
    plan_next(e);
}
static void form_reset(jb_t *e) { make_bar(e, &e->vb, e->bar, BK_CHART, e->shift, 0); e->kind = BK_CHART; plan_next(e); }
static chord_t cur_chord(const jb_t *e, int beat) { chord_t c = e->vb.beat[beat & 3]; c.root = (int8_t)pcof(c.root + e->transpose); return c; }
/* the next bar's transposition: a queued key change takes effect there, so anticipations land in the new key */
static int next_transpose(const jb_t *e) { return e->key_pend ? e->key_tgt - e->nx_shift : e->transpose; }
static chord_t next_chord(const jb_t *e) { chord_t c = e->vbn.beat[0]; c.root = (int8_t)pcof(c.root + next_transpose(e)); return c; }
/* a queued key change: at a bar line (or at once when stopped). The target is the actual key offset; the loop's own
 * key step (shift) stays as it is, so key_off takes the difference. */
static void apply_key(jb_t *e) {
    if (!e->key_pend) return;
    e->key_off = e->key_tgt - e->shift; e->key_pend = 0;
    e->transpose = e->s_transpose = e->key_off;
    if (e->bidx_prev >= 0) e->kbase = e->bidx_prev - e->key_tgt;
}

/* ------------------------------------------------------------ generators */

static void add(jb_t *e, int tick, int inst, int note, int vel, int dur) {
    if (e->nev >= JB_MAXEV || tick >= JB_TPBAR || note < 0 || note > 127) return;
    if (vel < 1) vel = 1;
    if (vel > 127) vel = 127;
    if (dur < 1) dur = 1;
    if (dur > 255) dur = 255;
    ev_t *v = &e->ev[e->nev++];
    v->tick = (uint8_t)tick; v->inst = (uint8_t)inst; v->note = (uint8_t)note; v->vel = (uint8_t)vel; v->dur = (uint8_t)dur;
}
/* ---- styles. Each instrument has 5 accompaniment styles, modelled on how these players comp (an impression of their
 * approach, not a transcription). Style 0 is the original 0.5 band. */
enum { NSTY = 12, NPIA = 10 };                      /* the most players on one instrument; piano and guitar players */
static const uint8_t NPLAY[4] = {12, 10, 10, 10};    /* drums, bass, piano, guitar */
enum { DR_COBB, DR_PHILLY, DR_ELVIN, DR_MOTIAN, DR_BLAKEY, DR_TONY, DR_HAYNES, DR_HIGGINS, DR_JOJONES, DR_DEJOHNETTE, DR_ROACH, DR_AIRTO };
enum { BS_CHAMBERS, BS_BROWN, BS_CARTER, BS_LAFARO, BS_MINGUS, BS_SAMJONES, BS_HADEN, BS_GARRISON, BS_PAGE, BS_PETTIFORD };
enum { PN_EVANS, PN_GARLAND, PN_MCCOY, PN_HERBIE, PN_MONK, PN_KELLY, PN_BASIE, PN_JAMAL, PN_SILVER, PN_GARNER };
enum { GT_FREDDIE, GT_HALL, GT_WES, GT_BURRELL, GT_DJANGO, GT_GRANT, GT_ELLIS, GT_KESSEL, GT_CHRISTIAN, GT_GILBERTO };
static const char *const STY_NAME[4][NSTY] = {
    {"JIMMY COBB", "PHILLY JOE", "ELVIN JONES", "PAUL MOTIAN", "ART BLAKEY", "TONY WILLIAMS", "ROY HAYNES", "BILLY HIGGINS",
     "JO JONES", "JACK DEJOHNETTE", "MAX ROACH", "AIRTO"},
    {"PAUL CHAMBERS", "RAY BROWN", "RON CARTER", "SCOTT LAFARO", "CHARLES MINGUS", "SAM JONES", "CHARLIE HADEN",
     "JIMMY GARRISON", "WALTER PAGE", "OSCAR PETTIFORD", "", ""},
    {"BILL EVANS", "RED GARLAND", "MCCOY TYNER", "HERBIE HANCOCK", "THELONIOUS MONK", "WYNTON KELLY", "COUNT BASIE",
     "AHMAD JAMAL", "HORACE SILVER", "ERROLL GARNER", "", ""},
    {"FREDDIE GREEN", "JIM HALL", "WES MONTGOMERY", "KENNY BURRELL", "DJANGO REINHARDT", "GRANT GREEN", "HERB ELLIS",
     "BARNEY KESSEL", "CHARLIE CHRISTIAN", "JOAO GILBERTO", "", ""}};
static const char *const STY_HINT[4][NSTY] = {
    {"STEADY RIDE - LIGHT COMPING", "DRIVING - SNARE CHATTER - RIM ON 4", "TRIPLET WAVES ACROSS THE BAR", "BROKEN TIME - OPEN AND SPARSE", "PRESS ROLLS - STRONG ACCENTS",
     "HAT ON EVERY BEAT - FAST BURSTS", "CRISP SNARE SNAPS - SURPRISES", "BUOYANT RIDE - LIGHT TOUCH", "TIME ON THE HI-HAT - BASIE SWING",
     "WASHY RIDE - TOMS TALK BACK", "MELODIC TOMS - HAT ON 2 AND 4", "BRAZILIAN - SHAKER - CONGA - SURDO"},
    {"CLASSIC WALKING LINE", "ROOTS AND FIFTHS - BIG PUSH", "CHROMATIC - WIDE RANGE", "TALKS BACK - TRIPLETS AND SPACE", "BLUES RIFFS - PEDALS - BIG ACCENTS",
     "DEEP WALK - ROOTS ON 1 AND 3", "SIMPLE ROOTS - SPACE", "PEDAL POINTS - DOUBLE STOPS", "EVEN FOUR - ARPEGGIOS", "MELODIC BEBOP - PICKUPS", "", ""},
    {"ROOTLESS VOICINGS", "SHORT STABS ON THE AND OF 2 AND 4", "FOURTHS - LEFT HAND ROOT AND 5TH", "SPARSE - HIGH - ODD PLACES", "ANGULAR CHORDS - DISPLACED STABS",
     "BOUNCY - BLUESY ANTICIPATIONS", "SPARSE HIGH PLINKS", "SPACE - VAMPS - BLOCK CHORDS", "FUNKY - LEFT HAND PUNCHES", "LEFT HAND STRUMS FOUR", "", ""},
    {"FOUR TO THE BAR", "LIGHT - HIGH - LYRICAL", "PUNCHY STABS - OCTAVE FILLS", "BLUESY CHARLESTON - SLIDES", "LA POMPE - SHORT ACCENTED CHORDS",
     "BLUESY STABS - REPEATED NOTE RIFFS", "SWINGING FOUR - SLIDE CHORDS", "CHORD MELODY - MOVING TOP", "SWING ERA FOUR - HORN RIFFS", "BOSSA BATIDA - THUMB BASS", "", ""}};
/* bossa nova: a two-bar clave in straight eighths (tick 6 = the and), cross-stick, piano and guitar all follow it */
static const uint8_t CLAVE[2][3] = {{0, 18, 36}, {12, 30, 255}};

/* drums -------------------------------------------------------------------------------------------- */
static void drums_cobb(jb_t *e) {                                                  /* the original pattern */
    static const uint8_t rt[6] = {0, 12, 20, 24, 36, 44}, rv[6] = {95, 90, 60, 92, 90, 60};
    for (int i = 0; i < 6; i++) add(e, rt[i], I_DRUMS, D_RIDE, rv[i] + irand(e, 10) - 5 - (e->ballad ? 30 : 0), 1);
    add(e, 12, I_DRUMS, D_HAT, 80, 1); add(e, 36, I_DRUMS, D_HAT, 80, 1);
    if (e->ballad) return;
    for (int b = 0; b < 4; b++) add(e, b * 12, I_DRUMS, D_KICK, 26, 1);           /* feathered kick */
    if ((e->bar + 1) % 8 == 0) {                                                  /* fill into the next section */
        static const uint8_t ft[6] = {24, 28, 32, 36, 40, 44};
        for (int i = 0; i < 6; i++) add(e, ft[i], I_DRUMS, D_SNARE, 45 + i * 11, 1);
        e->crash_next = 1;
    } else {
        static const uint8_t ct[4] = {8, 20, 32, 44};
        for (int i = 0; i < 4; i++) if (chance(e, 140)) add(e, ct[i], I_DRUMS, D_SNARE, 28 + irand(e, 34), 1);
        if (chance(e, 50)) add(e, 44, I_DRUMS, D_KICK, 70, 1);
    }
}
static void drums_philly(jb_t *e) {                                                /* hard swing, busy left hand */
    static const uint8_t rt[6] = {0, 12, 20, 24, 36, 44}, rv[6] = {100, 96, 70, 98, 96, 72};
    for (int i = 0; i < 6; i++) add(e, rt[i], I_DRUMS, D_RIDE, rv[i] + irand(e, 8) - 4 - (e->ballad ? 32 : 0), 1);
    add(e, 12, I_DRUMS, D_HAT, 92, 1); add(e, 36, I_DRUMS, D_HAT, 92, 1);
    if (e->ballad) { add(e, 36, I_DRUMS, D_STICK, 48, 1); return; }                /* rim click on 4 */
    for (int b = 0; b < 4; b++) add(e, b * 12, I_DRUMS, D_KICK, 30, 1);
    if ((e->bar + 1) % 8 == 0) {                                                  /* triplet fill, snare and kick */
        for (int i = 0; i < 6; i++) add(e, 24 + i * 4, I_DRUMS, (i % 3 == 2) ? D_KICK : D_SNARE, 55 + i * 10, 1);
        e->crash_next = 1;
        return;
    }
    static const uint8_t ct[7] = {8, 16, 20, 28, 32, 40, 44};
    for (int i = 0; i < 7; i++) if (chance(e, 170)) add(e, ct[i], I_DRUMS, D_SNARE, 34 + irand(e, 40), 1);
    if (chance(e, 90)) add(e, chance(e, 500) ? 20 : 44, I_DRUMS, D_KICK, 88, 1);   /* a bomb */
    if ((e->bar & 3) == 3) add(e, 36, I_DRUMS, D_STICK, 72, 1);                   /* rim shot on 4 */
}
static void drums_elvin(jb_t *e) {                                                 /* rolling triplets over the bar line */
    static const uint8_t rt[6] = {0, 12, 20, 24, 36, 44}, rv[6] = {92, 86, 62, 90, 86, 62};
    int soft = e->ballad ? 30 : 0;
    for (int i = 0; i < 6; i++) add(e, rt[i], I_DRUMS, D_RIDE, rv[i] + irand(e, 12) - 6 - soft, 1);
    for (int b = 0; b < 4; b++) if (chance(e, 350)) add(e, b * 12 + 4, I_DRUMS, D_RIDE, 52 + irand(e, 14) - soft, 1);
    add(e, 12, I_DRUMS, D_HAT, 74, 1); add(e, 36, I_DRUMS, D_HAT, 74, 1);
    if (e->ballad) { if ((e->bar & 3) == 0) add(e, 0, I_DRUMS, D_CRASH, 36, 1); return; }
    if ((e->bar + 1) % 8 == 0) {                                                  /* triplet stream into the next section */
        for (int t = 16; t < 48; t += 4) add(e, t, I_DRUMS, ((t >> 2) & 1) ? D_KICK : D_SNARE, 40 + (t - 16) * 2, 1);
        e->crash_next = 1;
        return;
    }
    int off = irand(e, 2) * 4;                                                    /* quarter-note triplets: 3 against 4 */
    for (int t = off; t < 48; t += 8) {
        if (t == 0 || !chance(e, 520)) continue;
        if (chance(e, 600)) add(e, t, I_DRUMS, D_SNARE, 38 + irand(e, 32), 1); else add(e, t, I_DRUMS, D_KICK, 46 + irand(e, 20), 1);
    }
    if ((e->bar & 3) == 0 && chance(e, 400)) add(e, 0, I_DRUMS, D_CRASH, 58, 1);
}
static void drums_motian(jb_t *e) {                                                /* broken time: the pulse is implied */
    static const uint8_t rt[6] = {0, 12, 20, 24, 36, 44};
    int keep = e->ballad ? 450 : 650;
    for (int i = 0; i < 6; i++) if (i == 0 || chance(e, keep)) add(e, rt[i], I_DRUMS, D_RIDE, 50 + irand(e, 32) - (e->ballad ? 12 : 0), 1);
    for (int k = 0; k < 2; k++) if (chance(e, 300)) add(e, 4 + 4 * irand(e, 11), I_DRUMS, D_RIDE, 45 + irand(e, 25), 1);
    if (chance(e, 500)) add(e, 12, I_DRUMS, D_HAT, 60, 1);
    if (chance(e, 500)) add(e, 36, I_DRUMS, D_HAT, 60, 1);
    if (chance(e, 160)) add(e, irand(e, 2) * 24, I_DRUMS, D_CRASH, 30 + irand(e, 16), 1);   /* soft wash */
    if (e->ballad) return;
    if (chance(e, 300)) add(e, 4 * irand(e, 12), I_DRUMS, D_KICK, 40 + irand(e, 20), 1);
    for (int k = 0; k < 2; k++) if (chance(e, 250)) add(e, 4 + 4 * irand(e, 11), I_DRUMS, D_SNARE, 24 + irand(e, 22), 1);
}
/* Blakey: a firm ride, call-and-response accents, and rising snare rolls into sections.
 * Two-tick strokes approximate a press roll with the existing kit's snare voices. */
static void drums_blakey(jb_t *e) {
    int soft = e->ballad ? 32 : 0;
    for (int b = 0; b < e->nb; b++) {
        add(e, b * 12, I_DRUMS, D_RIDE, 98 + ((b & 1) ? 6 : 0) - soft, 1);
        if (b & 1) {
            add(e, b * 12, I_DRUMS, D_HAT, 88 - soft, 1);
            add(e, b * 12 + 8, I_DRUMS, D_RIDE, 70 - soft, 1);
        }
    }
    if (e->ballad) { add(e, (e->nb - 1) * 12, I_DRUMS, D_STICK, 46, 1); return; }
    add(e, 0, I_DRUMS, D_KICK, 54, 1);
    if ((e->bar + 1) % 8 == 0) {
        int from = e->tpbar - 24;
        for (int t = from; t < e->tpbar; t += 2)
            add(e, t, I_DRUMS, D_SNARE, 40 + (t - from) * 3, 1);
        add(e, e->tpbar - 4, I_DRUMS, D_KICK, 94, 1);
        e->crash_next = 1;
    } else {
        int t = (e->bar & 1) ? e->tpbar - 4 : 20;
        add(e, t, I_DRUMS, D_SNARE, 84 + irand(e, 12), 1);
        if ((e->bar & 3) == 3) {
            for (int t = e->tpbar - 12; t < e->tpbar; t += 4)
                add(e, t, I_DRUMS, D_SNARE, 58 + (t - (e->tpbar - 12)) * 4, 1);
        }
    }
}
/* 3/4, jazz waltz: ride on 1, 2, the 2-let, 3; hat on 2 and 3; the players as in 4/4 */
static void drums_waltz(jb_t *e) {
    static const uint8_t rt[4] = {0, 12, 20, 24}, rv[4] = {96, 86, 58, 90};
    static const uint8_t DW[NSTY] = {0, 1, 2, 3, 0, 2, 1, 0, 0, 3, 1, 0};        /* 1.5 drummers: the nearest 3/4 feel */
    int st = DW[e->sty[I_DRUMS] % NSTY], bal = e->ballad, soft = bal ? 30 : 0, loud = st == 1 ? 6 : 0;
    if (st == 3) {                                                                /* Motian: broken, sparse */
        for (int i = 0; i < 4; i++) if (i == 0 || chance(e, bal ? 450 : 650)) add(e, rt[i], I_DRUMS, D_RIDE, 50 + irand(e, 30) - (bal ? 12 : 0), 1);
        if (chance(e, 300)) add(e, 4 + 4 * irand(e, 8), I_DRUMS, D_RIDE, 45 + irand(e, 25), 1);
        if (chance(e, 500)) add(e, 12, I_DRUMS, D_HAT, 58, 1);
        if (chance(e, 160)) add(e, 0, I_DRUMS, D_CRASH, 30 + irand(e, 16), 1);
        if (!bal && chance(e, 250)) add(e, 4 + 4 * irand(e, 8), I_DRUMS, D_SNARE, 24 + irand(e, 22), 1);
        return;
    }
    for (int i = 0; i < 4; i++) add(e, rt[i], I_DRUMS, D_RIDE, rv[i] + loud + irand(e, 10) - 5 - soft, 1);
    if (st == 2) for (int b = 0; b < 3; b++) if (chance(e, 350)) add(e, b * 12 + 4, I_DRUMS, D_RIDE, 50 + irand(e, 14) - soft, 1);
    add(e, 12, I_DRUMS, D_HAT, 78 + loud, 1); add(e, 24, I_DRUMS, D_HAT, 62 + loud, 1);
    if (bal) { if (st == 1) add(e, 24, I_DRUMS, D_STICK, 46, 1); return; }
    add(e, 0, I_DRUMS, D_KICK, 34, 1);
    if ((e->bar + 1) % 8 == 0) {                                                  /* triplet fill on 2 and 3 */
        for (int i = 0; i < 6; i++) add(e, 12 + i * 4, I_DRUMS, (st && i % 3 == 2) ? D_KICK : D_SNARE, 45 + i * 11, 1);
        e->crash_next = 1;
        return;
    }
    if (st == 2) {                                                                /* Elvin: two against three, triplet snare */
        if (chance(e, 600)) add(e, 18, I_DRUMS, D_SNARE, 40 + irand(e, 25), 1);
        for (int t = 28; t < 36; t += 4) if (chance(e, 350)) add(e, t, I_DRUMS, D_SNARE, 34 + irand(e, 26), 1);
        if (chance(e, 300)) add(e, 32, I_DRUMS, D_KICK, 50 + irand(e, 16), 1);
        return;
    }
    static const uint8_t ct[4] = {8, 20, 28, 32};
    for (int i = st == 1 ? 0 : 1; i < 4; i += st == 1 ? 1 : 2) if (chance(e, st == 1 ? 200 : 140)) add(e, ct[i], I_DRUMS, D_SNARE, 30 + irand(e, 36), 1);
    if (st == 1 && (e->bar & 3) == 3) add(e, 24, I_DRUMS, D_STICK, 70, 1);       /* Philly: rim on 3 */
    if (st == 1 && chance(e, 90)) add(e, 32, I_DRUMS, D_KICK, 86, 1);
}
/* ---- 1.5 drummers (4/4; in 3/4 Tony, Haynes, Higgins, Jo Jones and DeJohnette use the nearest waltz feel) */
static void ride_time(jb_t *e, int v1, int v2, int soft) {                       /* ride on every beat, the skip before 2 and 4 */
    for (int b = 0; b < e->nb; b++) {
        add(e, b * 12, I_DRUMS, D_RIDE, v1 + irand(e, 8) - 4 - soft, 1);
        if (b & 1 && b * 12 + 8 < e->tpbar) add(e, b * 12 + 8, I_DRUMS, D_RIDE, v2 + irand(e, 8) - 4 - soft, 1);
    }
}
static void hat_24(jb_t *e, int vel) { for (int b = 1; b < e->nb; b += 2) add(e, b * 12, I_DRUMS, D_HAT, vel, 1); if (e->nb == 3) add(e, 24, I_DRUMS, D_HAT, vel - 14, 1); }
static void drums_tony(jb_t *e) {                                                  /* hat on all four, bursts, fast fills */
    int bal = e->ballad;
    ride_time(e, 100, 70, bal ? 32 : 0);
    if (bal) { hat_24(e, 64); return; }
    for (int b = 0; b < e->nb; b++) add(e, b * 12, I_DRUMS, D_HAT, 70 + (b & 1) * 10, 1);
    if ((e->bar + 1) % 8 == 0) {
        static const uint8_t pc[4] = {D_SNARE, D_TOMH, D_TOML, D_KICK};
        for (int t = 24, i = 0; t < 48; t += 3, i++) add(e, t, I_DRUMS, pc[(i >> 1) & 3], 52 + i * 5, 1);
        e->crash_next = 1; return;
    }
    if (chance(e, 320)) { int t = 12 * irand(e, 4) + 4; for (int k = 0; k < 3; k++) add(e, t + k * 3, I_DRUMS, D_SNARE, 40 + k * 14, 1); }
    if (chance(e, 220)) add(e, 4 + 8 * irand(e, 5), I_DRUMS, D_KICK, 60 + irand(e, 20), 1);
}
static void drums_haynes(jb_t *e) {                                                /* crisp snare snaps in odd places */
    int bal = e->ballad;
    for (int b = 0; b < 4; b++) {
        add(e, b * 12, I_DRUMS, D_RIDE, 94 + irand(e, 8) - (bal ? 32 : 0), 1);
        if ((b & 1) && chance(e, 800)) add(e, b * 12 + 8, I_DRUMS, D_RIDE, 66 - (bal ? 30 : 0), 1);
    }
    if (chance(e, 800)) hat_24(e, bal ? 60 : 82);
    if (bal) { if (chance(e, 300)) add(e, 36, I_DRUMS, D_STICK, 44, 1); return; }
    if ((e->bar + 1) % 8 == 0) {
        add(e, 32, I_DRUMS, D_SNARE, 70, 1); add(e, 36, I_DRUMS, D_SNARE, 84, 1); add(e, 38, I_DRUMS, D_SNARE, 60, 1);
        add(e, 40, I_DRUMS, D_TOMH, 82, 1); add(e, 44, I_DRUMS, D_KICK, 90, 1);
        e->crash_next = 1; return;
    }
    static const uint8_t ct[5] = {4, 8, 20, 32, 44};
    for (int i = 0; i < 5; i++) if (chance(e, 220)) { int v = 46 + irand(e, 46); add(e, ct[i], I_DRUMS, D_SNARE, v, 1); if (chance(e, 250)) add(e, ct[i] + 2, I_DRUMS, D_SNARE, v - 24, 1); }
    if (chance(e, 160)) add(e, chance(e, 500) ? 20 : 44, I_DRUMS, D_KICK, 76, 1);
}
static void drums_higgins(jb_t *e) {                                               /* buoyant, smiling ride, a light touch */
    static const uint8_t rt[6] = {0, 12, 20, 24, 36, 44}, rv[6] = {92, 90, 74, 96, 92, 76};
    int bal = e->ballad;
    for (int i = 0; i < 6; i++) add(e, rt[i], I_DRUMS, D_RIDE, rv[i] + irand(e, 6) - 3 - (bal ? 30 : 0), 1);
    hat_24(e, bal ? 62 : 80);
    if (bal) { if (chance(e, 300)) add(e, 36, I_DRUMS, D_STICK, 40, 1); return; }
    for (int b = 0; b < 4; b++) add(e, b * 12, I_DRUMS, D_KICK, 22, 1);
    if ((e->bar + 1) % 8 == 0) { for (int t = 32; t < 48; t += 4) add(e, t, I_DRUMS, D_SNARE, 40 + (t - 32) * 2, 1); e->crash_next = 1; return; }
    if (chance(e, 300)) add(e, 44, I_DRUMS, D_SNARE, 40 + irand(e, 10), 1);
    if (chance(e, 180)) add(e, 20, I_DRUMS, D_SNARE, 34 + irand(e, 10), 1);
}
static void drums_jojones(jb_t *e) {                                               /* Basie: the time on the hi-hat */
    int bal = e->ballad;
    for (int b = 0; b < 4; b++) {
        add(e, b * 12, I_DRUMS, D_HAT, ((b & 1) ? 96 : 72) - (bal ? 26 : 0), 1);
        if (b & 1) add(e, b * 12 + 8, I_DRUMS, D_HAT, 58 - (bal ? 20 : 0), 1);
    }
    if (bal) return;
    for (int b = 0; b < 4; b++) add(e, b * 12, I_DRUMS, D_KICK, 30, 1);           /* four on the floor, felt */
    if ((e->bar + 1) % 8 == 0) { for (int t = 36; t < 48; t += 4) add(e, t, I_DRUMS, D_SNARE, 64 + (t - 36) * 2, 1); e->crash_next = 1; return; }
    if (chance(e, 120)) add(e, 44, I_DRUMS, D_SNARE, 62, 1);
    if ((e->bar & 3) == 3 && chance(e, 400)) add(e, 44, I_DRUMS, D_KICK, 80, 1);
}
static void drums_dejohnette(jb_t *e) {                                            /* washy ride, toms and snare talking */
    int bal = e->ballad, soft = bal ? 30 : 0;
    ride_time(e, 86, 62, soft);
    for (int b = 0; b < 4; b++) if (chance(e, 250)) add(e, b * 12 + 4, I_DRUMS, D_RIDE, 50 + irand(e, 14) - soft, 1);
    if (chance(e, 200)) add(e, 0, I_DRUMS, D_CRASH, 40 + irand(e, 10) - soft / 2, 1);   /* the crash as a ride */
    if (chance(e, 700)) hat_24(e, bal ? 58 : 76);
    if (bal) return;
    if ((e->bar + 1) % 8 == 0) {
        static const uint8_t pc[4] = {D_SNARE, D_TOMH, D_TOML, D_KICK};
        for (int t = 24, i = 0; t < 48; t += 4, i++) add(e, t, I_DRUMS, pc[i & 3], 50 + i * 6, 1);
        e->crash_next = 1; return;
    }
    static const uint8_t pc[4] = {D_SNARE, D_KICK, D_TOMH, D_TOML};
    for (int t = 4; t < 48; t += 4) if (chance(e, 120)) add(e, t, I_DRUMS, pc[irand(e, 4)], 38 + irand(e, 34), 1);
}
static void drums_roach(jb_t *e) {                                                 /* melodic toms, the hat on 2 and 4 */
    int bal = e->ballad, soft = bal ? 30 : 0, end = e->tpbar;
    ride_time(e, 92, 66, soft);
    hat_24(e, bal ? 62 : 86);
    if (bal) { if ((e->bar & 3) == 0) add(e, 0, I_DRUMS, D_TOML, 44, 1); return; }
    add(e, 0, I_DRUMS, D_KICK, 46, 1);
    if ((e->bar + 1) % 8 == 0) {                                                  /* down the kit */
        for (int t = end - 24, i = 0; t < end; t += 4, i++) add(e, t, I_DRUMS, i < 2 ? D_SNARE : i < 4 ? D_TOMH : D_TOML, 58 + i * 6, 1);
        e->crash_next = 1; return;
    }
    if ((e->bar & 3) == 3) {                                                      /* a little tom melody */
        static const int8_t ph[6][2] = {{-24, D_TOMH}, {-20, D_TOMH}, {-16, D_TOML}, {-12, D_SNARE}, {-8, D_TOML}, {-4, D_KICK}};
        static const uint8_t pv[6] = {72, 58, 76, 66, 80, 78};
        for (int i = 0; i < 6; i++) add(e, end + ph[i][0], I_DRUMS, ph[i][1], pv[i], 1);
        return;
    }
    static const uint8_t ct[4] = {8, 20, 32, 44}, pc[3] = {D_SNARE, D_TOMH, D_TOML};
    for (int i = 0; i < 4; i++) if (ct[i] < end && chance(e, 170)) add(e, ct[i], I_DRUMS, pc[irand(e, 3)], 42 + irand(e, 30), 1);
}
/* Airto: Brazilian percussion over a light kit, straight eighths in any feel */
static void drums_airto(jb_t *e) {
    int bal = e->ballad, end = e->tpbar, ab = e->bar & 1;
    for (int t = 0; t < end; t += bal ? 12 : 6) {
        int off = (t % 12) != 0;
        add(e, t, I_DRUMS, D_SHAKER, (off ? 90 : 50) + irand(e, 10) - (bal ? 14 : 0), 1);
        if (!bal && chance(e, 220)) add(e, t + 3, I_DRUMS, D_SHAKER, 36 + irand(e, 10), 1);
    }
    static const uint8_t CG[2][4][2] = {{{18, 84}, {42, 96}, {0, 0}, {0, 0}}, {{6, 58}, {12, 74}, {30, 88}, {36, 98}}};
    for (int i = 0; i < 4; i++) if (CG[ab][i][1] && CG[ab][i][0] < end) add(e, CG[ab][i][0], I_DRUMS, D_CONGA, CG[ab][i][1] - (bal ? 26 : 0) + irand(e, 8), 1);
    add(e, 0, I_DRUMS, D_TOML, bal ? 40 : 54, 1);                                 /* surdo: soft on 1, strong on 3 */
    if (e->nb == 4) add(e, 24, I_DRUMS, D_TOML, bal ? 52 : 74, 1);
    if (bal) return;
    add(e, 0, I_DRUMS, D_KICK, 50, 1);
    for (int i = 0; i < 3; i++) if (CLAVE[ab][i] < end) add(e, CLAVE[ab][i], I_DRUMS, D_STICK, 58 + irand(e, 10), 1);
    if ((e->bar + 1) % 8 == 0)
        for (int t = end - 12, i = 0; t < end; t += 3, i++) add(e, t, I_DRUMS, (i & 1) ? D_TOMH : D_CONGA, 64 + i * 8, 1);
}
/* every other drummer in the BOSSA feel: straight eighths, cross-stick clave, kick on 1, the and of 2, 3, the and of 4 */
static void drums_bossa(jb_t *e) {
    int st = e->sty[I_DRUMS], end = e->tpbar, ab = e->bar & 1;
    int ride = st == DR_ELVIN || st == DR_MOTIAN || st == DR_TONY || st == DR_DEJOHNETTE;
    for (int t = 0; t < end; t += 6) {
        int on = (t % 12) == 0;
        add(e, t, I_DRUMS, ride ? D_RIDE : D_HAT, (ride ? (on ? 72 : 58) : (on ? 64 : 50)) + irand(e, 8), 1);
    }
    for (int i = 0; i < 3; i++) if (CLAVE[ab][i] < end)
        add(e, CLAVE[ab][i], I_DRUMS, st == DR_ROACH && i == 1 ? D_TOMH : D_STICK, 64 + irand(e, 10), 1);
    for (int b = 0; b < e->nb; b += 2) {
        add(e, b * 12, I_DRUMS, D_KICK, 60, 1);
        if (b * 12 + 18 < end) add(e, b * 12 + 18, I_DRUMS, D_KICK, 42, 1);
    }
    if ((e->bar + 1) % 8 == 0)
        for (int t = end - 12, i = 0; t < end; t += 3, i++)
            add(e, t, I_DRUMS, st == DR_ROACH || st == DR_TONY ? (i < 2 ? D_TOMH : D_TOML) : D_SNARE, 36 + i * 9, 1);
}
static void gen_drums(jb_t *e) {
    int st = e->sty[I_DRUMS] % NSTY;
    if (e->crash_next) { if (st != DR_AIRTO && !e->bossa) add(e, 0, I_DRUMS, D_CRASH, 90, 1); add(e, 0, I_DRUMS, D_KICK, 80, 1); e->crash_next = 0; }
    if (st == DR_AIRTO) { drums_airto(e); return; }
    if (e->bossa) { drums_bossa(e); return; }
    if (st == DR_BLAKEY) { drums_blakey(e); return; }
    if (st == DR_ROACH) { drums_roach(e); return; }
    if (e->nb == 3) { drums_waltz(e); return; }
    switch (st) {
    case DR_PHILLY: drums_philly(e); break;
    case DR_ELVIN: drums_elvin(e); break;
    case DR_MOTIAN: drums_motian(e); break;
    case DR_TONY: drums_tony(e); break;
    case DR_HAYNES: drums_haynes(e); break;
    case DR_HIGGINS: drums_higgins(e); break;
    case DR_JOJONES: drums_jojones(e); break;
    case DR_DEJOHNETTE: drums_dejohnette(e); break;
    default: drums_cobb(e); break;
    }
}

/* bass --------------------------------------------------------------------------------------------- */
static int fold(int n, int lo, int hi);
static void bass_waltz_two(jb_t *e, const chord_t *c, int lo, int hi) {   /* 3/4 ballad: 1 (held), then 3 */
    int last = e->bass_last, n0 = nearest(c[0].root, last, lo, hi), n2;
    if (!ceq(c[2], c[0])) n2 = nearest(c[2].root, n0, lo, hi);
    else if (chance(e, 500)) n2 = nearest(pcof(c[0].root + fifth(c[0].q)), n0, lo, hi);
    else { int tgt = nearest(c[3].root, n0, lo, hi); n2 = tgt + (chance(e, 600) ? -1 : 1); }
    add(e, 0, I_BASS, n0, 100, 22); add(e, 24, I_BASS, fold(n2, lo, hi), 88, 10); e->bass_last = fold(n2, lo, hi);
}
static void bass_two_feel(jb_t *e, const chord_t *c, int lo, int hi) {
    int last = e->bass_last, n0 = nearest(c[0].root, last, lo, hi);
    int n2 = ceq(c[2], c[0]) ? nearest(pcof(c[0].root + fifth(c[0].q)), n0, lo, hi) : nearest(c[2].root, n0, lo, hi);
    add(e, 0, I_BASS, n0, 100, 22); add(e, 24, I_BASS, n2, 92, 22); e->bass_last = n2;
}
static int fold(int n, int lo, int hi) { while (n < lo) n += 12; while (n > hi) n -= 12; return n; }
static void bass_chambers(jb_t *e, const chord_t *c) {                             /* the original walking line */
    const int lo = 28, hi = 50; int last = e->bass_last;
    for (int b = 0; b < e->nb; b++) {
        int start = b == 0 || !ceq(c[b], c[b - 1]);
        int k = b + 1; while (k < e->nb && ceq(c[k], c[b])) k++;
        int n;
        if (start) {
            n = nearest(c[b].root, last, lo, hi);
            if (n > 45 && chance(e, 500) && n - 12 >= lo) n -= 12;
        } else if (k == b + 1) {                                                  /* approach the next chord */
            int tgt = nearest(c[k].root, last, lo, hi), r = irand(e, 100);
            if (r < 45) n = tgt + (last > tgt ? 1 : -1);
            else if (r < 70) n = tgt - 1;
            else if (r < 85) n = tgt + 1;
            else { n = tgt + 7; if (n > hi) n = tgt - 5; }
            if (n == last) n = (n == tgt - 1) ? tgt + 1 : tgt - 1;
        } else {
            int tgt = nearest(c[k].root, last, lo, hi);
            if (chance(e, 500)) {
                int ivs[3] = {third(c[b].q), fifth(c[b].q), seventh(c[b].q)};
                n = nearest(pcof(c[b].root + ivs[irand(e, 3)]), last, lo, hi);
                if (n == last) n = nearest(pcof(c[b].root + fifth(c[b].q)), last + 4, lo, hi);
            } else {
                int dir = tgt > last ? 1 : -1; if (tgt == last) dir = last > 40 ? -1 : 1;
                n = scale_step(c[b], last, dir);
            }
        }
        n = fold(n, lo, hi);
        add(e, b * 12, I_BASS, n, b == 0 ? 104 : 88 + irand(e, 10), 11);
        last = n;
    }
    e->bass_last = last;
}
static void bass_brown(jb_t *e, const chord_t *c) {                                /* roots and fifths, approach from below */
    const int lo = 28, hi = 48; int last = e->bass_last;
    for (int b = 0; b < e->nb; b++) {
        int start = b == 0 || !ceq(c[b], c[b - 1]), into = 0;
        int k = b + 1; while (k < e->nb && ceq(c[k], c[b])) k++;
        for (int j = b; j > 0 && ceq(c[j - 1], c[b]); j--) into++;               /* beats since the chord began */
        int n;
        if (start) n = nearest(c[b].root, last, lo, hi);
        else if (k == b + 1) {
            int tgt = nearest(c[k].root, last, lo, hi), r = irand(e, 100);
            n = r < 60 ? tgt - 1 : r < 85 ? tgt + 1 : tgt + 7;
        } else if (into == 2) n = nearest(pcof(c[b].root + fifth(c[b].q)), last, lo, hi);
        else n = nearest(pcof(c[b].root + (chance(e, 500) ? third(c[b].q) : 0)), last + (chance(e, 500) ? 5 : -5), lo, hi);
        n = fold(n, lo, hi);
        if (b > 0 && chance(e, 160)) add(e, b * 12 - 4, I_BASS, n, 58, 2);       /* triplet pickup into the beat */
        add(e, b * 12, I_BASS, n, b == 0 ? 112 : 98 + irand(e, 8), 11);
        last = n;
    }
    e->bass_last = last;
}
static void bass_carter(jb_t *e, const chord_t *c) {                               /* chromatic, wide, legato */
    const int lo = 28, hi = 55; int last = e->bass_last;
    for (int b = 0; b < e->nb; b++) {
        int start = b == 0 || !ceq(c[b], c[b - 1]);
        int k = b + 1; while (k < e->nb && ceq(c[k], c[b])) k++;
        int tgt = nearest(c[k].root, last, lo, hi), n;
        if (start) {
            int iv = chance(e, 160) ? third(c[b].q) : chance(e, 100) ? fifth(c[b].q) : 0;
            n = nearest(pcof(c[b].root + iv), last + (last < 38 ? 7 : -3), lo, hi);
        } else if (k == b + 1) n = tgt + (chance(e, 500) ? 1 : -1);
        else if (chance(e, 850)) { int dir = tgt >= last ? 1 : -1; if (iabs(tgt - last) < 3) dir = -dir; n = last + dir; }   /* chromatic run */
        else n = scale_step(c[b], last, tgt >= last ? 2 : -2);
        n = fold(n, lo, hi);
        add(e, b * 12, I_BASS, n, b == 0 ? 100 : 90 + irand(e, 10), 12);
        last = n;
    }
    e->bass_last = last;
}
static int bass_tone(jb_t *e, chord_t c, int last, int lo, int hi) {             /* a melodic chord or scale tone */
    int r = irand(e, 100), n;
    if (r < 55) { int ivs[4] = {0, third(c.q), fifth(c.q), seventh(c.q)}; n = nearest(pcof(c.root + ivs[irand(e, 4)]), last + irand(e, 9) - 4, lo, hi); }
    else n = scale_step(c, last, chance(e, 500) ? 1 : -1);
    if (n == last) n = scale_step(c, last, last > (lo + hi) / 2 ? -2 : 2);
    return fold(n, lo, hi);
}
static void bass_lafaro(jb_t *e, const chord_t *c) {                               /* conversational: triplets and space */
    const int lo = 33, hi = 60; int last = e->bass_last;
    if (last < lo) last = lo + 7;
    for (int b = 0; b < e->nb; b++) {
        int start = b == 0 || !ceq(c[b], c[b - 1]), r = irand(e, 100), t = b * 12;
        if (start && b == 0) { int n = nearest(pcof(c[b].root + (chance(e, 300) ? third(c[b].q) : 0)), last, lo, hi);
                               add(e, t, I_BASS, n, 100, e->ballad ? 22 : 11); last = n; if (e->ballad) b++; continue; }
        if (e->ballad) { if (chance(e, 450)) { last = bass_tone(e, c[b], last, lo, hi); add(e, t + (chance(e, 500) ? 8 : 0), I_BASS, last, 84, 14); } continue; }
        if (r < 40 || start) { last = bass_tone(e, c[b], last, lo, hi); add(e, t, I_BASS, last, 92 + irand(e, 8), 11); }
        else if (r < 55) continue;                                                 /* space */
        else if (r < 82) { for (int s = 0; s < 2; s++) { last = bass_tone(e, c[b], last, lo, hi); add(e, t + s * 8, I_BASS, last, 86 + irand(e, 10), s ? 4 : 7); } }
        else { for (int s = 0; s < 3; s++) { last = bass_tone(e, c[b], last, lo, hi); add(e, t + s * 4, I_BASS, last, 82 + irand(e, 10), 3); } }
    }
    e->bass_last = last;
}
/* Mingus: an anchored two-bar blues riff; chord changes always take priority. */
static void bass_mingus(jb_t *e, const chord_t *c) {
    int last = e->bass_last;
    for (int b = 0; b < e->nb; b++) {
        int changed = b == 0 || !ceq(c[b], c[b - 1]);
        int iv = changed || (e->bar & 1) == 0 ? 0 : b == 1 ? third(c[b].q) : fifth(c[b].q);
        int n = nearest(pcof(c[b].root + iv), changed ? 33 : last, 28, 52);
        /* A short blue-note pickup resolves immediately into the chord's third. */
        if (!e->ballad && !changed && b == 1 && (e->bar & 1))
            add(e, b * 12 - 4, I_BASS, fold(n - 1, 28, 52), 78, 3);
        if (e->ballad && b == 1 && !changed) continue;
        int dur = e->ballad ? (b == 0 && ceq(c[0], c[1]) ? 22 : 10) : 8;
        add(e, b * 12, I_BASS, n, b == 0 ? 116 : 100 + (b & 1) * 8, dur);
        last = n;
        if (!e->ballad && b == e->nb - 1 && (e->bar & 1)) {
            int target = nearest(c[e->nb].root, last, 28, 52);
            last = fold(target - 1, 28, 52);
            add(e, b * 12 + 8, I_BASS, last, 88, 3);
        }
    }
    e->bass_last = last;
}
/* ---- 1.5 bass players */
static void bass_samjones(jb_t *e, const chord_t *c) {                             /* deep, on top of the beat, roots on 1 and 3 */
    const int lo = 28, hi = 48; int last = e->bass_last;
    for (int b = 0; b < e->nb; b++) {
        int start = b == 0 || !ceq(c[b], c[b - 1]), into = 0, n;
        int k = b + 1; while (k < e->nb && ceq(c[k], c[b])) k++;
        for (int j = b; j > 0 && ceq(c[j - 1], c[b]); j--) into++;
        int tgt = nearest(c[k].root, last, lo, hi);
        if (start) n = nearest(c[b].root, last, lo, hi);
        else if (k == b + 1) n = chance(e, 600) ? tgt + 1 : tgt - 1;              /* from above, mostly */
        else if (into == 2) n = chance(e, 500) ? nearest(c[b].root, last + 6, lo, hi) : nearest(pcof(c[b].root + fifth(c[b].q)), last, lo, hi);
        else n = scale_step(c[b], last, tgt >= last ? 1 : -1);
        n = fold(n, lo, hi);
        add(e, b * 12, I_BASS, n, b == 0 ? 110 : 98 + irand(e, 8), 11);
        last = n;
    }
    e->bass_last = last;
}
static void bass_haden(jb_t *e, const chord_t *c) {                                /* simple, roots and fifths, room to breathe */
    const int lo = 28, hi = 45; int last = e->bass_last;
    if (chance(e, 250)) { if (e->nb == 3) bass_waltz_two(e, c, lo, hi); else bass_two_feel(e, c, lo, hi); return; }
    for (int b = 0; b < e->nb; b++) {
        int start = b == 0 || !ceq(c[b], c[b - 1]);
        int k = b + 1; while (k < e->nb && ceq(c[k], c[b])) k++;
        int n;
        if (start) n = nearest(c[b].root, last, lo, hi);
        else if (k == b + 1) { int tgt = nearest(c[k].root, last, lo, hi); n = chance(e, 500) ? tgt - 1 : tgt + 2; }
        else n = nearest(pcof(c[b].root + (chance(e, 600) ? fifth(c[b].q) : 0)), last, lo, hi);
        n = fold(n, lo, hi);
        add(e, b * 12, I_BASS, n, b == 0 ? 102 : 92 + irand(e, 6), 11);
        last = n;
    }
    e->bass_last = last;
}
static void bass_garrison(jb_t *e, const chord_t *c) {                             /* pedal points, double stops */
    if (!ceq(c[0], c[e->nb - 1])) { bass_chambers(e, c); return; }
    int r = nearest(c[0].root, 33, 28, 40), last = r;
    for (int b = 0; b < e->nb; b++) {
        int n = (b == 2 && chance(e, 450)) ? r + 12 : (b == e->nb - 1 && chance(e, 300)) ? r + 7 : r;
        add(e, b * 12, I_BASS, n, b == 0 ? 108 : 94 + irand(e, 8), 11);
        if (b == 0 && chance(e, 400)) add(e, 0, I_BASS, r + 7, 80, 11);           /* the fifth on top */
        last = n;
    }
    e->bass_last = last;
}
static void bass_page(jb_t *e, const chord_t *c) {                                 /* Basie: even four, up the chord */
    const int lo = 28, hi = 45; int last = e->bass_last;
    for (int b = 0; b < e->nb; b++) {
        int into = 0; for (int j = b; j > 0 && ceq(c[j - 1], c[b]); j--) into++;
        int k = b + 1; while (k < e->nb && ceq(c[k], c[b])) k++;
        int n;
        if (k == b + 1 && into > 0) n = nearest(c[k].root, last, lo, hi) - 1;
        else { int ivs[4] = {0, third(c[b].q), fifth(c[b].q), c[b].q == Q_DOM7 || c[b].q == Q_MAJ6 || c[b].q == Q_MIN6 ? 9 : seventh(c[b].q)};
               n = into == 0 ? nearest(c[b].root, last, lo, hi) : fold(nearest(c[b].root, last, lo, hi - 9) + ivs[into & 3], lo, hi); }
        n = fold(n, lo, hi);
        add(e, b * 12, I_BASS, n, 96 + irand(e, 4), 11);
        last = n;
    }
    e->bass_last = last;
}
static void bass_pettiford(jb_t *e, const chord_t *c) {                            /* melodic, higher, bebop pickups */
    const int lo = 31, hi = 55; int last = e->bass_last;
    if (last < lo) last = lo + 5;
    for (int b = 0; b < e->nb; b++) {
        int start = b == 0 || !ceq(c[b], c[b - 1]);
        int k = b + 1; while (k < e->nb && ceq(c[k], c[b])) k++;
        int tgt = nearest(c[k].root, last, lo, hi), n;
        if (start) n = nearest(pcof(c[b].root + (chance(e, 250) ? third(c[b].q) : 0)), last, lo, hi);
        else if (k == b + 1) n = tgt + (last > tgt ? 1 : -1);
        else n = bass_tone(e, c[b], last, lo, hi);
        n = fold(n, lo, hi);
        if (b > 0 && chance(e, 250)) add(e, b * 12 - 4, I_BASS, fold(n + (chance(e, 500) ? 1 : -1), lo, hi), 64, 2);
        add(e, b * 12, I_BASS, n, b == 0 ? 104 : 92 + irand(e, 10), 11);
        last = n;
    }
    e->bass_last = last;
}
/* BOSSA: root on 1, the fifth on the and of 2 tied into 3, then the next root (Haden: just the roots) */
static void bass_bossa(jb_t *e, const chord_t *c) {
    const int lo = 28, hi = 48; int last = e->bass_last, st = e->sty[I_BASS];
    for (int h = 0; h * 24 < e->tpbar; h++) {
        chord_t ch = c[h * 2]; int same = h > 0 && ceq(ch, c[0]);
        int n = same ? nearest(pcof(ch.root + fifth(ch.q)), last - 5, lo, hi) : nearest(ch.root, last, lo, hi);
        add(e, h * 24, I_BASS, n, h ? 92 : 102, 16); last = n;
        if (st == BS_HADEN || h * 24 + 18 >= e->tpbar) continue;
        int m = same ? nearest(ch.root, last, lo, hi) : nearest(pcof(ch.root + fifth(ch.q)), n, lo, hi);
        add(e, h * 24 + 18, I_BASS, m, 82, 5); last = m;
    }
    e->bass_last = last;
}
static void gen_bass(jb_t *e) {
    chord_t c[5]; for (int b = 0; b < 4; b++) c[b] = cur_chord(e, b);
    c[e->nb] = next_chord(e);                                                     /* c[nb]: the next bar's chord */
    int st = e->sty[I_BASS] % NSTY;
    if (e->bossa) { bass_bossa(e, c); return; }
    if (st == BS_MINGUS) { bass_mingus(e, c); return; }
    if (e->ballad && st != BS_LAFARO) { if (e->nb == 3) bass_waltz_two(e, c, 28, 50); else bass_two_feel(e, c, 28, st == BS_CARTER || st == BS_PETTIFORD ? 55 : 50); return; }
    switch (st) {
    case BS_BROWN: bass_brown(e, c); break;
    case BS_CARTER: bass_carter(e, c); break;
    case BS_LAFARO: bass_lafaro(e, c); break;
    case BS_SAMJONES: bass_samjones(e, c); break;
    case BS_HADEN: bass_haden(e, c); break;
    case BS_GARRISON: bass_garrison(e, c); break;
    case BS_PAGE: bass_page(e, c); break;
    case BS_PETTIFORD: bass_pettiford(e, c); break;
    default: bass_chambers(e, c); break;
    }
}

/* piano -------------------------------------------------------------------------------------------- */
static void voicing_in(jb_t *e, chord_t c, int out[4], int lo, int hi) {         /* rootless A / B, voice-led */
    int best[4] = {60, 64, 67, 71}, bs = 1 << 30;
    for (int form = 0; form < 2; form++) {
        const int8_t *iv = form ? VB[c.q] : VA[c.q];
        for (int oct = 3; oct <= 6; oct++) {
            int n[4], base = c.root + 12 * oct, s = 0;
            for (int i = 0; i < 4; i++) n[i] = base + iv[i];
            if (n[0] < lo || n[0] > hi) continue;
            if (e->pv_ok) for (int i = 0; i < 4; i++) s += iabs(n[i] - e->pv[i]); else s = iabs(n[0] - (lo + hi) / 2);
            s = s * 4 + irand(e, 3);
            if (s < bs) { bs = s; for (int i = 0; i < 4; i++) best[i] = n[i]; }
        }
    }
    for (int i = 0; i < 4; i++) { out[i] = best[i]; e->pv[i] = best[i]; }
    e->pv_ok = 1;
}
static void voicing(jb_t *e, chord_t c, int out[4]) { voicing_in(e, c, out, 46, 60); }
static void quartal(jb_t *e, chord_t c, int out[4], int lo, int hi) {           /* stacked fourths from a chord tone */
    int ivs[3] = {2, third(c.q), fifth(c.q)}, iv = ivs[irand(e, 3)];
    int ref = e->pv_ok ? e->pv[0] : (lo + hi) / 2;
    int n = nearest(pcof(c.root + iv), ref, lo, hi);
    out[0] = n;
    for (int i = 1; i < 4; i++) {
        int m = out[i - 1] + 5;
        if (!in_scale(c, m)) m = in_scale(c, m + 1) ? m + 1 : m - 1;
        out[i] = m;
    }
    for (int i = 0; i < 4; i++) e->pv[i] = out[i];
    e->pv_ok = 1;
}
static void piano_hit(jb_t *e, int tick, chord_t c, int dur, int vel) {
    int n[4], st = e->sty[I_PIANO];
    if (st == 2) {                                                                /* McCoy: fourths + left-hand root and fifth */
        quartal(e, c, n, 53, 64);
        if (tick < e->tpbar - 4 && dur > 8) { int r = nearest(c.root, 40, 33, 45); add(e, tick, I_PIANO, r, vel + 6, dur); add(e, tick, I_PIANO, r + 7, vel, dur); }
    } else if (st == 3) {                                                         /* Herbie: high, sometimes in fourths */
        if (chance(e, 400)) quartal(e, c, n, 57, 68); else voicing_in(e, c, n, 52, 64);
    } else if (st == 4) {                                                         /* Monk: open shells with a tight upper rub */
        int r = nearest(c.root, 51, 45, 56);
        n[0] = r; n[1] = r + seventh(c.q);
        n[2] = r + third(c.q) + 12;
        n[3] = n[2] + 1;                                                         /* deliberate minor-second dissonance */
        for (int i = 0; i < 4; i++) e->pv[i] = n[i];
        e->pv_ok = 1;
    } else if (st == PN_BASIE) {                                                  /* Basie: a small plink up high, with the octave */
        n[0] = nearest(pcof(c.root + third(c.q)), e->pv_ok ? e->pv[0] : 66, 62, 71);
        n[1] = n[0] + pcof(fifth(c.q) - third(c.q)); n[2] = n[0] + pcof(seventh(c.q) - third(c.q)); n[3] = n[0] + 12;
        for (int i = 0; i < 4; i++) e->pv[i] = n[i];
        e->pv_ok = 1;
    } else if (st == PN_JAMAL) {                                                  /* Jamal: block chords, higher */
        voicing_in(e, c, n, 53, 65);
    } else if (st == PN_SILVER) {                                                 /* Silver: rootless chord + left-hand punch */
        voicing(e, c, n);
        if (dur > 4) { int r = nearest(c.root, 38, 31, 43); add(e, tick, I_PIANO, r, vel + 8, dur); add(e, tick, I_PIANO, r + 7, vel + 2, dur); }
    } else voicing(e, c, n);
    for (int i = 0; i < 4; i++) add(e, tick, I_PIANO, n[i], vel + (i == 3 ? 8 : 0) + irand(e, 6), dur);
}
/* patterns: {hits, tick, tick}; hits at 44 anticipate the next bar's chord */
static const uint8_t PAT_EVANS[10][3] = {{2, 0, 20}, {1, 20, 0}, {2, 8, 32}, {2, 12, 32}, {2, 0, 32},
                                         {2, 20, 44}, {2, 32, 44}, {1, 0, 0}, {1, 32, 0}, {0, 0, 0}};
static const uint8_t PAT_GARLAND[10][3] = {{2, 20, 44}, {2, 20, 44}, {2, 20, 44}, {2, 0, 20}, {2, 0, 20},
                                           {1, 20, 0}, {2, 8, 32}, {2, 20, 32}, {1, 44, 0}, {2, 8, 44}};
static const uint8_t PAT_MCCOY[10][3] = {{2, 0, 20}, {2, 0, 32}, {1, 0, 0}, {2, 0, 24}, {2, 0, 44},
                                         {1, 0, 0}, {2, 0, 20}, {2, 8, 32}, {1, 44, 0}, {2, 0, 32}};
static const uint8_t PAT_HERBIE[10][3] = {{1, 16, 0}, {1, 28, 0}, {2, 8, 40}, {1, 40, 0}, {2, 16, 44},
                                          {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 4, 0}, {2, 20, 36}};
static const uint8_t PAT_MONK[10][3] = {{1, 4, 0}, {2, 0, 28}, {1, 20, 0}, {2, 16, 36}, {0, 0, 0},
                                        {2, 8, 24}, {1, 40, 0}, {2, 12, 32}, {0, 0, 0}, {2, 4, 44}};
static const uint8_t PAT_KELLY[10][3] = {{2, 20, 44}, {2, 8, 20}, {2, 0, 20}, {2, 20, 32}, {1, 20, 0},
                                         {2, 32, 44}, {2, 8, 44}, {2, 20, 44}, {1, 44, 0}, {2, 12, 32}};
static const uint8_t PAT_BASIE[10][3] = {{1, 20, 0}, {1, 44, 0}, {0, 0, 0}, {1, 32, 0}, {2, 20, 44},
                                         {0, 0, 0}, {1, 8, 0}, {1, 40, 0}, {0, 0, 0}, {1, 44, 0}};
static const uint8_t PAT_JAMAL[10][3] = {{2, 0, 24}, {1, 20, 0}, {0, 0, 0}, {2, 8, 32}, {2, 0, 24},
                                         {1, 44, 0}, {0, 0, 0}, {2, 20, 44}, {1, 0, 0}, {2, 0, 32}};
static const uint8_t PAT_SILVER[10][3] = {{2, 0, 20}, {2, 20, 44}, {2, 8, 32}, {2, 0, 32}, {2, 20, 32},
                                          {2, 0, 44}, {2, 12, 32}, {1, 20, 0}, {2, 8, 44}, {2, 0, 24}};
/* 3/4: hits at 32 anticipate the next bar */
static const uint8_t PAT3[NPIA][8][3] = {
    {{2, 0, 20}, {1, 20, 0}, {2, 12, 24}, {2, 0, 24}, {1, 0, 0}, {2, 20, 32}, {1, 8, 0}, {0, 0, 0}},          /* Evans */
    {{2, 20, 32}, {2, 20, 32}, {2, 8, 20}, {1, 20, 0}, {2, 0, 20}, {1, 32, 0}, {2, 12, 32}, {2, 20, 32}},     /* Garland */
    {{2, 0, 24}, {1, 0, 0}, {2, 0, 20}, {2, 0, 24}, {1, 0, 0}, {2, 0, 32}, {2, 8, 24}, {1, 0, 0}},           /* McCoy */
    {{1, 16, 0}, {1, 28, 0}, {2, 8, 28}, {0, 0, 0}, {0, 0, 0}, {1, 4, 0}, {2, 20, 32}, {1, 16, 0}},          /* Herbie */
    {{1, 4, 0}, {2, 0, 28}, {1, 20, 0}, {2, 8, 24}, {0, 0, 0}, {1, 16, 0}, {2, 12, 32}, {0, 0, 0}},         /* Monk */
    {{2, 20, 32}, {2, 8, 20}, {2, 0, 20}, {1, 20, 0}, {2, 12, 32}, {2, 20, 32}, {1, 32, 0}, {2, 8, 24}},     /* Kelly */
    {{1, 20, 0}, {0, 0, 0}, {1, 32, 0}, {1, 8, 0}, {0, 0, 0}, {2, 20, 32}, {1, 28, 0}, {0, 0, 0}},           /* Basie */
    {{2, 0, 24}, {1, 0, 0}, {0, 0, 0}, {2, 8, 24}, {1, 20, 0}, {2, 0, 32}, {0, 0, 0}, {1, 0, 0}},            /* Jamal */
    {{2, 0, 20}, {2, 20, 32}, {2, 8, 24}, {2, 0, 24}, {1, 20, 0}, {2, 12, 32}, {2, 0, 32}, {2, 8, 20}},      /* Silver */
    {{2, 0, 20}, {1, 20, 0}, {2, 12, 24}, {2, 0, 24}, {1, 0, 0}, {2, 20, 32}, {1, 8, 0}, {0, 0, 0}}};       /* Garner (3/4 uses beats) */
static void gen_piano(jb_t *e) {
    static const uint8_t (*const PATS[NPIA])[3] = {PAT_EVANS, PAT_GARLAND, PAT_MCCOY, PAT_HERBIE, PAT_MONK,
                                                   PAT_KELLY, PAT_BASIE, PAT_JAMAL, PAT_SILVER, PAT_GARLAND};
    static const int16_t SHORT[NPIA] = {550, 850, 250, 700, 900, 800, 950, 450, 750, 900},
                         VLO[NPIA] = {56, 62, 70, 50, 66, 60, 56, 48, 66, 56}, VRNG[NPIA] = {18, 16, 18, 18, 20, 16, 14, 30, 16, 14};
    int st = e->sty[I_PIANO] % NPIA;
    chord_t c0 = cur_chord(e, 0), c2 = cur_chord(e, 2);
    int two = !ceq(c0, c2);
    if (e->bossa) {                                                               /* BOSSA: the clave, short chords */
        const uint8_t *cl = CLAVE[e->bar & 1];
        for (int i = 0; i < 3; i++) {
            int t = cl[i]; if (t >= e->tpbar || (t == 0 && e->skip_down)) continue;
            piano_hit(e, t, cur_chord(e, t / 12), 5 + irand(e, 4), VLO[st] - 4 + irand(e, 12));
        }
        e->skip_down = 0;
        if ((e->bar & 1) && chance(e, 600)) { piano_hit(e, e->tpbar - 6, next_chord(e), 10, VLO[st] + irand(e, 10)); e->skip_down = 1; }
        return;
    }
    if (st == PN_GARNER && !e->ballad) {                                          /* Garner: the left hand strums every beat */
        for (int b = 0; b < e->nb; b++) {
            if (b == 0 && e->skip_down) continue;
            piano_hit(e, b * 12, cur_chord(e, b), 6, 52 + ((b & 1) ? 6 : 0) + irand(e, 6));
        }
        if ((e->bar & 3) == 3) piano_hit(e, e->tpbar - 4, next_chord(e), 8, 62);    /* a late right-hand chord into the next bar */
        e->skip_down = (e->bar & 3) == 3;
        return;
    }
    if (e->ballad) {
        if (st == 4) {                                                            /* Monk ballad: space, then a brief response */
            if (!e->skip_down) piano_hit(e, 4, c0, 8, 58 + irand(e, 10));
            if (two || (e->bar & 1)) piano_hit(e, 24, c2, 6, 62 + irand(e, 10));
        } else if (st == 3) {                                                            /* Herbie ballad: one placed chord, maybe two */
            if (!e->skip_down) piano_hit(e, chance(e, 500) ? 0 : 8, c0, 20, 48 + irand(e, 10));
            if (two || chance(e, 300)) piano_hit(e, 28, cur_chord(e, 2), 18, 46 + irand(e, 10));
        } else {
            if (!e->skip_down) piano_hit(e, 0, c0, two ? 22 : e->tpbar - 2, 52 + irand(e, 10) + (st == 2 ? 8 : 0));
            if (two) piano_hit(e, 24, c2, e->tpbar - 26, 50 + irand(e, 10));
        }
        e->skip_down = 0; return;
    }
    const int ant = e->tpbar - 4;                                                 /* the and of the last beat */
    int pi = e->nb == 3 ? irand(e, 8) : irand(e, 10);
    if (st == PN_JAMAL && (e->bar & 1)) pi = e->ppat;                             /* Jamal: a two-bar vamp */
    e->ppat = pi;
    const uint8_t *p = e->nb == 3 ? PAT3[st][pi % 8] : PATS[st][pi];
    if (p[0] == 0 && two) p = e->nb == 3 ? PAT3[0][2] : PATS[st][0][0] ? PATS[st][0] : PAT_EVANS[4];
    int on[3], n = 0, anticip = 0;
    for (int i = 0; i < p[0]; i++) on[n++] = p[1 + i];
    if (two) {                                                                    /* the second chord gets played */
        int has = 0; for (int i = 0; i < n; i++) if (on[i] >= 24 && on[i] < ant) has = 1;
        if (!has) { if (n == 2 && on[1] == ant) { on[2] = ant; on[1] = 24; n = 3; } else on[n++] = 24; }
        for (int i = 1; i < n; i++) for (int j = i; j > 0 && on[j] < on[j - 1]; j--) { int t = on[j]; on[j] = on[j - 1]; on[j - 1] = t; }
    }
    for (int i = 0; i < n; i++) {
        int t = on[i];
        if (t == 0 && e->skip_down) continue;
        chord_t c = t >= ant ? next_chord(e) : cur_chord(e, t / 12);
        int next = i + 1 < n ? on[i + 1] : e->tpbar;
        int dur = chance(e, SHORT[st]) ? 4 + irand(e, 4) : next - t - 1;
        if (t >= ant) { anticip = 1; dur = st == PN_GARLAND || st == PN_KELLY || st == PN_BASIE ? 6 : 14; }
        if (dur < 3) dur = 3;
        piano_hit(e, t, c, dur, VLO[st] + irand(e, VRNG[st]));
    }
    e->skip_down = anticip;
}

/* guitar ------------------------------------------------------------------------------------------- */
static void gtr_freddie(jb_t *e) {                                                 /* the original: shells, 4 to the bar */
    for (int b = 0; b < e->nb; b += e->ballad ? 2 : 1) {
        chord_t c = cur_chord(e, b);
        int r = nearest(c.root, 45, 40, 51), vel = 50 + irand(e, 8) + ((b & 1) ? 4 : 0), d = e->ballad ? 20 : 5;
        add(e, b * 12, I_GUITAR, r, vel, d);
        add(e, b * 12, I_GUITAR, r + seventh(c.q), vel, d);
        add(e, b * 12, I_GUITAR, r + third(c.q) + 12, vel, d);
    }
}
static int ninth_or_13(chord_t c) { return (c.q == Q_DOM7 || c.q == Q_MAJ6 || c.q == Q_MIN6) ? 9 : (c.q == Q_HALFDIM || c.q == Q_DIM7 || c.q == Q_DOM7B9) ? fifth(c.q) : 2; }
static void gtr_chord(jb_t *e, int tick, chord_t c, int style, int dur, int vel) {
    static const uint8_t GV[NPIA] = {3, 1, 2, 3, 4, 3, 3, 2, 4, 1};                 /* each guitarist's voicing family */
    int n[4], k = 3;
    style = GV[style % NPIA];
    if (style == 4) {                                                              /* Django: compact, full rhythm chords */
        int r = nearest(c.root, 46, 40, 51);
        n[0] = r; n[1] = r + fifth(c.q); n[2] = r + seventh(c.q); n[3] = r + third(c.q) + 12; k = 4;
    } else if (style == 1) {                                                              /* Hall: 3rd, 7th, 9th up high, voice-led */
        n[0] = nearest(pcof(c.root + third(c.q)), e->gv ? e->gv : 55, 50, 61);
        n[1] = n[0] + pcof(seventh(c.q) - third(c.q)); n[2] = n[1] + pcof(ninth_or_13(c) - seventh(c.q));
        e->gv = n[0];
    } else if (style == 2) {                                                       /* Wes: root, 7th, 3rd, 5th / 13th */
        int r = nearest(c.root, 47, 42, 53);
        n[0] = r; n[1] = r + seventh(c.q); n[2] = r + third(c.q) + 12; n[3] = r + (c.q == Q_DOM7 ? 21 : fifth(c.q) + 12); k = 4;
    } else {                                                                       /* Burrell: 7th, 3rd, 13th / 5th, mid range */
        int s = nearest(pcof(c.root + seventh(c.q)), e->gv ? e->gv : 52, 46, 57);
        n[0] = s; n[1] = s + pcof(third(c.q) - seventh(c.q)); n[2] = n[1] + pcof(ninth_or_13(c) - third(c.q));
        e->gv = s;
    }
    for (int i = 0; i < k; i++) add(e, tick, I_GUITAR, n[i], vel + irand(e, 6), dur);
}
/* ---- 1.5 guitarists */
static void gtr_gilberto(jb_t *e) {                                                /* the batida: thumb on 1 and 3, fingers on the clave */
    int bal = e->ballad, end = e->tpbar;
    for (int h = 0; h * 24 < end; h++) {
        chord_t c = cur_chord(e, h * 2);
        int r = (h && ceq(c, cur_chord(e, 0))) ? nearest(pcof(c.root + fifth(c.q)), 40, 36, 47) : nearest(c.root, 42, 38, 49);
        add(e, h * 24, I_GUITAR, r, bal ? 52 : 60, 10);
    }
    const uint8_t *cl = CLAVE[e->bar & 1];
    for (int i = 0; i < 3; i++) {
        int t = cl[i]; if (t >= end) continue;
        chord_t c = cur_chord(e, t / 12);
        int s = nearest(pcof(c.root + seventh(c.q)), e->gv ? e->gv : 55, 51, 61), a = s + pcof(third(c.q) - seventh(c.q)), b = a + pcof(ninth_or_13(c) - third(c.q));
        e->gv = s;
        add(e, t, I_GUITAR, s, (bal ? 38 : 46) + irand(e, 6), bal ? 8 : 5);
        add(e, t, I_GUITAR, a, (bal ? 38 : 46) + irand(e, 6), bal ? 8 : 5);
        add(e, t, I_GUITAR, b, (bal ? 40 : 48) + irand(e, 6), bal ? 8 : 5);
    }
}
static void gtr_grant(jb_t *e) {                                                   /* bluesy stabs; a repeated-note riff every 4th bar */
    int ant = e->tpbar - 4;
    if ((e->bar & 3) == 3) {
        chord_t c = cur_chord(e, 0);
        int n = nearest(pcof(c.root + fifth(c.q)), 64, 60, 70);
        static const uint8_t rt[5] = {0, 8, 12, 20, 24};
        for (int i = 0; i < 5; i++) if (rt[i] < e->tpbar) add(e, rt[i], I_GUITAR, n, 62 + ((i & 1) ? 6 : 0), 3);
        gtr_chord(e, ant, next_chord(e), GT_GRANT, 4, 60);
        return;
    }
    gtr_chord(e, 20, cur_chord(e, 1), GT_GRANT, 3, 56 + irand(e, 8));
    if (!ceq(cur_chord(e, 0), cur_chord(e, 2)) || chance(e, 400)) gtr_chord(e, ant, next_chord(e), GT_GRANT, 4, 54 + irand(e, 8));
}
static void gtr_ellis(jb_t *e) {                                                   /* four to the bar, a slide chord into the next */
    for (int b = 0; b < e->nb; b++) gtr_chord(e, b * 12, cur_chord(e, b), GT_ELLIS, 4, 50 + ((b & 1) ? 8 : 0) + irand(e, 6));
    chord_t x = next_chord(e);
    if (!ceq(x, cur_chord(e, e->nb - 1)) && chance(e, 600)) { x.root = (uint8_t)pcof(x.root + 1); gtr_chord(e, e->tpbar - 4, x, GT_ELLIS, 2, 48); }
}
static void gtr_kessel(jb_t *e) {                                                  /* Charleston chords, then a moving top voice */
    chord_t c0 = cur_chord(e, 0), c2 = cur_chord(e, 2);
    gtr_chord(e, 0, c0, GT_KESSEL, 5, 60 + irand(e, 6));
    gtr_chord(e, 20, cur_chord(e, 1), GT_KESSEL, 4, 56 + irand(e, 6));
    if (!ceq(c0, c2)) gtr_chord(e, 24, c2, GT_KESSEL, 4, 58);
    else if (chance(e, 450)) {
        int n = nearest(pcof(c0.root + third(c0.q)), 64, 60, 70);
        for (int t = e->tpbar - 16; t < e->tpbar - 4; t += 4) { add(e, t, I_GUITAR, n, 56, 3); n = scale_step(c0, n, 1); }
    }
}
static void gtr_christian(jb_t *e) {                                               /* swing-era rhythm; a horn riff every 4th bar */
    int riff = (e->bar & 3) == 3 && e->nb == 4;
    for (int b = 0; b < (riff ? 2 : e->nb); b++) gtr_chord(e, b * 12, cur_chord(e, b), GT_CHRISTIAN, 4, 50 + ((b & 1) ? 10 : 0) + irand(e, 6));
    if (!riff) return;
    chord_t c = cur_chord(e, 2);
    int r = nearest(c.root, 57, 52, 63), iv[5] = {0, third(c.q), fifth(c.q), c.q == Q_DOM7 || c.q == Q_MAJ6 ? 9 : seventh(c.q), 12};
    for (int i = 0; i < 5; i++) add(e, 24 + i * 4 + (i ? 0 : 0), I_GUITAR, r + iv[i], 62 + i * 2, 3);
}
static void gen_guitar(jb_t *e) {
    int st = e->sty[I_GUITAR] % NPIA;
    if (st == GT_GILBERTO) { gtr_gilberto(e); return; }
    if (e->bossa) {                                                               /* BOSSA: the clave in each guitarist's voicing */
        const uint8_t *cl = CLAVE[e->bar & 1];
        for (int i = 0; i < 3; i++) if (cl[i] < e->tpbar) gtr_chord(e, cl[i], cur_chord(e, cl[i] / 12), st, 5, 48 + irand(e, 8));
        return;
    }
    if (st == GT_FREDDIE) { gtr_freddie(e); return; }
    if (!e->ballad) switch (st) {
    case GT_GRANT: gtr_grant(e); return;
    case GT_ELLIS: gtr_ellis(e); return;
    case GT_KESSEL: gtr_kessel(e); return;
    case GT_CHRISTIAN: gtr_christian(e); return;
    }
    if (st == 4) {                                                                /* Django: clipped la pompe, accented backbeats */
        for (int b = 0; b < e->nb; b++) {
            int accent = e->nb == 3 ? b == 1 : b & 1;
            gtr_chord(e, b * 12, cur_chord(e, b), 4, e->ballad ? 8 : accent ? 2 : 4,
                      (e->ballad ? 42 : 52) + (accent ? 18 : 0));
        }
        if (!e->ballad && (e->bar & 3) == 3) {
            chord_t c = cur_chord(e, e->nb - 1);
            int target = nearest(next_chord(e).root, 59, 52, 67);
            int n = nearest(pcof(c.root + fifth(c.q)), target, 52, 67);
            add(e, e->tpbar - 8, I_GUITAR, n, 64, 3);
            add(e, e->tpbar - 4, I_GUITAR, target - 1, 58, 3);
        }
        return;
    }
    chord_t c0 = cur_chord(e, 0), c2 = cur_chord(e, 2);
    int two = !ceq(c0, c2);
    if (e->ballad) {
        gtr_chord(e, st == 1 ? 8 * irand(e, 2) : 0, c0, st, 22, st == 2 ? 52 : 44);
        if (two || chance(e, 400)) gtr_chord(e, 24, c2, st, 20, st == 2 ? 50 : 42);
        return;
    }
    if (st == 1) {                                                                 /* Jim Hall: one or two light touches */
        static const uint8_t P[6][3] = {{1, 8, 0}, {1, 20, 0}, {2, 0, 32}, {1, 32, 0}, {2, 20, 40}, {0, 0, 0}};
        static const uint8_t P3[6][3] = {{1, 8, 0}, {1, 20, 0}, {2, 0, 24}, {1, 24, 0}, {2, 12, 28}, {0, 0, 0}};
        const uint8_t *p = e->nb == 3 ? P3[irand(e, 6)] : P[irand(e, 6)]; int got2 = 0;
        for (int i = 0; i < p[0]; i++) { int t = p[1 + i]; if (t >= 24) got2 = 1; gtr_chord(e, t, cur_chord(e, t / 12), 1, 14 + irand(e, 8), 40 + irand(e, 10)); }
        if (two && !got2) gtr_chord(e, 24, c2, 1, 14, 42);
    } else if (st == 2) {                                                          /* Wes: punchy offbeats; octaves every 4th bar */
        int oct = (e->bar & 3) == 3 && !two, ant = e->tpbar - 4, from = e->nb == 3 ? 12 : 24;
        static const uint8_t P[4][3] = {{2, 20, 44}, {2, 8, 32}, {1, 20, 0}, {2, 20, 32}};
        static const uint8_t P3[4][3] = {{2, 20, 32}, {2, 8, 24}, {1, 20, 0}, {2, 12, 24}};
        const uint8_t *p = e->nb == 3 ? P3[irand(e, 4)] : P[irand(e, 4)];
        for (int i = 0; i < p[0]; i++) {
            int t = p[1 + i]; if (oct && t >= from) continue;
            gtr_chord(e, t, t >= ant ? next_chord(e) : cur_chord(e, t / 12), 2, 3, 62 + irand(e, 10));
        }
        if (two) gtr_chord(e, 24, c2, 2, 3, 64);
        if (oct) {                                                                 /* descending octave line on beats 3 and 4 */
            static const uint8_t ot4[4] = {24, 32, 36, 44}, ot3[4] = {12, 20, 24, 32};
            const uint8_t *ot = e->nb == 3 ? ot3 : ot4;
            int n = nearest(pcof(c0.root + fifth(c0.q)), 60, 55, 66);
            for (int i = 0; i < 4; i++) { add(e, ot[i], I_GUITAR, n, 70, i == 3 ? 6 : 3); add(e, ot[i], I_GUITAR, n + 12, 64, i == 3 ? 6 : 3); n = scale_step(c0, n, -1); }
        }
    } else {                                                                       /* Burrell: Charleston, slide into beat 3 */
        gtr_chord(e, 0, c0, 3, 5, 58 + irand(e, 8));
        gtr_chord(e, 20, cur_chord(e, 1), 3, 3, 54 + irand(e, 8));
        if (two || chance(e, 450)) {
            chord_t c = two ? c2 : c0;
            if (chance(e, 600)) { int s = nearest(pcof(c.root + third(c.q)), 55, 50, 61); add(e, 23, I_GUITAR, s - 1, 44, 1); }   /* slide up a half step */
            gtr_chord(e, 24, c, 3, 4, 60 + irand(e, 6));
        }
    }
}
static void gen_bar(jb_t *e) {
    e->nev = 0;
    if (e->state == ST_COUNT) { for (int b = 0; b < e->nb; b++) add(e, b * 12, I_DRUMS, D_STICK, 100, 1); return; }
    if (e->kind == BK_HOLD || e->kind == BK_END) return;      /* the final chord rings */
    if (e->kind == BK_FINAL) {                                /* everyone on the downbeat, held for two bars */
        chord_t c = cur_chord(e, 0);
        int ring = 2 * e->tpbar;
        add(e, 0, I_DRUMS, D_CRASH, 112, 1); add(e, 0, I_DRUMS, D_KICK, 100, 1);
        int r = nearest(c.root, 33, 28, 39);
        add(e, 0, I_BASS, r, 108, ring);
        int n[4]; voicing(e, c, n);
        for (int i = 0; i < 4; i++) add(e, 0, I_PIANO, n[i], 82 + (i == 3 ? 8 : 0), ring);
        add(e, 0, I_PIANO, nearest(c.root, 40, 33, 45), 76, ring);           /* the left hand's root under it */
        gtr_chord(e, 0, c, 3, ring, 66);
        return;
    }
    gen_drums(e); gen_bass(e); gen_piano(e); gen_guitar(e);
    if (e->nx_kind == BK_FINAL) {                             /* set up the last chord: a triplet fill on 4 */
        for (int t = 36; t < 48; t += 4) add(e, t, I_DRUMS, D_SNARE, 70 + (t - 36) * 3, 1);
        add(e, 44, I_DRUMS, D_KICK, 84, 1);
    }
}

/* ---------------------------------------------------------------- voices */

/* gain factor (per mille) * 2^30 / 127, so vel * G(pm) = factor in Q30; integer constant */
#define G(pm) ((int32_t)((int64_t)(pm) * 8454660 / 1000))
static voice_t *alloc_voice(jb_t *e, int lo, int hi) {
    voice_t *best = &e->v[lo]; int32_t bg = 0x7FFFFFFF;
    for (int i = lo; i < hi; i++) { voice_t *v = &e->v[i]; if (!v->active) return v; if (v->g < bg) { bg = v->g; best = v; } }
    return best;
}
/* the sound an instrument plays now: a valid sample, else synth A, unless synth B is asked for */
#define JB_REL_GTRPK 31873                                                   /* about -20 dB in 120 ms: a fretting-hand mute */
static int nylon_on(const jb_t *e) { return e->sty[I_GUITAR] == GT_GILBERTO && PKS.ok[PK_NYLON]; }   /* Gilberto plays jbnylon */
static int src_of(const jb_t *e, int inst) {
    int k = INST_IDX(inst);
    return PKS.ok[inst] || IS[k].ok || (inst == I_GUITAR && nylon_on(e)) ? SRC_SAMPLE : SRC_SYNTH_A;
}
static void trigger(jb_t *e, const ev_t *ev, int32_t blocks_per_tick_q8) {
    int note = ev->note, vel = ev->vel;
    int src = ev->inst == I_DRUMS ? (PKS.ok[I_DRUMS] ? SRC_SAMPLE : SRC_SYNTH_A) : src_of(e, ev->inst);   /* 1.5: samples, synth only as a fallback */
    int pki = ev->inst == I_GUITAR && nylon_on(e) ? PK_NYLON : ev->inst;          /* the pack this note plays from */
    const jbzone_t *pz = src == SRC_SAMPLE ? pk_zone(pki, ev->note, ev->inst == I_GUITAR ? ev->vel - 14 : ev->vel) : 0;   /* guitar: the soft-pick takes, hard only on real accents */
    if (ev->inst == I_DRUMS && !pz) src = SRC_SYNTH_A;
    if (ev->inst != I_DRUMS && !pz && src == SRC_SAMPLE && !IS[INST_IDX(ev->inst)].ok) src = SRC_SYNTH_A;   /* no zone, no slot */
    int ks_voice = ev->inst == I_GUITAR && src != SRC_SAMPLE;
    voice_t *v = ks_voice ? alloc_voice(e, 0, JB_GTRV) : alloc_voice(e, JB_GTRV, JB_VOICES);
    int16_t *ks = ks_voice ? e->ks[v - e->v] : 0;
    for (int i = 0; i < 7; i++) { v->ph[i] = 0; v->inc[i] = 0; }
    for (int i = 0; i < 4; i++) { v->pa[i] = 0; v->pd[i] = 32767; }
    v->hx = v->hy = v->hx2 = v->hy2 = 0; v->np = 0; v->wait = 0; v->pitch = 0;
    v->active = 1; v->dec = 32767; v->rel = 31000; v->pl = v->pr = 23170;
    v->len = (ev->dur * blocks_per_tick_q8) >> 8;
    if (pz) {                                                                     /* a sound pack zone */
        /* gains matched to the synths (test/packs_test.c, first half second); vel2 * G() stays below 2^31: G <= 2000 */
        enum { PK_GN = 452 };                                                    /* jbnylon */
        static const int16_t PK_G[4] = {0, 1230, 1530, 750}, PK_DG[D_N] = {740, 1950, 600, 52, 390, 810, 1490, 1920, 26, 600};   /* by instrument / drum */
        static const int16_t DPL[D_N] = {23170, 21981, 18678, 25905, 17183, 23170, 25000, 19000, 26500, 18000},
                             DPR[D_N] = {23170, 24302, 26921, 20066, 27902, 23170, 21000, 26500, 19000, 27500};
        int d = ev->inst == I_DRUMS ? 0 : note - pz->key; if (d < -48) d = -48; if (d > 48) d = 48;
        const jbpack_t *pk = PKS.ok[pki];
        v->type = VT_PK; v->pk_d = pk_data(pk) + pz->off; v->pk_len = (int32_t)pz->len; v->pk_pos = 0;
        v->pk_codec = pz->codec; v->pk_pred = pz->pred; v->pk_idx = pz->idx;
#ifdef JB_WEB
        v->pk_loop = 0;
#endif
        v->pk_inc = ((JB_RATIO[d + 48] >> 2) * (pz->step >> 2)) >> 12;
        v->pk_s0 = pk_next(v); v->pk_s1 = pk_next(v); v->pk_frac = 0;
        int vel2 = 48 + ((vel * 79) >> 7);                                        /* the layers carry most of the dynamics */
        v->rel = JB_REL_SMP;
        if (ev->inst == I_DRUMS) {
            int k = note < D_N ? note : D_STICK;
            v->g = mul30(vel2 * G(PK_DG[k]), e->lvl[0]); v->gprev = v->g >> 15; v->len = 0x7FFFFFFF;
            v->pl = DPL[k]; v->pr = DPR[k];
            return;
        }
        v->g = mul30(vel2 * G(pki == PK_NYLON ? PK_GN : PK_G[ev->inst]), e->lvl[ev->inst == I_BASS ? 1 : ev->inst == I_PIANO ? 2 : 3]); v->gprev = 0;
        if (ev->inst == I_PIANO) {
            int32_t pan = -9830 + (note - 60) * 364;
            v->pl = (int16_t)(23170 - ((pan * 11585) >> 15)); v->pr = (int16_t)(23170 + ((pan * 11585) >> 15));
        } else if (ev->inst == I_GUITAR) {                                       /* a real guitar: chords ring, then the hand mutes them */
            v->pl = 18022; v->pr = 27525;
            v->len = ((ev->dur + 4) * blocks_per_tick_q8) >> 8; v->rel = JB_REL_GTRPK;
            if (e->strum_n == 0) e->strum_sp = 260 + (int)((e->bar * 37u + e->tick * 11u) % 9u) * 40;    /* 6..13 ms between strings */
            v->wait = (e->strum_n++) * e->strum_sp;
        }
        return;
    }
    if (ev->inst == I_DRUMS) {
        v->len = 0x7FFFFFFF;
        switch (note) {
        case D_KICK:  v->type = VT_KICK; v->g = vel * G(900); v->dec = JB_DEC_KICK; v->pitch = 32767; break;
        case D_SNARE: v->type = VT_SNARE; v->g = vel * G(1000); v->dec = JB_DEC_SNARE; v->inc[0] = JB_INC_SNARE;
                      v->pa[0] = 32767; v->pd[0] = JB_DEC_SNTONE; v->pl = 21981; v->pr = 24302; break;
        case D_RIDE:  v->type = VT_RIDE; v->g = vel * G(320); v->dec = JB_DEC_RIDE; v->pl = 18678; v->pr = 26921;
                      for (int i = 0; i < 6; i++) v->inc[i] = JB_METAL_RIDE[i];
                      v->inc[6] = JB_INC_PING; break;
        case D_HAT:   v->type = VT_HAT; v->g = vel * G(300); v->dec = JB_DEC_HAT; v->pl = 25905; v->pr = 20066;
                      for (int i = 0; i < 6; i++) v->inc[i] = JB_METAL_HAT[i];
                      break;
        case D_CRASH: v->type = VT_CRASH; v->g = vel * G(450); v->dec = JB_DEC_CRASH; v->pl = 17183; v->pr = 27902;
                      for (int i = 0; i < 6; i++) v->inc[i] = JB_METAL_RIDE[i];
                      break;
        case D_TOMH: case D_TOML: case D_CONGA:                                   /* pitched, decaying sine (no pack zone) */
                      v->type = VT_BASS; v->np = 1; v->inc[0] = JB_INC[note == D_TOMH ? 50 : note == D_TOML ? 43 : 57];
                      v->pa[0] = 32767; v->g = vel * G(note == D_CONGA ? 500 : 700);
                      v->dec = note == D_CONGA ? JB_DEC_SNARE : JB_DEC_KICK;
                      v->pl = note == D_TOMH ? 25000 : note == D_TOML ? 19000 : 18000; v->pr = note == D_TOMH ? 21000 : note == D_TOML ? 26500 : 27500; break;
        case D_SHAKER: v->type = VT_HAT; v->g = vel * G(160); v->dec = JB_DEC_STICK; v->pl = 26500; v->pr = 19000;
                      for (int i = 0; i < 6; i++) v->inc[i] = JB_METAL_HAT[i];
                      break;
        default:      v->type = VT_STICK; v->g = vel * G(600); v->dec = JB_DEC_STICK; break;
        }
        v->g = mul30(v->g, e->lvl[0]);
        v->gprev = v->g >> 15;                                                    /* drums: no attack ramp */
        return;
    }
    int32_t lvl = e->lvl[ev->inst == I_BASS ? 1 : ev->inst == I_PIANO ? 2 : 3];
    if (src == SRC_SAMPLE) {                                                      /* the OP-1 sampler slot */
        const islot_t *is = &IS[INST_IDX(ev->inst)];
        int best = 0, bd = 999;
        for (int i = 0; i < is->nz; i++) { int d = iabs(note - is->z[i].root); if (d < bd) { bd = d; best = i; } }
        int d = note - is->z[best].root; if (d < -48) d = -48; if (d > 48) d = 48;
        v->type = VT_SMP; v->sd = is->d; v->spos = is->z[best].start; v->send = is->z[best].start + is->z[best].len;
        v->sfrac = 0; v->sinc = JB_RATIO[d + 48]; v->sgain = is->gain;
        v->rel = JB_REL_SMP; v->gprev = 0;
        if (ev->inst == I_PIANO) {
            v->g = vel * G(380);
            int32_t pan = -9830 + (note - 60) * 364;
            v->pl = (int16_t)(23170 - ((pan * 11585) >> 15)); v->pr = (int16_t)(23170 + ((pan * 11585) >> 15));
        } else if (ev->inst == I_BASS) {
            v->g = vel * G(900);
        } else {
            v->g = vel * G(450); v->pl = 18022; v->pr = 27525;
            v->wait = (e->strum_n++) * JB_STRUM;
        }
        v->g = mul30(v->g, lvl);
        return;
    }
    if (ev->inst == I_BASS) {
        if (src == SRC_SYNTH_B) {                                                 /* FM bass */
            v->type = VT_FMB; v->inc[0] = v->inc[1] = JB_INC[note];
            v->pa[0] = 1800 + ((800 * vel) >> 7); v->pd[0] = JB_DEC_FMBI;
            v->g = vel * G(800); v->dec = JB_DEC_FMB;
        } else {
            static const int16_t amp[3] = {32767, 12452, 4588};
            static const int16_t dec[3] = {JB_DEC_BASS0, JB_DEC_BASS1, JB_DEC_BASS2};
            v->type = VT_BASS; v->np = 3;
            for (int i = 0; i < 3; i++) { v->inc[i] = JB_INC[note] * (uint32_t)(i + 1); v->pa[i] = amp[i]; v->pd[i] = dec[i]; }
            v->g = vel * G(850);
        }
        v->rel = JB_REL_BASS; v->gprev = 0;
    } else if (ev->inst == I_PIANO) {
        if (src == SRC_SYNTH_B) {                                                 /* FM electric piano */
            v->type = VT_EP; v->inc[0] = v->inc[1] = JB_INC[note];
            v->pa[0] = 700 + ((1700 * vel) >> 7); v->pd[0] = JB_DEC_EPI;
            v->g = vel * G(330); v->dec = JB_DEC_EP;
        } else {
            int32_t bright = 11469 + ((21299 * vel) >> 7);                        /* 0.35 + 0.65 * vel */
            v->type = VT_PIANO;
            for (int k = 0; k < 4; k++) {
                uint32_t inc = JB_PIANO_INC[note * 4 + k];
                if (!inc) break;
                v->inc[k] = inc; v->pd[k] = (int16_t)JB_PIANO_DEC[note * 4 + k];
                v->pa[k] = k ? (JB_PIANO_AMP[k] * bright) >> 15 : 32767;
                v->ph[k] = rnd(e); v->np++;
            }
            v->g = vel * G(260);
        }
        v->rel = JB_REL_PIANO; v->gprev = 0;
        int32_t pan = -9830 + (note - 60) * 364;                                  /* -0.3 + (note - 60) / 90 */
        v->pl = (int16_t)(23170 - ((pan * 11585) >> 15)); v->pr = (int16_t)(23170 + ((pan * 11585) >> 15));
    } else {                                                                      /* guitar: tuned Karplus-Strong */
        int dark = src == SRC_SYNTH_B;
        int N = JB_KS_N[note], pp, c = dark ? 1638 + ((3932 * vel) >> 7) : 3277 + ((7209 * vel) >> 7);
        int32_t y = 0, sum = 0;
        v->type = VT_GUITAR; v->ks = ks; v->ksn = (int16_t)N; v->kspos = 0; v->apc = JB_KS_AP[note];
        v->apx = v->apy = 0; v->ksfb = dark ? 32637 : 32604;                     /* 0.996 / 0.995 */
        for (int i = 0; i < N; i++) { y += (c * (noise(e) - y)) >> 15; ks[i] = (int16_t)y; }
        pp = (N * (dark ? 25 : 17)) / 100; if (pp < 1) pp = 1;                    /* pick position comb */
        for (int i = N - 1; i >= pp; i--) ks[i] = op1_sat16(ks[i] - ((ks[i - pp] * (dark ? 19661 : 29491)) >> 15));
        for (int i = 0; i < N; i++) sum += ks[i];
        sum /= N;
        for (int i = 0; i < N; i++) ks[i] = op1_sat16(ks[i] - sum);
        v->g = vel * G(dark ? 1400 : 1200); v->rel = JB_REL_GTR; v->gprev = v->g >> 15;
        v->wait = (e->strum_n++) * JB_STRUM;
    }
    v->g = mul30(v->g, lvl);
    if (v->gprev) v->gprev = v->g >> 15;
}

/* one voice, one 64-sample block, mixed into L / R (or the guitar bus G) */
static void render_voice(jb_t *e, voice_t *v, int32_t *L, int32_t *R, int32_t *Gb) {
    int start = 0;
    if (v->wait >= OP1_FX_BLOCK) { v->wait -= OP1_FX_BLOCK; return; }
    if (v->wait > 0) { start = v->wait; v->wait = 0; }
    int32_t g0 = v->gprev;
    v->g = mul30(v->g, v->dec);
    if (v->len > 0) v->len--; else v->g = mul30(v->g, v->rel);
    if (v->type == VT_GUITAR && v->len <= 0) v->ksfb = 31785;                     /* damped: 0.97 */
    int32_t g1 = v->g >> 15, dg = (g1 - g0) * 8;                                  /* per-sample ramp over 64 samples, Q15 << 9 */
    int32_t gacc = g0 << 9;
    v->gprev = g1;
    if (v->type == VT_KICK) { v->inc[0] = JB_INC_48HZ + (uint32_t)(((JB_INC_75HZ >> 15) * v->pitch)); v->pitch = (v->pitch * JB_DEC_KICKP) >> 15; }
    for (int n = start; n < OP1_FX_BLOCK; n++) {
        int32_t s = 0;
        switch (v->type) {
        case VT_KICK: v->ph[0] += v->inc[0]; s = op1_sin((int32_t)(v->ph[0] >> 19)); break;
        case VT_SNARE: {
            v->ph[0] += v->inc[0];
            int32_t x = noise(e);
            v->hy = (JB_HP80 * (v->hy + x - v->hx)) >> 15; v->hx = x;
            s = ((op1_sin((int32_t)(v->ph[0] >> 19)) * v->pa[0]) >> 16) + ((v->hy * 22938) >> 15);
        } break;
        case VT_RIDE: case VT_HAT: case VT_CRASH: {
            int32_t m = 0;
            for (int i = 0; i < 6; i++) { v->ph[i] += v->inc[i]; m += (int32_t)v->ph[i] < 0 ? -1 : 1; }
            int32_t x = v->type == VT_RIDE ? m * 3277 + ((noise(e) * 8192) >> 15)
                      : v->type == VT_HAT ? m * 3823 + ((noise(e) * 16384) >> 15)
                                          : m * 2731 + ((noise(e) * 22938) >> 15);
            v->hy = (JB_HP82 * (v->hy + x - v->hx)) >> 15; v->hx = x;
            v->hy2 = (JB_HP82 * (v->hy2 + v->hy - v->hx2)) >> 15; v->hx2 = v->hy;
            s = v->hy2;
            if (v->type == VT_RIDE) { v->ph[6] += v->inc[6]; s += (op1_sin((int32_t)(v->ph[6] >> 19)) * 4588) >> 15; }
        } break;
        case VT_STICK: { int32_t x = noise(e); v->hy = (JB_HP60 * (v->hy + x - v->hx)) >> 15; v->hx = x; s = v->hy; } break;
        case VT_BASS: case VT_PIANO:
            for (int i = 0; i < v->np; i++) { v->ph[i] += v->inc[i]; s += (op1_sin((int32_t)(v->ph[i] >> 19)) * v->pa[i]) >> 15; }
            break;
        case VT_EP: case VT_FMB: {                                                /* 2-op FM, 1:1 */
            v->ph[0] += v->inc[0]; v->ph[1] += v->inc[1];
            int32_t m = op1_sin((int32_t)(v->ph[1] >> 19));
            s = op1_sin((int32_t)(v->ph[0] >> 19) + ((m * v->pa[0]) >> 15));
        } break;
        case VT_PK: {                                                             /* pack: decode on the fly, linear interpolation */
            if (v->pk_pos > v->pk_len) { v->active = 0; goto done; }
            s = v->pk_s0 + (((v->pk_s1 - v->pk_s0) * (int32_t)(v->pk_frac >> 1)) >> 15);
            v->pk_frac += v->pk_inc;
            while (v->pk_frac >= 65536) {
                v->pk_frac -= 65536; v->pk_s0 = v->pk_s1;
#ifdef JB_WEB
                if (v->pk_pos >= v->pk_len && v->pk_loop > 0) v->pk_pos = v->pk_loop;
#endif
                if (v->pk_pos < v->pk_len) v->pk_s1 = pk_next(v); else { v->pk_s1 = 0; v->pk_pos++; }
            }
        } break;
        case VT_SMP: {
            if (v->spos >= v->send - 1) { v->active = 0; goto done; }
            int32_t a = v->sd[v->spos], b = v->sd[v->spos + 1];
            s = a + (((b - a) * (int32_t)(v->sfrac >> 1)) >> 15);
            v->sfrac += v->sinc; v->spos += (int32_t)(v->sfrac >> 16); v->sfrac &= 0xFFFF;
            int32_t left = v->send - v->spos;
            if (left < 512) s = (s * left) >> 9;                                  /* fade at the end of the zone */
            s = (s * v->sgain) >> 15;
        } break;
        case VT_GUITAR: {
            int p = v->kspos, q = p + 1; if (q >= v->ksn) q = 0;
            int32_t y = v->ks[p], avg = ((y + v->ks[q]) * v->ksfb) >> 16;
            int32_t ap = ((v->apc * (avg - v->apy)) >> 15) + v->apx;
            v->apx = avg; v->apy = ap;
            v->ks[p] = op1_sat16(ap); v->kspos = (int16_t)q; s = y;
        } break;
        }
        gacc += dg;
        int32_t x = ((s >> 2) * (gacc >> 9)) >> 13;        /* |s| < 2^17, gain < 2^16: no 32-bit overflow */
        if (x > 65535) x = 65535; else if (x < -65535) x = -65535;   /* x * pan stays inside 32 bits */
        if (v->type == VT_GUITAR) Gb[n] += x;
        else { L[n] += (x * v->pl) >> 15; R[n] += (x * v->pr) >> 15; }
    }
done:
    if (v->type == VT_BASS || v->type == VT_PIANO) {
        for (int i = 0; i < v->np; i++) v->pa[i] = (v->pa[i] * v->pd[i]) >> 15;
        if (v->pa[0] < 4) v->active = 0;
    }
    if (v->type == VT_SNARE || v->type == VT_EP || v->type == VT_FMB) v->pa[0] = (v->pa[0] * v->pd[0]) >> 15;
    if (g1 < 3 && v->len <= 0) v->active = 0;
    if (v->type <= VT_STICK && g1 < 3) v->active = 0;
}

/* ------------------------------------------------------------- transport */

static void stop(jb_t *e);
static void clear_pend(jb_t *e) { if (e->pend >= 0) { e->pend = -1; e->s_pend = -1; if (e->row > 0) e->row--; } }   /* the CANCEL QUEUE row goes */
static void do_tick(jb_t *e, int32_t blocks_per_tick_q8) {
    if (e->tick == 0) gen_bar(e);
    e->strum_n = 0;
    if (e->tick % JB_TPB == 0) { e->s_beat = e->tick / JB_TPB; e->s_bar = e->bar; e->s_state = e->state; }
#ifdef JB_TRIGGER_HOOK                                  /* web build: a player's touch (timing, dynamics) per note */
    for (int i = 0; i < e->nev; i++) if (e->ev[i].tick == e->tick) JB_TRIGGER_HOOK(e, &e->ev[i], blocks_per_tick_q8);
#else
    for (int i = 0; i < e->nev; i++) if (e->ev[i].tick == e->tick) trigger(e, &e->ev[i], blocks_per_tick_q8);
#endif
#ifdef JB_TICK_HOOK
    JB_TICK_HOOK(e, blocks_per_tick_q8);                 /* web build: the lead line plays on the band's own ticks */
#endif
    if (++e->tick >= e->tpbar) {
        /* one bar line, one order: a queued song first (it brings its own key, jump and settings: any queued key or
         * jump of the old song is dropped), else the count ends or the form moves on (a confirmed jump is part of the
         * plan), then a queued key change. */
        e->tick = 0;
        if (e->pend >= 0 && e->state == ST_PLAY) {       /* a song chosen in SONGS while playing */
            int to = e->pend; clear_pend(e);
            load_song(e, to); e->set = e->s_set = e->pend_set; e->spos = e->s_spos = e->pend_pos;
            e->bar = 0; e->crash_next = 1; form_reset(e);
        } else {
            if (e->state == ST_COUNT) { e->state = ST_PLAY; plan_next(e); }   /* vb: the first bar, set by start() */
            else begin_bar(e);
            apply_key(e);
        }
        if (e->kind == BK_END) stop(e);                  /* after the ending: done */
    }
}
static void start(jb_t *e) {
    e->state = ST_COUNT; e->tick = 0; e->acc = e->spt;
    e->bass_last = 38; e->pv_ok = 0; e->skip_down = 0; e->crash_next = 0; e->gv = 0; e->pend = -1;
    e->chorus = 1; e->pass = 0; e->ending = 0; e->tag_i = 0; e->shift = 0;
    /* the count-in counts into the first bar: the loop's start, else bar 1 */
    e->kind = BK_CHART; e->bar = e->sbar >= 0 && e->sbar < e->nbars ? e->sbar : looping(e) ? loop_lo(e) : 0;
    e->bcur = -1; e->jump = -1;
    e->nx_bar = e->bar; e->nx_kind = BK_CHART; e->nx_shift = 0;
    make_bar(e, &e->vb, e->bar, BK_CHART, 0, 0);
    for (int k = 0; k < 4; k++) e->vbn.beat[k] = e->vb.beat[k];   /* the screen's NEXT during the count: the first chord */
    e->vbn.section = e->vb.section;
    if (e->intro != IN_COUNT) {                          /* an intro instead of the count: the band starts on it now */
        e->state = ST_PLAY; e->kind = BK_INTRO; e->intro_i = 0;
        make_bar(e, &e->vb, 0, BK_INTRO, 0, 0); plan_next(e);
    }
}
static void stop(jb_t *e) {
    e->state = ST_STOP; e->s_state = ST_STOP; clear_pend(e); e->jump = -1; e->bm = 0;
    apply_key(e);                                        /* stopped: a queued key takes effect now */
    for (int i = 0; i < JB_VOICES; i++) if (e->v[i].active) { e->v[i].len = 0; e->v[i].rel = JB_REL_STOP; }
}

/* Shift words. Layer 1 (0..3): levels for drums, bass, piano, guitar (fine).
 * Layer 2 (4..7): the player per instrument, same knob order. Drums: 10 steps = 5 players x 2 sounds.
 * Bass, piano, guitar: 15 steps = 5 players x 3 sounds (SAMPLE, SYNTH A, SYNTH B), player by player. */
static const int16_t LVL_DEFAULT[4] = {23593, 26214, 20971, 11796};   /* x 1.25: 0.9, 1.0, 0.8, 0.45 */
static const uint8_t PLAYER_STEPS[4] = {5 * 2, 5 * 3, 5 * 3, 5 * 3};   /* 1.3 layout: 5 players, each with its sound choices */
static int step_of(int32_t w, int n) { int s = (int)((w * n) >> 15); return s < 0 ? 0 : s >= n ? n - 1 : s; }
/* 1.4: Shift + the four knobs is the mixer only (layer 1, read every block). The players and sounds moved to SETUP BAND
 * (separate fields, this session); the 1.3 Shift layer 2 words (player and sound in one value) are read once at start
 * so a saved lineup still plays as it was. */
static void read_shift(jb_t *e, int pos) {
    const int16_t *w = op1_fx_shift_words(pos);
    int all0 = 1; for (int i = 0; i < 8; i++) if (w[i]) all0 = 0;
    for (int i = 0; i < 4; i++) { int32_t x = all0 ? LVL_DEFAULT[i] : w[i]; if (x < 0) x = 0; e->lvl[i] = (x * 40960) >> 15; }
    if (e->band_init) return;
    e->band_init = 1;
    for (int i = 0; i < 4; i++) {
        int s = all0 ? 0 : step_of(w[4 + i], PLAYER_STEPS[i]);
        e->sty[i] = (uint8_t)(i == I_DRUMS ? s / 2 : s / 3);                   /* the player; the old sound choice is ignored */
    }
}

static void jb_init(int pos) {
    jb_t *e = &S[pos % 3];
    for (int i = 0; i < JB_VOICES; i++) e->v[i].active = 0;
    e->rng = 0x9E3779B9u ^ (uint32_t)(pos * 7919); e->nz = 22222u;
    e->state = e->s_state = ST_STOP; e->latch_prev = 0;
    e->s_volume = e->s_muted = 0;
    e->band_gain = 0;
    e->b1l = e->b1b = e->b2l = e->b2b = e->blp = 0;
    e->spt = (22050 << 14) / JB_TPB;
    e->transpose = e->s_transpose = 0; e->bar = 0;
    for (int i = 0; i < 4; i++) e->lvl[i] = (LVL_DEFAULT[i] * 40960) >> 15;
    for (int i = 0; i < 3; i++) e->src[i] = SRC_SAMPLE;
    for (int i = 0; i < 4; i++) e->sty[i] = 0;
    e->dsrc = SRC_SAMPLE;
    e->cursor = -1; e->pend = -1; e->browse = 0; e->gv = 0; e->white_prev = -1;
    e->view = VIEW_ALL; e->row = 0; e->kprev = -1; e->set = e->s_set = -1; e->spos = 0; e->pend_set = -1; e->pend_pos = 0;
    e->key_off = 0; e->bprev = -1; e->blue_prev = -1; e->bmode = 0;
    e->kstep = KS_OFF; e->choruses = 0; e->turn = TA_OFF; e->endtype = 0; e->intro = IN_COUNT; e->intro_i = 0;
    e->bcur = -1; e->sbar = -1; e->jump = -1; e->bidle = 0;
    e->sview = 0; e->srow = 0; e->sedit = 0; e->green_prev = -1; e->mute_inv = 0;
    e->screen = SCR_PLAY; e->sw = 0; e->sw_row = 0; e->bm = 0; e->bm_row = 0; e->scat = -1;
    e->red_prev = -1; e->rprev = -1; e->bidx_prev = -1; e->kbase = 0; e->key_pend = 0; e->key_tgt = 0; e->kmsg = 0;
    e->loaded = 0; e->band_init = 0; e->ballad = 0; e->bossa = 0; e->ppat = 0;
    load_song(e, 0);
}

/* the band for one block, stereo, before the output stage */
/* browser rows. VIEW_MENU: each set, then ALL SONGS. A set: a back row, then its songs. VIEW_ALL: (a back row when there
 * are sets) then every song of the book. */
static int view_rows(int v) {
    if (v == VIEW_MENU) return SETS.n + 1;
    if (v >= 0 && v < SETS.n) return 1 + SETS.count[v];
    return (SETS.n > 0) + nsongs();
}
static int row_song(int v, int row) {                /* the song on a row, -1: a set (menu) or the back row */
    if (v == VIEW_MENU) return -1;
    if (v >= 0 && v < SETS.n) return row >= 1 && row <= SETS.count[v] ? SETS.song[v][row - 1] : -1;
    row -= SETS.n > 0;
    return row >= 0 && row < nsongs() ? row : -1;
}
static int row_of(const jb_t *e, int v, int song) {  /* the row of a song in a view (its own place in its set), else the first */
    if (v >= 0 && v < SETS.n) {
        if (e->set == v && e->spos < SETS.count[v] && SETS.song[v][e->spos] == song) return 1 + e->spos;
        for (int i = 0; i < SETS.count[v]; i++) if (SETS.song[v][i] == song) return 1 + i;
        return SETS.count[v] ? 1 : 0;
    }
    if (v == VIEW_MENU) return e->set >= 0 && e->set < SETS.n ? e->set : 0;
    return (SETS.n > 0) + (song < nsongs() ? song : 0);
}
static void browser_open(jb_t *e, int next) {       /* where the list opens: the set in use (its next song), else the menu */
    if (e->set >= 0 && e->set < SETS.n) {
        e->view = e->set;
        e->row = 1 + e->spos + (next && e->spos + 1 < SETS.count[e->set] ? 1 : 0);
    } else if (SETS.n > 0) { e->view = VIEW_MENU; e->row = 0; }
    else { e->view = VIEW_ALL; e->row = row_of(e, VIEW_ALL, e->song); }
    e->browse = JB_BROWSE_BLOCKS;
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- PLAY: bar actions (white press on the bar cursor). k = 0: how many; k >= 1: the k-th action */
enum { BA_START, BA_JUMP, BA_CANCEL };
static int bar_actions(const jb_t *e, int k) {
    int a[3], n = 0;
    if (e->state == ST_STOP) a[n++] = BA_START;
    else if ((e->state == ST_PLAY || e->state == ST_COUNT) && !e->ending && (e->kind == BK_CHART || e->kind == BK_INTRO)) a[n++] = BA_JUMP;
    a[n++] = BA_CANCEL;
    return k <= 0 ? n : a[(k - 1) % n];
}
static int bar_action_do(jb_t *e, int act) {            /* returns 1 when the form needs planning again */
    e->bm = 0;
    if (act == BA_START) { e->sbar = e->bcur; e->bcur = -1; e->state = e->s_state = ST_ARMED; return 0; }   /* count-in / intro into it */
    if (act == BA_JUMP) { e->jump = e->bcur; clear_pend(e); return 1; }   /* at the next bar line; a queued song is dropped */
    e->bcur = -1; if (e->jump >= 0) { e->jump = -1; return 1; }        /* CANCEL: the cursor and a queued jump */
    return 0;
}

/* ---- SONGS: the browser, with CANCEL QUEUE as the first row while a song is queued. Browsing never loads. */
static int songs_rows(const jb_t *e) { return (e->pend >= 0) + view_rows(e->view); }
static void songs_press(jb_t *e, int playing) {
    int q = e->pend >= 0, r = e->row - q;
    if (q && e->row == 0) {                              /* CANCEL QUEUE: the cursor goes back to the song that plays */
        e->pend = -1; e->row = e->view == VIEW_MENU ? 0 : row_of(e, e->view, e->song); return;
    }
    if (e->view == VIEW_MENU) {                          /* open a set (or every song) */
        int v = r < SETS.n ? r : VIEW_ALL;
        e->view = v; e->row = q + row_of(e, v, e->song); return;
    }
    int cs = row_song(e->view, r);
    if (cs < 0) { int from = e->view; e->view = VIEW_MENU; e->row = q + (from >= 0 ? from : SETS.n); return; }   /* back row */
    int set = e->view >= 0 ? e->view : -1, sp = r - 1;
    if (!playing) {                                      /* stopped: load it, ready to start (red) */
        if (cs != e->song) load_song(e, cs);
        e->set = set; e->spos = sp;
    } else if (cs != e->song) {                          /* playing: queued for the next bar line; a queued jump is dropped */
        if (!q) e->row++;                                /* the CANCEL QUEUE row appears above */
        e->pend = cs; e->pend_set = set; e->pend_pos = sp; e->jump = -1; e->bcur = -1;
    } else { if (q) e->row--; e->pend = -1; e->set = set; e->spos = sp; }   /* the song that plays: a queue is cancelled */
}

/* ---- SETUP: categories, then rows. White turns pick a row, red turns change its value, white press opens / acts. */
enum { SC_PRACTICE, SC_ARRANGE, SC_BAND, SC_SONG, SC_N };
enum { R_BACK, R_LOOP, R_FROM, R_TO, R_KSTEP, R_CHORUS, R_FEEL, R_INTRO, R_TURN, R_END,
       R_DPLAY, R_BPLAY, R_PPLAY, R_GPLAY, R_RKEY, R_RTEMPO, R_N };
static const uint8_t SC_ROWS[SC_N][9] = {
    {R_BACK, R_LOOP, R_FROM, R_TO, R_KSTEP, R_CHORUS},
    {R_BACK, R_FEEL, R_INTRO, R_TURN, R_END},
    {R_BACK, R_DPLAY, R_BPLAY, R_PPLAY, R_GPLAY},
    {R_BACK, R_RKEY, R_RTEMPO}};
static const uint8_t SC_NROWS[SC_N] = {6, 5, 5, 3};
static int setup_rows(int c) { return c >= 0 && c < SC_N ? SC_NROWS[c] : 1; }
static int setup_row(int c, int row) { return c >= 0 && c < SC_N && row >= 0 && row < SC_NROWS[c] ? SC_ROWS[c][row] : R_BACK; }
static int wrapn(int v, int n) { return ((v % n) + n) % n; }
static int setup_turn(jb_t *e, int r, int d) {           /* red turn on a row; returns 1 when the form changed */
    if (r == R_LOOP || r == R_FROM || r == R_TO || r == R_KSTEP) e->pass = 0;   /* a new loop counts from 1 */
    switch (r) {
    case R_LOOP:   e->loop = wrapn(e->loop + d, e->nsec + 2); if (!looping(e)) e->shift = 0; return 1;
    case R_FROM:   e->loop = e->nsec + 1; e->lfrom = clampi(e->lfrom + d, 0, e->nbars - 1); if (e->lto < e->lfrom) e->lto = e->lfrom; return 1;
    case R_TO:     e->loop = e->nsec + 1; e->lto = clampi(e->lto + d, 0, e->nbars - 1); if (e->lfrom > e->lto) e->lfrom = e->lto; return 1;
    case R_KSTEP:  e->kstep = wrapn(e->kstep + d, KS_N); return 1;
    case R_CHORUS: e->choruses = clampi(e->choruses + d, 0, 16); return 1;
    case R_FEEL: { int f = wrapn(e->ballad + 2 * e->bossa + d, 3); e->ballad = f == 1; e->bossa = f == 2; return 0; }
    case R_INTRO:  e->intro = wrapn(e->intro + d, IN_N); return 1;
    case R_TURN:   e->turn = wrapn(e->turn + d, TA_N); return 1;
    case R_END:    e->endtype ^= d & 1; return 1;
    case R_DPLAY: case R_BPLAY: case R_PPLAY: case R_GPLAY: {
        int i = r - R_DPLAY; e->sty[i] = (uint8_t)wrapn(e->sty[i] + d, NPLAY[i]); return 0;
    }
    }
    return 0;
}
static int setup_act(jb_t *e, int r) {                  /* white press on an action row */
    if (r == R_RKEY) {                                   /* back to the written key: at once stopped, next bar playing */
        if (e->state < ST_COUNT) { e->key_off = 0; e->shift = 0; e->key_pend = 0; e->transpose = 0; if (e->bidx_prev >= 0) e->kbase = e->bidx_prev; }
        else if (wrapk(e->key_off + e->shift) != 0) { e->key_pend = 1; e->key_tgt = 0; }
        if (e->kstep) { e->kstep = KS_OFF; return 1; }
        return 0;
    }
    if (r == R_RTEMPO) { if (e->song >= 0 && e->song < 130) TADJ[e->song] = 0; set_bpm(e, song_bpm(e->song)); }
    return 0;
}
static uint32_t scan_clock;
static void band_block(int pos, const int16_t *p, int32_t *L, int32_t *R) {
    jb_t *e = &S[pos % 3];
    int32_t Gb[OP1_FX_BLOCK];
    for (int n = 0; n < OP1_FX_BLOCK; n++) L[n] = R[n] = Gb[n] = 0;

    /* sampler slots: songbook in small steps; instruments one at a time, about 3 times a second each */
    ++scan_clock;
    book_scan((scan_clock & 63) == 32);
    pk_scan();
    if ((scan_clock & 63) == 0) scan_inst((int)((scan_clock >> 6) % 3));

    /* knobs and presses (per block). 1.4 control map, the same on every screen unless the screen says otherwise:
     *   blue  : plain knob, 12 positions = the key (PLAY only); press (latch flag) = the screen switcher
     *   green : band volume; press (latch flag) = band mute, on every screen
     *   white : stepped, relative clicks; press = bit 7 of word 7. PLAY: bar cursor / bar actions. SONGS: browse /
     *           select. SETUP: row / open or act. Switcher: choose / open.
     *   red   : stepped, relative clicks; press = bit 15 of word 7 = transport, everywhere. PLAY: tempo. SETUP: value.
     * Only white and red can be stepped with a push (op1r has two push bits); blue is a plain knob read relative to the
     * position it had when the key was last set, so a saved preset never moves the key by itself. */
    int ns = nsongs();
    op1_fx_wrap[pos % 3][2] = 127; op1_fx_wrap[pos % 3][3] = 127;
    int wstep = (op1_fx_param_q15(p, 2) * 127) >> 15, rstep = (op1_fx_param_q15(p, 3) * 127) >> 15;
    int bidx = op1_fx_list(op1_fx_param_q15(p, 0), 12);
    int replan = 0, playing = e->state >= ST_COUNT;
    if (e->book_gen != B.gen) {                          /* a new book: its sets and song numbers differ */
        for (int i = 0; i < 130; i++) { SS[i].used = 0; TADJ[i] = 0; }
        e->loaded = 0;
        e->pend = -1; e->set = -1; e->view = VIEW_ALL; e->row = 0;
        load_song(e, playing && e->song < ns ? e->song : 0);
        e->row = row_of(e, VIEW_ALL, e->song);
        if (playing) { e->bar = 0; e->tick = 0; form_reset(e); }
    }
    read_shift(e, pos);
    e->s_volume = op1_fx_param_q15(p, 1);
    e->s_muted = op1_fx_page[pos % 3][1] != 0;          /* green: mute on every screen; the live input passes */
    int bl = op1_fx_page[pos % 3][0] != 0, wp = (p[7] >> 7) & 1, rp = ((uint16_t)p[7] >> 15) & 1;
    int bpress = 0, wpress = 0, rpress = 0, wd = 0, rd = 0;
    if (e->blue_prev < 0) e->blue_prev = bl; else if (bl != e->blue_prev) { e->blue_prev = bl; bpress = 1; }
    if (e->white_prev < 0) e->white_prev = wp; else if (wp != e->white_prev) { e->white_prev = wp; wpress = 1; }
    if (e->red_prev < 0) e->red_prev = rp; else if (rp != e->red_prev) { e->red_prev = rp; rpress = 1; }
    if (e->kprev < 0) e->kprev = wstep;                  /* saved positions at start are not turns */
    else if (wstep != e->kprev) { wd = wstep - e->kprev; if (wd > 63) wd -= 127; else if (wd < -63) wd += 127; e->kprev = wstep; }
    if (e->rprev < 0) e->rprev = rstep;
    else if (rstep != e->rprev) { rd = rstep - e->rprev; if (rd > 63) rd -= 127; else if (rd < -63) rd += 127; e->rprev = rstep; }
    int on_play = e->screen == SCR_PLAY && !e->sw;

    /* blue turn: the key. PLAY only (elsewhere a turn just moves the reference). Stopped: at once. Playing: queued for
     * the next bar line; turning back to the key that plays cancels it. A manual key turns the loop's key cycle off. */
    {
        int actual = wrapk(e->key_off + e->shift);
        if (e->bidx_prev < 0) { e->bidx_prev = bidx; e->kbase = bidx - actual; }
        int moved = bidx != e->bidx_prev;
        e->bidx_prev = bidx;
        if (moved && on_play) {
            int want = wrapk(bidx - e->kbase);
            if (e->kstep != KS_OFF) { e->kstep = KS_OFF; e->kmsg = 1378; e->pass = 0; replan = 1; }   /* 2 s of KEY CYCLE OFF */
            if (!playing) { e->key_off = want - e->shift; e->key_pend = 0; e->transpose = e->key_off; }
            else if (want == actual) e->key_pend = 0;
            else { e->key_pend = 1; e->key_tgt = want; }
        } else if (!e->key_pend) e->kbase = bidx - actual;   /* the knob's place now means the key that plays */
        if (e->kmsg > 0) e->kmsg--;
    }
    e->transpose = e->key_off; e->s_transpose = e->transpose;

    /* blue press: the switcher opens; pressed again it closes and nothing changes */
    if (bpress) { if (e->sw) e->sw = 0; else { e->sw = 1; e->sw_row = e->screen; } }

    if (e->sw) {                                         /* switcher: white chooses and opens; red turns do nothing */
        if (wd) e->sw_row = ((e->sw_row + wd) % SCR_N + SCR_N) % SCR_N;
        if (wpress) {
            int to = e->sw_row; e->sw = 0;
            if (to != e->screen) {
                e->screen = to; e->bm = 0;
                if (to == SCR_SONGS) browser_open(e, playing);   /* playing in a set: on its next song */
                if (to == SCR_SETUP) { e->scat = -1; e->srow = 0; }
            }
        }
    } else if (e->screen == SCR_PLAY) {
        if (e->bm) {                                     /* bar actions: white chooses, white press confirms */
            int n = bar_actions(e, 0);
            if (wd) e->bm_row = ((e->bm_row + wd) % n + n) % n;
            if (wpress) { if (bar_action_do(e, bar_actions(e, e->bm_row + 1))) replan = 1; }
        } else {
            if (wd) {                                    /* white: the bar cursor (selects only) */
                if (e->bcur < 0) e->bcur = playing ? e->bar : e->sbar >= 0 ? e->sbar : looping(e) ? loop_lo(e) : 0;
                e->bcur = ((e->bcur + wd) % e->nbars + e->nbars) % e->nbars;
            }
            if (wpress) {
                if (e->bcur < 0) e->bcur = playing ? e->bar : e->sbar >= 0 ? e->sbar : looping(e) ? loop_lo(e) : 0;
                e->bm = 1; e->bm_row = 0;
            }
        }
        if (rd) {                                        /* red: the tempo, 1 BPM a click, at once */
            int b = e->bpm ? e->bpm : e->op1bpm10 ? (e->op1bpm10 + 5) / 10 : 120;
            b = clampi(b + rd, 40, 320);
            if (e->song >= 0 && e->song < 130) TADJ[e->song] = (int16_t)(b == song_bpm(e->song) ? 0 : b);
            set_bpm(e, b);
        }
    } else if (e->screen == SCR_SONGS) {                 /* browsing only moves the cursor; a press acts */
        int n = songs_rows(e);
        if (wd) e->row = ((e->row + wd) % n + n) % n;
        if (wpress) songs_press(e, playing);
    } else {                                             /* SETUP */
        if (e->scat < 0) {
            if (wd) e->srow = ((e->srow + wd) % SC_N + SC_N) % SC_N;
            if (wpress) { e->scat = e->srow; e->srow = 0; }
        } else {
            int n = setup_rows(e->scat);
            if (wd) e->srow = ((e->srow + wd) % n + n) % n;
            int r = setup_row(e->scat, e->srow);
            if (wpress) { if (r == R_BACK) { e->srow = e->scat; e->scat = -1; } else if (setup_act(e, r)) replan = 1; }
            if (rd && setup_turn(e, r, rd)) replan = 1;
        }
    }
    {   int cs = row_song(e->view, e->row - (e->pend >= 0));
        e->cursor = cs; e->s_cursor = cs; }
    e->s_pend = e->pend; e->s_view = e->view; e->s_row = e->row; e->s_set = e->set; e->s_spos = e->spos;

    /* tempo: the song's. When the OP-1 is set to it (within 1 BPM; over 220 BPM: half of it, the OP-1's limit) the band
     * runs on the OP-1 clock instead, so the metronome, tape and sequencers line up. No song tempo: the OP-1 clock. */
    op1_beat_t bc; op1_beat_clock(&bc);
    if (bc.len != e->op1len) {                           /* a division only when the OP-1 tempo changes */
        e->op1len = bc.len;
        e->op1bpm10 = bc.len > (1 << 20) ? (int)(423360000u / (uint32_t)(bc.len >> 10)) : 0;   /* 2646000 * 160 / (len >> 10) */
    }
    int half = e->bpm > 220, tgt10 = half ? e->bpm * 5 : e->bpm * 10, d10 = e->op1bpm10 - tgt10;
    e->sync = e->bpm && e->op1bpm10 && bc.step != 0 && d10 >= -10 && d10 <= 10;
    e->s_op1bpm10 = e->op1bpm10; e->s_sync = e->sync;
    int32_t len = e->sync ? (half ? bc.len >> 1 : bc.len) : e->bpm ? e->blen : bc.len > 0 ? bc.len : (22050 << 14);
    e->spt = len / JB_TPB;
    int32_t bpt_q8 = e->spt >> 12;                       /* blocks per tick, Q8: spt / (64 << 14) * 256 */
    if (rpress) {                                        /* red press: transport, on every screen and in the switcher */
        if (e->state == ST_STOP) { e->state = ST_ARMED; e->s_state = ST_ARMED; }
        else if (e->state == ST_PLAY && e->kind == BK_CHART && !e->ending) { e->ending = 1; replan = 1; }   /* finish this pass */
        else stop(e);                                    /* count-in, intro, finishing: stop now */
        e->s_pend = e->pend;
    }
    if (replan && e->state == ST_PLAY) plan_next(e);
    e->s_shift = e->shift; e->s_kind = e->kind; e->s_chorus = e->chorus; e->s_pass = e->pass; e->s_ending = e->ending;
    if (e->state == ST_ARMED) {   /* on the song's own clock: now; on the OP-1's: its next beat (now, if it stands still) */
        if ((e->bpm && !e->sync) || bc.len <= 0 || bc.step == 0 || bc.next < (OP1_FX_BLOCK << 14)) start(e);
    }
    if (e->state >= ST_COUNT) {
        e->acc += OP1_FX_BLOCK << 14;
        while (e->acc >= e->spt) { e->acc -= e->spt; do_tick(e, bpt_q8); }
    }
    int any = 0;
    for (int i = 0; i < JB_VOICES; i++) if (e->v[i].active) { render_voice(e, &e->v[i], L, R, Gb); any = 1; }
    if (!any && e->state == ST_STOP) {   /* idle: no CPU, and no fixed-point residue left in the body filters */
        e->b1l = e->b1b = e->b2l = e->b2b = e->blp = 0;
        return;
    }

    /* guitar body: two resonances (state-variable band-passes, unity peak) and a 3.2 kHz roll-off, panned right */
    for (int n = 0; n < OP1_FX_BLOCK; n++) {
        int32_t x = Gb[n];
        e->b1l += (JB_SVF_F105 * e->b1b) >> 15;
        int32_t h1 = x - e->b1l - ((JB_SVF_Q105 * e->b1b) >> 15);
        e->b1b += (JB_SVF_F105 * h1) >> 15;
        e->b2l += (JB_SVF_F240 * e->b2b) >> 15;
        int32_t h2 = x - e->b2l - ((JB_SVF_Q240 * e->b2b) >> 15);
        e->b2b += (JB_SVF_F240 * h2) >> 15;
        int32_t o = ((x * 22938) >> 15) + ((((e->b1b * JB_SVF_Q105) >> 15) * 29491) >> 15) + ((((e->b2b * JB_SVF_Q240) >> 15) * 16384) >> 15);
        e->blp += (JB_LP3200 * (o - e->blp)) >> 15;
        L[n] += (e->blp * 18022) >> 15; R[n] += (e->blp * 27525) >> 15;
    }
}

static int32_t clampbus(int32_t x) { return x > 100000 ? 100000 : (x < -100000 ? -100000 : x); }
/* soft clip: u in Q15, |u| <= 1.5 maps to |y| <= 1 (y = u - 4u^3/27) */
static int32_t softclip(int32_t u) {
    if (u > 49151) u = 49151;
    if (u < -49151) u = -49151;
    int32_t h = u >> 1, u2 = (h * h) >> 13, u3 = ((u2 >> 1) * u) >> 14;   /* halved first: no 32-bit overflow for |u| <= 49151 */
    return u - ((u3 * 4855) >> 15);
}

static void jb_render_stereo(int pos, int16_t *l, int16_t *r, const int16_t *p) {
    int32_t L[OP1_FX_BLOCK], R[OP1_FX_BLOCK];
    band_block(pos, p, L, R);
    int32_t vol = S[pos % 3].s_muted ? 0 : op1_fx_param_q15(p, 1);
    int32_t from = S[pos % 3].band_gain;
    for (int n = 0; n < OP1_FX_BLOCK; n++) {
        int32_t gain = from + ((vol - from) * (n + 1)) / OP1_FX_BLOCK;
        int32_t bl = softclip((clampbus(L[n]) * 19661) >> 15), br = softclip((clampbus(R[n]) * 19661) >> 15);
        l[n] = op1_sat16(l[n] + ((bl * gain) >> 15));
        r[n] = op1_sat16(r[n] + ((br * gain) >> 15));
    }
    S[pos % 3].band_gain = vol;
}
static void jb_render(int pos, int16_t *buf, const int16_t *p) {     /* synth / drum FX slots: mono */
    int32_t L[OP1_FX_BLOCK], R[OP1_FX_BLOCK];
    band_block(pos, p, L, R);
    int32_t vol = S[pos % 3].s_muted ? 0 : op1_fx_param_q15(p, 1);
    int32_t from = S[pos % 3].band_gain;
    for (int n = 0; n < OP1_FX_BLOCK; n++) {
        int32_t gain = from + ((vol - from) * (n + 1)) / OP1_FX_BLOCK;
        buf[n] = op1_sat16(buf[n] + ((softclip((clampbus((L[n] + R[n]) >> 1) * 19661) >> 15) * gain) >> 15));
    }
    S[pos % 3].band_gain = vol;
}

const op1_fx_t FX_jazzband = { jb_init, jb_render,
                               { OP1_PRESS_LATCH, OP1_PRESS_LATCH, OP1_FX_STEPS(127), OP1_FX_STEPS(127) },
                               jb_render_stereo };

/* ---------------------------------------------------------------- screen
 * GUI thread. jazzband.svg is an empty frame; everything is stroke text and lines drawn here. */
#include "seq/seq_text.h"

static const uint8_t C_BLUE[4] = {0x4D, 0x9E, 0xFF, 0xFF}, C_GREEN[4] = {0x00, 0xED, 0x95, 0xFF},
                     C_WHITE[4] = {0xFF, 0xFF, 0xFF, 0xFF}, C_RED[4] = {0xFF, 0x3A, 0x5D, 0xFF},
                     C_GREY[4] = {0x6F, 0x71, 0x73, 0xFF};
static const uint8_t *const KNOBC[4] = {C_BLUE, C_GREEN, C_WHITE, C_RED};
static const char *const ROOTS[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
/* '@': a raised small circle (diminished, as C°7), '&': a slashed circle (half-diminished, as Cø7); drawn in jb_text_n */
static const char *const QUALS[Q_NUM] = {"MA7", "-7", "7", "&7", "@7", "-6", "6", "7b9", "-MA7"};
static void jb_ring(void *r, int32_t cx, int32_t cy, int32_t a, int32_t w, const uint8_t *c) {   /* an octagon, radius a */
    int32_t b = (a * 5) / 12;
    const int32_t px[8] = {a, b, -b, -a, -a, -b, b, a}, py[8] = {b, a, a, b, -b, -a, -a, -b};
    for (int i = 0; i < 8; i++) { int j = (i + 1) & 7; sq_line(r, cx + px[i], cy + py[i], cx + px[j], cy + py[j], w, c); }
}

/* stroke text with a drawn flat sign for 'b'; unknown characters leave a gap; returns the x after the text */
static int32_t jb_text_n(void *r, int32_t x, int32_t y, int32_t s, const char *t, int n, int32_t w, const uint8_t *c) {
    for (int i = 0; i < n && t[i]; i++) {
        char ch = t[i];
        if (ch == 'b') {
            int32_t h = s >> 1;
            sq_line(r, x + s, y, x + s, y + 6 * s, w, c);
            sq_line(r, x + s, y + 6 * s, x + 3 * s, y + 9 * h, w, c);
            sq_line(r, x + 3 * s, y + 9 * h, x + 3 * s, y + 7 * h, w, c);
            sq_line(r, x + 3 * s, y + 7 * h, x + s, y + 7 * h, w, c);
            x += 4 * s;
        } else if (ch == '@') {                          /* degree: small, at the top */
            jb_ring(r, x + (s * 3) / 2, y + (s * 3) / 2, (s * 3) / 2, w, c);
            x += 4 * s;
        } else if (ch == '&') {                          /* o with a slash, full height */
            jb_ring(r, x + 2 * s, y + 3 * s, (s * 5) / 2, w, c);
            sq_line(r, x, y + 6 * s, x + 4 * s, y, w, c);
            x += 6 * s;
        } else if (ch == ' ') {
            x += 4 * s;
        } else {
            if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
            char one[2] = {ch, 0};
            x = sq_text(r, x, y, s, one, w, c);
        }
    }
    return x;
}
static int32_t jb_text(void *r, int32_t x, int32_t y, int32_t s, const char *t, int32_t w, const uint8_t *c) {
    return jb_text_n(r, x, y, s, t, 1 << 30, w, c);
}
static int32_t text_w(int32_t s, const char *t) {   /* width of jb_text, for right alignment */
    int32_t x = 0;
    for (; *t; t++) x += (*t == 'b' || *t == ' ' || *t == '@' ? 4 : (*t == '1' || *t == '.' ? 4 : 6)) * s;
    return x;
}
/* chord: root big, quality smaller and raised; returns the x after it */
static int32_t draw_chord(void *r, int32_t x, int32_t y, int32_t s, chord_t c, int32_t w, const uint8_t *col) {
    x = jb_text(r, x, y, s, ROOTS[c.root], w, col);
    return jb_text(r, x + s / 2, y, s / 2, QUALS[c.q], w * 3 / 4, col);
}

static int registered;
static void jb_screen_init(void *scr) {
    (void)scr;
    if (registered) return;
    registered = 1;
    op1_fx_push_bit(&FX_jazzband, 2, 7);                                                    /* white push */
    op1_fx_push_bit(&FX_jazzband, 3, 15);                                                   /* red push: transport */
    for (int k = 0; k < 4; k++) op1_fx_shift_ext(&FX_jazzband, k, 1, LVL_DEFAULT[k]);          /* Shift: the mixer */
}

static const uint8_t C_BLACK[4] = {0x00, 0x00, 0x00, 0xFF};
static void fill(void *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {   /* a black box (thick lines) */
    for (int32_t y = y0 + OP1_Q10(2); y <= y1; y += OP1_Q10(3)) sq_line(r, x0, y, x1, y, OP1_Q10(4), C_BLACK);
}
static void box(void *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, const uint8_t *c) {
    sq_line(r, x0, y0, x1, y0, w, c); sq_line(r, x0, y1, x1, y1, w, c); sq_line(r, x0, y0, x0, y1, w, c); sq_line(r, x1, y0, x1, y1, w, c);
}
static void arrow(void *r, int32_t x, int32_t y, const uint8_t *c) {   /* the row cursor */
    sq_line(r, x, y, x, y + OP1_Q10(9), OP1_Q10(2), c);
    sq_line(r, x, y, x + OP1_Q10(6), y + OP1_Q10(4), OP1_Q10(2), c);
    sq_line(r, x, y + OP1_Q10(9), x + OP1_Q10(6), y + OP1_Q10(5), OP1_Q10(2), c);
}
static void rtext(void *r, int32_t xr, int32_t y, int32_t s, const char *t, const uint8_t *c) { jb_text(r, xr - text_w(s, t), y, s, t, s, c); }
static void key_name(char *p, int key, int minor, int off) { p = sq_cat(p, ROOTS[pcof(key + off)]); if (minor) sq_cat(p, "-"); }
static int red_next(const jb_t *e) {                     /* what a red press does now: 0 play, 1 finish, 2 stop */
    if (e->s_state == ST_STOP) return 0;
    if (e->s_state == ST_PLAY && e->s_kind == BK_CHART && !e->s_ending) return 1;
    return 2;
}
static const char *const RED_DO[3] = {"PUSH:PLAY", "PUSH:FINISH", "PUSH:STOP"};
/* the knob legend: one column per knob in its colour; top line what a turn shows, bottom line the press */
static void legend(void *r, const char *const top[4], const char *const bot[4]) {
    sq_line(r, OP1_Q10(12), OP1_Q10(138), OP1_Q10(308), OP1_Q10(138), OP1_Q10(1), C_GREY);
    for (int k = 0; k < 4; k++) {
        int32_t x = OP1_Q10(12 + k * 76);
        if (top[k] && top[k][0]) jb_text(r, x, OP1_Q10(142), OP1_Q10(1), top[k], OP1_Q10(1), KNOBC[k]);
        if (bot[k] && bot[k][0]) jb_text(r, x, OP1_Q10(153), 819, bot[k], 819, C_GREY);
    }
}
static void vol_text(char *p, const jb_t *e) {
    if (e->s_muted) { sq_cat(p, "MUTED"); return; }
    p = sq_cat(p, "VOL "); p = sq_num(p, (uint32_t)((e->s_volume * 100 + 16383) / 32767), 1); sq_cat(p, "%");
}

/* Shift held: the mixer (Shift + blue / green / white / red = drums / bass / piano / guitar level) */
static void draw_shift(void *r, const jb_t *e) {
    static const char *const LN[4] = {"DRUMS", "BASS", "PIANO", "GUITAR"};
    char buf[24], *p;
    jb_text(r, OP1_Q10(12), OP1_Q10(8), 1229, "BAND MIX", 1229, C_WHITE);
    jb_text(r, OP1_Q10(12), OP1_Q10(22), OP1_Q10(1), "SHIFT + TURN: LEVELS   RELEASE: BACK", OP1_Q10(1), C_GREY);
    for (int i = 0; i < 4; i++) {
        int32_t y = OP1_Q10(34 + i * 30), pct = (e->lvl[i] * 100 + 16384) >> 15;
        jb_text(r, OP1_Q10(16), y, OP1_Q10(2), LN[i], OP1_Q10(2), KNOBC[i]);
        int32_t x0 = OP1_Q10(110), x1 = x0 + (OP1_Q10(150) * (pct > 125 ? 125 : pct)) / 125;
        sq_line(r, x0, y + OP1_Q10(6), OP1_Q10(260), y + OP1_Q10(6), OP1_Q10(1), C_GREY);
        if (x1 > x0) sq_line(r, x0, y + OP1_Q10(6), x1, y + OP1_Q10(6), OP1_Q10(5), KNOBC[i]);
        p = buf; if (!pct) p = sq_cat(p, "MUTE"); else { p = sq_num(p, (uint32_t)pct, 1); p = sq_cat(p, "%"); }
        jb_text(r, OP1_Q10(272), y, OP1_Q10(1), buf, OP1_Q10(1), pct ? C_WHITE : C_RED);
    }
}

/* the switcher (blue press): PLAY / SONGS / SETUP; it stays until a choice or blue again */
static void draw_switcher(void *r, const jb_t *e) {
    static const char *const SN[SCR_N] = {"PLAY", "SONGS", "SETUP"};
    static const char *const SH[SCR_N] = {"CHORDS - BARS - KEY - TEMPO", "SET LISTS AND SONGS", "PRACTICE - ARRANGEMENT - BAND - SONG"};
    char buf[32];
    jb_text(r, OP1_Q10(12), OP1_Q10(8), 1229, "SCREENS", 1229, C_WHITE);
    {   const char *st = e->s_state == ST_STOP ? "STOPPED" : e->s_state == ST_PLAY ? "PLAYING" : "STARTING";
        char *p = buf; p = sq_cat(p, st);
        if (e->s_state == ST_PLAY && e->s_kind == BK_CHART) { p = sq_cat(p, "  BAR "); sq_num(p, (uint32_t)(e->s_bar + 1), 1); }
        rtext(r, OP1_Q10(308), OP1_Q10(9), 1024, buf, e->s_state == ST_STOP ? C_GREY : C_GREEN); }
    for (int i = 0; i < SCR_N; i++) {
        int32_t y = OP1_Q10(30 + i * 30);
        int sel = i == e->sw_row;
        if (sel) { arrow(r, OP1_Q10(14), y + OP1_Q10(2), C_WHITE); sq_line(r, OP1_Q10(28), y + OP1_Q10(19), OP1_Q10(308), y + OP1_Q10(19), OP1_Q10(1), C_GREY); }
        jb_text(r, OP1_Q10(30), y, OP1_Q10(2), SN[i], OP1_Q10(2), sel ? C_WHITE : C_GREY);
        jb_text(r, OP1_Q10(120), y + OP1_Q10(5), 819, SH[i], 819, C_GREY);
        if (i == e->screen) rtext(r, OP1_Q10(308), y + OP1_Q10(5), 1024, "NOW", C_BLUE);
    }
    jb_text(r, OP1_Q10(12), OP1_Q10(124), 1024, "TURN WHITE: CHOOSE   PUSH WHITE: OPEN", 1024, C_WHITE);
    char vb[16]; vol_text(vb, e);
    const char *top[4] = {"", vb, "CHOOSE", ""}, *bot[4] = {"PUSH:CANCEL", e->s_muted ? "PUSH:HEAR" : "PUSH:MUTE", "PUSH:OPEN", RED_DO[red_next(e)]};
    legend(r, top, bot);
}

/* SONGS: the set lists and songs. Browsing never loads; a white press loads (stopped) or queues (playing). */
static void draw_songs(void *r, const jb_t *e) {
    char buf[40], *p;
    int q = e->s_pend >= 0, v = e->s_view, nrows = q + view_rows(v), row = e->s_row, playing = e->s_state >= ST_COUNT;
    if (row < 0 || row >= nrows) row = 0;
    int inset = v >= 0 && v < SETS.n, cur = row < q ? -2 : row_song(v, row - q);
    if (v == VIEW_MENU) jb_text(r, OP1_Q10(12), OP1_Q10(8), 1229, "SET LISTS", 1229, C_WHITE);
    else if (inset) jb_text_n(r, OP1_Q10(12), OP1_Q10(8), 1229, B.bt + SETS.name[v], SETS.nlen[v], 1229, C_WHITE);
    else jb_text(r, OP1_Q10(12), OP1_Q10(8), 1229, SETS.n ? "ALL SONGS" : !B.ok ? "BUILT-IN SONGS" : B.src == BOOK_USB ? "USB SONGBOOK" : "SLOT 5 SONGBOOK", 1229, C_WHITE);
    p = buf;
    if (v == VIEW_MENU) { p = sq_num(p, (uint32_t)SETS.n, 1); p = sq_cat(p, SETS.n == 1 ? " SET" : " SETS"); }
    else if (cur >= 0) { int back = inset || SETS.n; p = sq_num(p, (uint32_t)(row - q + !back), 1); p = sq_cat(p, " / "); p = sq_num(p, (uint32_t)(nrows - q - back), 1); }
    else *p = 0;
    rtext(r, OP1_Q10(308), OP1_Q10(8), 1229, buf, C_GREY);
    int show = nrows < 5 ? nrows : 5, first = nrows <= 5 ? 0 : row - 2;
    for (int k = 0; k < show; k++) {
        int i = ((first + k) % nrows + nrows) % nrows;
        int32_t y = OP1_Q10(26 + k * 17);
        int sel = i == row;
        if (sel) { arrow(r, OP1_Q10(12), y, C_WHITE); sq_line(r, OP1_Q10(24), y + OP1_Q10(13), OP1_Q10(308), y + OP1_Q10(13), OP1_Q10(1), C_GREY); }
        if (i < q) {                                     /* CANCEL QUEUE: the queued song */
            const char *t, *c; int tl, key, minor, cl;
            song_info(e->s_pend, &t, &tl, &key, &minor, &c, &cl);
            int32_t x = jb_text(r, OP1_Q10(26), y, 1126, "CANCEL QUEUE: ", 1126, sel ? C_WHITE : C_RED);
            int fit = (OP1_Q10(306) - x) / (6 * 1126); if (tl > fit) tl = fit;
            jb_text_n(r, x, y, 1126, t, tl, 1126, sel ? C_WHITE : C_RED);
            continue;
        }
        int song = row_song(v, i - q);
        if (v == VIEW_MENU) {
            p = buf; p = sq_num(p, (uint32_t)(i - q < SETS.n ? SETS.count[i - q] : nsongs()), 1);
            rtext(r, OP1_Q10(306), y, 1126, buf, C_GREY);
            if (i - q < SETS.n) jb_text_n(r, OP1_Q10(26), y, 1229, B.bt + SETS.name[i - q], SETS.nlen[i - q], 1229, sel ? C_WHITE : C_GREY);
            else jb_text(r, OP1_Q10(26), y, 1229, "ALL SONGS", 1229, sel ? C_WHITE : C_GREY);
            continue;
        }
        if (song < 0) { jb_text(r, OP1_Q10(26), y, 1126, "BACK TO SET LISTS", 1126, sel ? C_WHITE : C_GREY); continue; }
        int now = song == e->s_song && (!inset || e->s_set != v || e->s_spos == i - q - 1), queued = song == e->s_pend;
        const uint8_t *col = sel ? C_WHITE : now ? C_GREEN : queued ? C_RED : C_GREY;
        p = buf; p = sq_num(p, (uint32_t)(inset ? i - q : song + 1), 1);
        jb_text(r, OP1_Q10(26), y, 1126, buf, 1126, now ? C_GREEN : C_GREY);
        const char *t, *c; int tl, key, minor, cl;
        song_info(song, &t, &tl, &key, &minor, &c, &cl);
        int fit = (OP1_Q10(270) - OP1_Q10(56)) / (6 * 1229); if (tl > fit) tl = fit;
        jb_text_n(r, OP1_Q10(56), y, 1229, t, tl, 1229, col);
        if (now) rtext(r, OP1_Q10(306), y + OP1_Q10(2), 819, playing ? "PLAYS" : "LOADED", C_GREEN);
        else if (queued) rtext(r, OP1_Q10(306), y + OP1_Q10(2), 819, "NEXT", C_RED);
    }
    if (cur >= 0) {                                      /* the highlighted song's key and tempo */
        const char *t, *c; int tl, key, minor, cl;
        song_info(cur, &t, &tl, &key, &minor, &c, &cl);
        p = buf; p = sq_cat(p, "KEY "); key_name(p, key, minor, 0); while (*p) p++;
        { int bpm = play_bpm(cur); if (bpm) { p = sq_cat(p, "   "); p = sq_num(p, (uint32_t)bpm, 1); p = sq_cat(p, " BPM"); } }
        jb_text(r, OP1_Q10(26), OP1_Q10(112), 1024, buf, 1024, C_GREY);
    }
    const char *what; const uint8_t *wc = C_RED;
    if (cur == -2) what = "PUSH WHITE: CANCEL THE QUEUED SONG";
    else if (v == VIEW_MENU) what = row - q < SETS.n ? "PUSH WHITE: OPEN THIS SET" : "PUSH WHITE: SHOW EVERY SONG";
    else if (cur < 0) what = "PUSH WHITE: BACK TO SET LISTS";
    else if (!playing) { what = cur == e->s_song ? "LOADED - PUSH RED TO PLAY" : "PUSH WHITE: LOAD THIS SONG"; if (cur == e->s_song) wc = C_GREEN; }
    else if (cur == e->s_song) { what = q ? "PUSH WHITE: KEEP PLAYING THIS" : "NOW PLAYING"; wc = C_GREEN; }
    else if (cur == e->s_pend) what = "QUEUED FOR THE NEXT BAR";
    else what = "PUSH WHITE: PLAY IT FROM THE NEXT BAR";
    jb_text(r, OP1_Q10(12), OP1_Q10(124), 1024, what, 1024, wc);
    char vb[16]; vol_text(vb, e);
    const char *top[4] = {"", vb, "BROWSE", ""}, *bot[4] = {"PUSH:SCREENS", e->s_muted ? "PUSH:HEAR" : "PUSH:MUTE", "PUSH:SELECT", RED_DO[red_next(e)]};
    legend(r, top, bot);
}

/* SETUP: categories, then rows (BACK first). White picks a row, red turns its value, white press opens or acts. */
static const char *const SC_NAME[SC_N] = {"PRACTICE", "ARRANGEMENT", "BAND", "SONG"};
static const char *const SC_HINT[SC_N] = {"LOOP - BARS - KEY CYCLE - CHORUSES", "FEEL - INTRO - TURNAROUND - ENDING",
                                          "PLAYER AND SOUND PER INSTRUMENT", "RESET KEY AND TEMPO"};
static const char *const R_NAME[R_N] = {"BACK", "LOOP", "LOOP FROM", "LOOP TO", "KEY CYCLE", "CHORUSES", "FEEL", "INTRO",
    "TURNAROUND", "ENDING", "DRUMS", "BASS", "PIANO", "GUITAR", "RESET KEY", "RESET TEMPO"};
static void bar_range(char *p, int a, int b) { p = sq_num(p, (uint32_t)(a + 1), 1); p = sq_cat(p, "-"); sq_num(p, (uint32_t)(b + 1), 1); }
static void setup_value(const jb_t *e, int r, char *buf, const uint8_t **col, const char **note) {
    static const char *const KS[KS_N] = {"OFF", "UP 1/2 STEP", "DOWN 1/2 STEP", "UP A 4TH"};
    static const char *const TA[TA_N] = {"OFF", "II-V", "III-VI-II-V"};
    char *p = buf; *p = 0; *col = C_WHITE; *note = 0;
    switch (r) {
    case R_BACK: *col = C_GREY; break;
    case R_LOOP:
        if (!looping(e)) { sq_cat(p, "OFF"); *col = C_GREY; }
        else if (e->loop <= e->nsec) { char t[2] = {e->sec_ch[e->loop - 1], 0}; p = sq_cat(p, t); p = sq_cat(p, " SECTION  "); bar_range(p, loop_lo(e), loop_hi(e)); }
        else { p = sq_cat(p, "BARS "); bar_range(p, loop_lo(e), loop_hi(e)); }
        break;
    case R_FROM: case R_TO:
        p = sq_cat(p, "BAR "); sq_num(p, (uint32_t)((r == R_FROM ? e->lfrom : e->lto) + 1), 1);
        if (e->loop != e->nsec + 1) { *col = C_GREY; *note = "CHANGING IT LOOPS THESE BARS"; }
        break;
    case R_KSTEP: sq_cat(p, KS[e->kstep % KS_N]); if (!e->kstep) *col = C_GREY; break;
    case R_CHORUS: if (!e->choruses) { sq_cat(p, "ENDLESS"); *col = C_GREY; } else sq_num(p, (uint32_t)e->choruses, 1); break;
    case R_FEEL: sq_cat(p, e->ballad ? "BALLAD" : e->bossa ? "BOSSA NOVA" : "SWING"); break;
    case R_INTRO: sq_cat(p, e->intro == IN_LAST4 ? "LAST 4 BARS" : e->intro == IN_TURN ? "I-VI-II-V" : "COUNT-IN"); if (!e->intro) *col = C_GREY; break;
    case R_TURN: sq_cat(p, TA[e->turn % TA_N]); if (!e->turn) *col = C_GREY; break;
    case R_END: sq_cat(p, e->endtype ? "TAG + FINAL CHORD" : "FINAL CHORD"); break;
    case R_DPLAY: case R_BPLAY: case R_PPLAY: case R_GPLAY: {   /* the player; a missing pack shows here (a synth stands in) */
        int i = r - R_DPLAY, st = e->sty[i] < NPLAY[i] ? e->sty[i] : 0;
        int have = PKS.ok[i] || (i != I_DRUMS && IS[INST_IDX(i)].ok);
        sq_cat(p, STY_NAME[i][st]); *note = STY_HINT[i][st];
        if (i == I_GUITAR && st == GT_GILBERTO && PKS.state >= 2) { if (PKS.ok[PK_NYLON]) { have = 1; *note = "BOSSA BATIDA - NYLON STRINGS"; } else if (have) *note = "BOSSA BATIDA - ADD JBNYLON FOR NYLON"; }
        if (!have && PKS.state >= 2) { *note = "NO SOUND PACK - A STAND-IN SYNTH PLAYS"; *col = C_RED; }
        break;
    }
    case R_RKEY: {
        const char *t, *c; int tl, key, minor, cl;
        song_info(e->s_song < nsongs() ? e->s_song : 0, &t, &tl, &key, &minor, &c, &cl);
        p = sq_cat(p, "TO "); key_name(p, key, minor, 0); while (*p) p++;
        p = sq_cat(p, "  NOW "); key_name(p, key, minor, e->s_transpose + e->s_shift);
        *col = wrapk(e->s_transpose + e->s_shift) ? C_WHITE : C_GREY; break;
    }
    case R_RTEMPO:
        p = sq_cat(p, "TO "); p = sq_num(p, (uint32_t)song_bpm(e->s_song), 1); p = sq_cat(p, "  NOW "); sq_num(p, (uint32_t)e->s_bpm, 1);
        *col = e->s_bpm != song_bpm(e->s_song) ? C_WHITE : C_GREY; break;
    }
}
static void draw_setup(void *r, const jb_t *e) {
    char buf[48], *p;
    int c = e->scat;
    p = buf; p = sq_cat(p, "SETUP"); if (c >= 0) { p = sq_cat(p, " - "); sq_cat(p, SC_NAME[c]); }
    jb_text(r, OP1_Q10(12), OP1_Q10(8), 1229, buf, 1229, C_WHITE);
    rtext(r, OP1_Q10(308), OP1_Q10(9), 1024, c < 0 ? "UNTIL RESTART" : c == SC_BAND ? "ALL SONGS - SESSION" : "THIS SONG - SESSION", C_GREY);
    const char *note = 0; int act = 0;
    if (c < 0) {
        for (int i = 0; i < SC_N; i++) {
            int32_t y = OP1_Q10(26 + i * 22); int sel = i == e->srow;
            if (sel) { arrow(r, OP1_Q10(12), y, C_WHITE); sq_line(r, OP1_Q10(24), y + OP1_Q10(17), OP1_Q10(308), y + OP1_Q10(17), OP1_Q10(1), C_GREY); }
            jb_text(r, OP1_Q10(26), y, 1229, SC_NAME[i], 1229, sel ? C_WHITE : C_GREY);
            jb_text(r, OP1_Q10(26), y + OP1_Q10(10), 819, SC_HINT[i], 819, C_GREY);
        }
        jb_text(r, OP1_Q10(12), OP1_Q10(124), 1024, "PUSH WHITE: OPEN", 1024, C_RED);
    } else {
        int n = setup_rows(c), row = e->srow, show = n < 5 ? n : 5, first = n <= 5 ? 0 : clampi(row - 2, 0, n - 5);   /* in order, BACK on top */
        for (int k = 0; k < show; k++) {
            int i = ((first + k) % n + n) % n, rr = setup_row(c, i), sel = i == row;
            int32_t y = OP1_Q10(26 + k * 18);
            if (sel) { arrow(r, OP1_Q10(12), y, C_WHITE); sq_line(r, OP1_Q10(24), y + OP1_Q10(14), OP1_Q10(308), y + OP1_Q10(14), OP1_Q10(1), C_GREY); }
            jb_text(r, OP1_Q10(26), y, 1126, R_NAME[rr], 1126, sel ? C_WHITE : C_GREY);
            const uint8_t *col; const char *nt;
            setup_value(e, rr, buf, &col, &nt);
            if (buf[0]) rtext(r, OP1_Q10(306), y, 1126, buf, sel && col == C_GREY ? C_WHITE : col);
            if (sel) { note = nt; act = rr; }
        }
        if (note) jb_text(r, OP1_Q10(26), OP1_Q10(116), 819, note, 819, C_GREY);
        const char *what = act == R_BACK ? "PUSH WHITE: BACK TO SETUP" : act == R_RKEY ? "PUSH WHITE: RESET THE KEY"
                         : act == R_RTEMPO ? "PUSH WHITE: RESET THE TEMPO" : "TURN RED: CHANGE THIS";
        jb_text(r, OP1_Q10(12), OP1_Q10(124), 1024, what, 1024, act == R_BACK || act == R_RKEY || act == R_RTEMPO ? C_WHITE : C_RED);
    }
    char vb[16]; vol_text(vb, e);
    int val = c >= 0 && act != R_BACK && act != R_RKEY && act != R_RTEMPO;
    const char *top[4] = {"", vb, "ROW", val ? "VALUE" : ""}, *bot[4] = {"PUSH:SCREENS", e->s_muted ? "PUSH:HEAR" : "PUSH:MUTE",
                         c < 0 ? "PUSH:OPEN" : act == R_BACK ? "PUSH:BACK" : act == R_RKEY || act == R_RTEMPO ? "PUSH:RESET" : "", RED_DO[red_next(e)]};
    legend(r, top, bot);
}

/* PLAY: the chord now and next, the chart with the bar cursor, the form and anything queued for the next bar */
static void draw_play(void *r, const jb_t *e) {
    int state = e->s_state, bar = e->s_bar, beat = e->s_beat & 3;
    if (e->nbars <= 0) return;
    if (bar >= e->nbars || bar < 0) bar = 0;
    char buf[64], *p;
    const char *t, *c; int tl, key, minor, cl;
    song_info(e->s_song < nsongs() ? e->s_song : 0, &t, &tl, &key, &minor, &c, &cl);
    int actual = wrapk(e->s_transpose + e->s_shift);

    /* top: number and title left; the actual key right (big), its offset smaller */
    {
        p = buf;
        if (e->s_set >= 0 && e->s_set < SETS.n) { p = sq_num(p, (uint32_t)(e->s_spos + 1), 1); p = sq_cat(p, "/"); p = sq_num(p, SETS.count[e->s_set], 1); }
        else p = sq_num(p, (uint32_t)(e->s_song + 1), 1);
        sq_cat(p, " ");
        int32_t x = jb_text(r, OP1_Q10(12), OP1_Q10(10), 1229, buf, 1229, B.ok ? C_GREEN : C_GREY);
        char kb[16]; p = kb; p = sq_cat(p, "KEY "); key_name(p, key, minor, actual);
        int32_t kx = OP1_Q10(308) - text_w(1434, kb);
        jb_text(r, kx, OP1_Q10(9), 1434, kb, 1229, C_WHITE);
        if (actual) {
            char ob[8]; p = ob; p = sq_cat(p, actual > 0 ? "+" : "-"); sq_num(p, (uint32_t)iabs(actual), 1);
            kx -= text_w(1024, ob) + OP1_Q10(4);
            jb_text(r, kx, OP1_Q10(11), 1024, ob, 1024, C_BLUE);
        }
        int fit = (kx - OP1_Q10(8) - x) / (6 * 1229);
        if (fit < 0) fit = 0;
        if (tl > fit) tl = fit;
        jb_text_n(r, x, OP1_Q10(10), 1229, t, tl, 1229, C_GREY);
    }

    /* status: where the band is (left), the form (right) */
    if (state == ST_STOP || state == ST_ARMED) {
        jb_text(r, OP1_Q10(12), OP1_Q10(26), 1229, state == ST_ARMED ? "STARTING" : e->nb == 3 ? "STOPPED  3/4" : "STOPPED", 1229, C_RED);
    } else {
        int kind = e->s_kind;
        p = buf;
        if (state == ST_PLAY && kind == BK_TAG) { p = sq_cat(p, "TAG "); p = sq_num(p, (uint32_t)(e->tag_i + 1), 1); p = sq_cat(p, "/3"); }
        else if (state == ST_PLAY && kind == BK_INTRO) { p = sq_cat(p, "INTRO "); p = sq_num(p, (uint32_t)(e->intro_i + 1), 1); p = sq_cat(p, "/4"); }
        else if (state == ST_PLAY && kind != BK_CHART) p = sq_cat(p, "ENDING");
        else {
            p = sq_cat(p, "BAR "); p = sq_num(p, (uint32_t)(state == ST_COUNT ? 0 : bar + 1), 2);
            char sec = state == ST_PLAY ? e->bars[bar].section : 0;
            if (sec) { p = sq_cat(p, " "); *p++ = sec; *p = 0; }
            if (e->nb == 3) p = sq_cat(p, " 3/4");
        }
        jb_text(r, OP1_Q10(12), OP1_Q10(26), 1229, state == ST_COUNT ? "COUNT IN" : buf, 1229, kind == BK_INTRO ? C_BLUE : kind != BK_CHART ? C_RED : C_WHITE);
        for (int i = 0; i < e->nb; i++) {
            int on = i == beat;
            int32_t cx = OP1_Q10(112 + i * 12), cy = OP1_Q10(31), h = on ? OP1_Q10(4) : OP1_Q10(2);
            sq_line(r, cx - h, cy, cx + h, cy, on ? OP1_Q10(6) : OP1_Q10(2), on ? KNOBC[i] : C_GREY);
        }
    }
    {   /* the form: muted, finishing, the loop and its key cycle, the chorus count; stopped: where red starts */
        const uint8_t *col = C_GREY; p = buf; *p = 0;
        int play = state >= ST_COUNT;
        if (e->s_muted) { sq_cat(p, "BAND MUTED"); col = C_GREEN; }
        else if (play && (e->s_ending || (e->s_kind != BK_CHART && e->s_kind != BK_INTRO))) { sq_cat(p, e->s_kind == BK_CHART ? "LAST TIME" : "FINISHING"); col = C_RED; }
        else if (looping(e)) {
            p = sq_cat(p, "LOOP ");
            if (e->loop <= e->nsec) { char s1[2] = {e->sec_ch[e->loop - 1], 0}; p = sq_cat(p, s1); } else { bar_range(p, loop_lo(e), loop_hi(e)); while (*p) p++; }
            if (e->kstep) { p = sq_cat(p, " CYCLE "); p = sq_num(p, (uint32_t)(e->s_pass % 12 + 1), 1); p = sq_cat(p, "/12"); }
            else if (play) { p = sq_cat(p, " X"); p = sq_num(p, (uint32_t)(e->s_pass + 1), 1); }
            col = C_BLUE;
        } else if (play && e->s_kind != BK_INTRO) {
            p = sq_cat(p, "CHORUS "); p = sq_num(p, (uint32_t)e->s_chorus, 1);
            if (e->choruses) { p = sq_cat(p, "/"); p = sq_num(p, (uint32_t)e->choruses, 1); }
            col = e->choruses && e->s_chorus >= e->choruses ? C_RED : C_GREEN;
        } else if (!play && e->sbar > 0) { p = sq_cat(p, "STARTS AT BAR "); sq_num(p, (uint32_t)(e->sbar + 1), 1); col = C_BLUE; }
        else if (!play && e->choruses) { p = sq_cat(p, "PLAY "); p = sq_num(p, (uint32_t)e->choruses, 1); sq_cat(p, e->choruses == 1 ? " CHORUS" : " CHORUSES"); }
        if (buf[0]) rtext(r, OP1_Q10(308), OP1_Q10(26), 1024, buf, col);
    }

    /* now / next, as the band plays them (loop, key cycle, turnaround, tag, final chord, a queued key) */
    int playing_now = state == ST_PLAY;
    int start_bar = e->sbar >= 0 && e->sbar < e->nbars ? e->sbar : looping(e) ? loop_lo(e) : 0;
    chord_t now = playing_now ? cur_chord(e, beat) : chord_at(e, start_bar, 0);
    chord_t nxt = now;
    if (playing_now) {
        int found = 0;
        for (int k = beat + 1; k < e->nb && !found; k++) { chord_t c1 = cur_chord(e, k); if (!ceq(c1, now)) { nxt = c1; found = 1; } }
        for (int k = 0; k < e->nb && !found; k++) {
            chord_t c1 = e->vbn.beat[k]; c1.root = (int8_t)pcof(c1.root + next_transpose(e));
            if (!ceq(c1, now) || k == 0) { nxt = c1; found = !ceq(c1, now); }
        }
    }
    if (state == ST_COUNT) {
        p = buf; sq_num(p, (uint32_t)(beat + 1), 1);
        jb_text(r, OP1_Q10(16), OP1_Q10(40), 3482, buf, OP1_Q10(2), C_WHITE);
        nxt = cur_chord(e, 0);
    } else draw_chord(r, OP1_Q10(16), OP1_Q10(40), 3482, now, OP1_Q10(2), state == ST_PLAY ? C_WHITE : C_GREY);
    jb_text(r, OP1_Q10(214), OP1_Q10(40), OP1_Q10(1), "NEXT", OP1_Q10(1), C_GREY);
    if (e->s_bpm && e->orig_bpm && e->orig_bpm != e->s_bpm) {   /* a tempo turned this session: the songbook's beside it */
        p = buf; p = sq_cat(p, "ORIG "); p = sq_num(p, (uint32_t)e->orig_bpm, 1); sq_cat(p, " BPM");
        rtext(r, OP1_Q10(308), OP1_Q10(41), 819, buf, C_GREY);
    }
    draw_chord(r, OP1_Q10(214), OP1_Q10(50), 2048, nxt, 1229, C_GREY);

    /* queued for the next bar: a song, a jump, a key; KEY CYCLE OFF after a manual key */
    {
        p = buf; *p = 0; int any = 0;
        if (e->s_pend >= 0) {
            const char *qt, *qc; int ql, qk, qm, qcl;
            song_info(e->s_pend, &qt, &ql, &qk, &qm, &qc, &qcl);
            p = sq_cat(p, "SONG: "); if (ql > 14) ql = 14; for (int i = 0; i < ql; i++) { char ch = qt[i]; *p++ = ch >= 'a' && ch <= 'z' ? (char)(ch - 32) : ch; } *p = 0;
            any = 1;
        } else if (e->jump >= 0 && state >= ST_COUNT) { p = sq_cat(p, "JUMP: BAR "); p = sq_num(p, (uint32_t)(e->jump + 1), 1); any = 1; }
        if (e->key_pend && e->s_pend < 0) {
            if (any) p = sq_cat(p, "  ");
            p = sq_cat(p, "KEY: "); key_name(p, key, minor, actual); while (*p) p++;
            p = sq_cat(p, " TO "); key_name(p, key, minor, e->key_tgt); while (*p) p++;
            any = 1;
        }
        if (any) { p = sq_cat(p, "  AT NEXT BAR"); jb_text(r, OP1_Q10(16), OP1_Q10(64), 819, buf, 819, C_RED); }
        else if (e->kmsg > 0) jb_text(r, OP1_Q10(16), OP1_Q10(64), 819, "KEY CYCLE OFF", 819, C_BLUE);
    }

    /* the chart: 3 lines of 4 bars. It follows the bar cursor while there is one, else the bar that plays. */
    sq_line(r, OP1_Q10(12), OP1_Q10(72), OP1_Q10(308), OP1_Q10(72), OP1_Q10(1), C_GREY);
    int bc = e->bcur, line0 = (bc >= 0 && bc < e->nbars ? bc : state == ST_PLAY ? bar : start_bar) & ~3, nlines = (e->nbars + 3) >> 2;
    if (nlines > 3) nlines = 3;
    for (int ln = 0; ln < nlines; ln++) {
        int first = line0 + ln * 4; while (first >= e->nbars) first -= (e->nbars + 3) & ~3;
        if (first < 0) first = 0;
        int32_t y0 = OP1_Q10(78 + ln * 20);
        for (int i = 0; i < 4 && first + i < e->nbars; i++) {
            int b = first + i, here = state == ST_PLAY && b == bar && e->s_kind == BK_CHART;
            int32_t x0 = OP1_Q10(12 + i * 74);
            const uint8_t *col = here ? C_WHITE : C_GREY;
            sq_line(r, x0, y0 - OP1_Q10(1), x0, y0 + OP1_Q10(15), OP1_Q10(1), C_GREY);
            if (here) sq_line(r, x0 + OP1_Q10(3), y0 + OP1_Q10(17), x0 + OP1_Q10(70), y0 + OP1_Q10(17), OP1_Q10(2), C_WHITE);
            if (e->bars[b].section) { char s1[2] = { e->bars[b].section, 0 }; jb_text(r, x0 + OP1_Q10(62), y0, 819, s1, 819, C_RED); }
            if (b == bc || (bc < 0 && state == ST_STOP && b == e->sbar && e->sbar > 0))   /* the bar cursor / the start bar */
                box(r, x0 + OP1_Q10(2), y0 - OP1_Q10(3), x0 + OP1_Q10(72), y0 + OP1_Q10(19), OP1_Q10(1), b == e->jump ? C_RED : C_BLUE);
            chord_t c0 = chord_at(e, b, 0), c2 = chord_at(e, b, 2);
            c0.root = (int8_t)pcof(c0.root + e->s_shift); c2.root = (int8_t)pcof(c2.root + e->s_shift);
            if (looping(e) && b >= loop_lo(e) && b <= loop_hi(e) && !here)
                sq_line(r, x0 + OP1_Q10(3), y0 + OP1_Q10(17), x0 + OP1_Q10(70), y0 + OP1_Q10(17), OP1_Q10(1), C_BLUE);
            if (ceq(c0, c2)) draw_chord(r, x0 + OP1_Q10(5), y0 + OP1_Q10(2), 1434, c0, OP1_Q10(1), col);
            else { draw_chord(r, x0 + OP1_Q10(4), y0, 1126, c0, OP1_Q10(1), col); draw_chord(r, x0 + OP1_Q10(34), y0 + OP1_Q10(7), 1126, c2, OP1_Q10(1), col); }
        }
    }

    /* the bar actions (white press): a box over the chart */
    if (e->bm) {
        static const char *const BA[3] = {"START HERE", "JUMP NEXT BAR", "CANCEL"};
        int32_t x0 = OP1_Q10(150), y0 = OP1_Q10(74), x1 = OP1_Q10(308), y1 = OP1_Q10(136);
        fill(r, x0, y0, x1, y1); box(r, x0, y0, x1, y1, OP1_Q10(1), C_WHITE);
        p = buf; p = sq_cat(p, "BAR "); sq_num(p, (uint32_t)(e->bcur + 1), 1);
        jb_text(r, x0 + OP1_Q10(6), y0 + OP1_Q10(4), 1024, buf, 1024, C_BLUE);
        int n = bar_actions(e, 0);
        for (int i = 0; i < n; i++) {
            int a = bar_actions(e, i + 1), sel = i == e->bm_row;
            int32_t y = y0 + OP1_Q10(16 + i * 13);
            if (sel) arrow(r, x0 + OP1_Q10(6), y, C_WHITE);
            jb_text(r, x0 + OP1_Q10(16), y, 1126, BA[a], 1126, sel ? C_WHITE : C_GREY);
        }
        if (n == 1) jb_text(r, x0 + OP1_Q10(16), y0 + OP1_Q10(32), 819, e->s_ending ? "FINISHING: NO JUMPS" : "NOT WHILE ENDING", 819, C_GREY);
        jb_text(r, x0 + OP1_Q10(6), y1 - OP1_Q10(9), 819, "TURN: CHOOSE  PUSH: DO IT", 819, C_GREY);
    }

    /* legend: blue key + offset, green volume, white bar, red tempo */
    char kb[16], vb[16], wb[16], rb[16];
    p = kb; p = sq_cat(p, "KEY "); p = sq_cat(p, actual >= 0 ? "+" : "-"); sq_num(p, (uint32_t)iabs(actual), 1);
    vol_text(vb, e);
    p = wb; p = sq_cat(p, "BAR "); sq_num(p, (uint32_t)((e->bcur >= 0 ? e->bcur : state == ST_PLAY ? bar : start_bar) + 1), 1);
    p = rb; if (e->s_bpm) { p = sq_num(p, (uint32_t)e->s_bpm, 1); sq_cat(p, " BPM"); } else sq_cat(p, "OP-1 BPM");
    const char *top[4] = {kb, vb, wb, rb}, *bot[4] = {"PUSH:SCREENS", e->s_muted ? "PUSH:HEAR" : "PUSH:MUTE", e->bm ? "PUSH:DO IT" : "PUSH:BAR MENU", RED_DO[red_next(e)]};
    legend(r, top, bot);
}

static void jb_screen_update(void *scr, int dt) {
    op1_screen_draw(scr, dt);
    void *r = op1_raster();
    int pos = op1_fx_screen_pos() % 3;
    const jb_t *e = &S[pos];
    if (op1_shift_held()) { draw_shift(r, e); return; }
    if (e->sw) { draw_switcher(r, e); return; }
    if (e->screen == SCR_SONGS) draw_songs(r, e);
    else if (e->screen == SCR_SETUP) draw_setup(r, e);
    else draw_play(r, e);
}

const op1_screen_t SCREEN_jazzband = { jb_screen_init, jb_screen_update };
