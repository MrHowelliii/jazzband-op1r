/* WebAssembly stand-in for op1r's op1_plugin.h: the parts the jazzband plugin uses, freestanding (no libc, no libm),
 * so jazzband.c compiles unchanged for the companion's browser play-along (web/jb_wasm.c). Same names and signatures
 * as the SDK and mock/op1_plugin.h. Knobs, presses, the beat clock and the FX table are set by jb_wasm.c. */
#ifndef OP1_PLUGIN_H
#define OP1_PLUGIN_H
#include <stdint.h>

#define OP1_HOST 1
#define JB_HOST_FXS 1
#define OP1_FX_BLOCK 64
#define OP1_SAMPLE_RATE 44100
#define OP1_FX_MASTER 2
enum { OP1_PRESS_NONE = 0, OP1_PRESS_PAGE = 1, OP1_PRESS_LATCH = 2 };

static int16_t host_sine[8192];                     /* filled by jbw_init() from JS (Math.sin) */
static inline int32_t op1_sin(int32_t k) { return host_sine[k & 8191]; }
static inline int16_t op1_sat16(int32_t x) { return (int16_t)(x > 32767 ? 32767 : (x < -32768 ? -32768 : x)); }
static inline int32_t op1_mul16(int32_t a, int32_t b) { return (a * b) >> 15; }
static inline int32_t op1_fx_param_q15(const int16_t *p, int i) { int32_t v = p[i]; return v < 0 ? 0 : (v > 32767 ? 32767 : v); }

volatile uint8_t op1_fx_page[3][4];
volatile uint8_t op1_fx_wrap[3][4];
#define OP1_PRESS_LIST(n)   (0x40 | (n))
#define OP1_FX_STEPS(n)  (0x80 + (n))
static inline int op1_fx_list(int32_t q15, int n) { int i = (int)((q15 * (n - 1) + 16384) >> 15); return i < 0 ? 0 : i >= n ? n - 1 : i; }

/* sampler slots: none in the browser (the songbook comes as jbsongs, sounds as packs) */
#define OP1_SAMPLE_GUARD   8
#define OP1_SAMPLE_MAX     264600
#define OP1_PEAK_BUCKETS   0x813
static inline const uint8_t *op1_sample_buffer(int slot) { (void)slot; return 0; }
static inline const int16_t *op1_sample_data(const uint8_t *sb) { (void)sb; return 0; }
static inline const int16_t *op1_sample_peaks(const uint8_t *sb) { (void)sb; static const int16_t z[OP1_PEAK_BUCKETS]; return z; }

/* FX Shift words: 0..3 the mixer levels (set by jb_wasm.c) */
static int16_t host_sw[8];
static inline void op1_fx_shift_ext(const void *fx, int k, int n, int d) { (void)fx; (void)k; (void)n; (void)d; }
static inline const int16_t *op1_fx_shift_words(int pos) { (void)pos; return host_sw; }
static inline void op1_fx_push_bit(const void *fx, int k, int bit) { (void)fx; (void)k; (void)bit; }
static inline int op1_fx_shift_layer(int pos) { (void)pos; return 0; }
static inline int op1_shift_held(void) { return 0; }

/* the OP-1 beat clock: standing still (step 0), so the band always plays the song's own tempo */
typedef struct { int32_t len, next, step, tape_pos; uint8_t follows_tape; } op1_beat_t;
static inline void op1_beat_clock(op1_beat_t *b) { b->len = 0; b->next = 0; b->step = 0; b->tape_pos = 0; b->follows_tape = 0; }

typedef struct {
    void (*init)(int pos);
    void (*render)(int pos, int16_t *buf, const int16_t *params);
    uint8_t press[4];
    void (*render_stereo)(int pos, int16_t *l, int16_t *r, const int16_t *params);
} op1_fx_t;

const op1_fx_t *op1_fxs[64];
int op1_fx_count;
typedef struct { int32_t x, y; } op1_pt;
typedef struct { void (*init)(void *scr); void (*update)(void *scr, int dt); } op1_screen_t;
#define OP1_Q10(v) ((int32_t)((v) * 1024))

/* the OP-1 screen is not drawn in the browser (the companion draws its own) */
static inline void *op1_raster(void) { return (void *)1; }
static inline void op1_screen_draw(void *scr, int dt) { (void)scr; (void)dt; }
static inline void *op1_find_node(void *scr, const char *id) { (void)scr; (void)id; return 0; }
static inline int op1_fx_screen_pos(void) { return OP1_FX_MASTER; }
static inline void op1_fx_knobs_live(int16_t out[4]) { for (int i = 0; i < 4; i++) out[i] = 0; }
#endif
