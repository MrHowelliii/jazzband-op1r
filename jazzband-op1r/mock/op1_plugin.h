/* Minimal host stand-in for op1r's op1_plugin.h: only the parts the jazzband plugin uses, with the same names
 * and signatures as the real SDK, so the plugin source compiles unchanged on a PC for testing. */
#ifndef OP1_PLUGIN_H
#define OP1_PLUGIN_H
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#define OP1_HOST 1
#define OP1_FX_BLOCK 64
#define OP1_SAMPLE_RATE 44100
#define OP1_FX_MASTER 2
enum { OP1_PRESS_NONE = 0, OP1_PRESS_PAGE = 1, OP1_PRESS_LATCH = 2 };

static int16_t host_sine[8192];
static inline void host_sine_init(void) { for (int i = 0; i < 8192; i++) host_sine[i] = (int16_t)lrint(32767 * sin(2 * M_PI * i / 8192)); }
static inline int32_t op1_sin(int32_t k) { return host_sine[k & 8191]; }
static inline int16_t op1_sat16(int32_t x) { return (int16_t)(x > 32767 ? 32767 : (x < -32768 ? -32768 : x)); }
static inline int32_t op1_mul16(int32_t a, int32_t b) { return (a * b) >> 15; }
static inline int32_t op1_fx_param_q15(const int16_t *p, int i) { int32_t v = p[i]; return v < 0 ? 0 : (v > 32767 ? 32767 : v); }

volatile uint8_t op1_fx_page[3][4];
volatile uint8_t op1_fx_wrap[3][4];
#define OP1_PRESS_LIST(n)   (0x40 | (n))
#define OP1_FX_STEPS(n)  (0x80 + (n))
static inline int op1_fx_list(int32_t q15, int n) { int i = (int)((q15 * (n - 1) + 16384) >> 15); return i < 0 ? 0 : i >= n ? n - 1 : i; }

/* sampler slot buffers: host stand-ins with the same accessors */
#define OP1_SAMPLE_GUARD   8
#define OP1_SAMPLE_MAX     264600
#define OP1_PEAK_BUCKETS   0x813
typedef struct { int16_t *data; int16_t peaks[OP1_PEAK_BUCKETS]; } host_sbuf_t;
static host_sbuf_t *host_sb[8];
static inline const uint8_t *op1_sample_buffer(int slot) { return (const uint8_t *)host_sb[slot & 7]; }
static inline const int16_t *op1_sample_data(const uint8_t *sb) { return ((const host_sbuf_t *)sb)->data + OP1_SAMPLE_GUARD; }
static inline const int16_t *op1_sample_peaks(const uint8_t *sb) { return ((const host_sbuf_t *)sb)->peaks; }

/* FX Shift words */
static int16_t host_sw[3][8]; static int host_layer, host_shift, host_shift_regs;
static inline void op1_fx_shift_ext(const void *fx, int k, int n, int d) { (void)fx; (void)n; host_shift_regs++; for (int p = 0; p < 3; p++) if (!host_sw[p][k]) host_sw[p][k] = (int16_t)d; }
static inline const int16_t *op1_fx_shift_words(int pos) { return host_sw[pos % 3]; }
static int host_push_regs;
static inline void op1_fx_push_bit(const void *fx, int k, int bit) { (void)fx; (void)k; (void)bit; host_push_regs++; }
static inline int op1_fx_shift_layer(int pos) { (void)pos; return host_layer; }
static inline int op1_shift_held(void) { return host_shift; }

typedef struct { int32_t len, next, step, tape_pos; uint8_t follows_tape; } op1_beat_t;
static inline op1_beat_t *host_beat(void) { static op1_beat_t b; return &b; }
static inline void op1_beat_clock(op1_beat_t *b) { *b = *host_beat(); }

typedef struct {
    void (*init)(int pos);
    void (*render)(int pos, int16_t *buf, const int16_t *params);
    uint8_t press[4];
    void (*render_stereo)(int pos, int16_t *l, int16_t *r, const int16_t *params);
} op1_fx_t;

/* the core's FX table (op1r exports op1_fxs / op1_fx_count): the host test puts a loaded jbsongs here */
const op1_fx_t *op1_fxs[64];
int op1_fx_count;
#define JB_HOST_FXS 1
typedef struct { int32_t x, y; } op1_pt;
typedef struct { void (*init)(void *scr); void (*update)(void *scr, int dt); } op1_screen_t;
#define OP1_Q10(v) ((int32_t)((v) * 1024))

/* screen stubs: count what the screen draws */
static inline void *op1_raster(void) { return (void *)1; }
static inline void op1_screen_draw(void *scr, int dt) { (void)scr; (void)dt; }
static inline void *op1_find_node(void *scr, const char *id) { (void)scr; (void)id; return 0; }
static inline int op1_fx_screen_pos(void) { return OP1_FX_MASTER; }
static int16_t host_knobs[4];
static inline void op1_fx_knobs_live(int16_t out[4]) { for (int i = 0; i < 4; i++) out[i] = host_knobs[i]; }
#endif
