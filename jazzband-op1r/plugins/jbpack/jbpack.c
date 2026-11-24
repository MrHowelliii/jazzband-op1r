/* jbpack: holds a Jazz Band sound pack (see jbpack.h). As an FX it does nothing: audio passes through untouched.
 * No .data / .bss (the packer appends audio after the image). Copyright (C) 2026 Brandon. GPL-3.0-or-later with the
 * OP-1 firmware permission, like op1r. Sample sources and licenses: packs/SOURCES.md. */
#include "op1_plugin.h"
#include "jbpack.h"

static void jp_init(int pos) { (void)pos; }
static void jp_render(int pos, int16_t *buf, const int16_t *p) { (void)pos; (void)buf; (void)p; }
static void jp_render_stereo(int pos, int16_t *l, int16_t *r, const int16_t *p) { (void)pos; (void)l; (void)r; (void)p; }

const jbpack_t FX_jbpack = { { jp_init, jp_render, JBP_PRESS, jp_render_stereo }, { JBP_MAGIC0, JBP_MAGIC1 },
                             0, 0, 0, 0, 0, 1, "EMPTY TEMPLATE", { { 0 } } };

#include "seq/seq_text.h"
static const uint8_t C_WHITE[4] = {0xFF, 0xFF, 0xFF, 0xFF}, C_GREY[4] = {0x6F, 0x71, 0x73, 0xFF},
                     C_GREEN[4] = {0x00, 0xED, 0x95, 0xFF};
static void jp_screen_init(void *scr) { (void)scr; }
static void jp_screen_update(void *scr, int dt) {
    op1_screen_draw(scr, dt);
    void *r = op1_raster();
    const volatile jbpack_t *b = &FX_jbpack;                     /* filled in by the packer, not by C */
    static const char *const NAME[5] = {"DRUMS", "BASS", "PIANO", "GUITAR", "NYLON"};
    char buf[40], *p;
    p = buf; p = sq_cat(p, "JAZZ BAND "); p = sq_cat(p, NAME[b->inst < 5 ? b->inst : 0]); p = sq_cat(p, " SOUNDS");
    sq_text(r, OP1_Q10(16), OP1_Q10(20), 1434, buf, OP1_Q10(1), C_WHITE);
    p = buf; p = sq_num(p, b->nzones, 1); p = sq_cat(p, " SAMPLES  "); p = sq_num(p, (b->data_len + 512) >> 10, 1); p = sq_cat(p, " KB");
    sq_text(r, OP1_Q10(16), OP1_Q10(48), OP1_Q10(1), buf, OP1_Q10(1), C_GREEN);
    char cr[97]; int n = 0; for (; n < 96 && b->credit[n]; n++) cr[n] = b->credit[n]; cr[n] = 0;
    sq_text(r, OP1_Q10(16), OP1_Q10(72), 1024, cr, 1024, C_GREY);
    sq_text(r, OP1_Q10(16), OP1_Q10(112), 1024, "THIS FX DOES NOTHING TO THE SOUND", 1024, C_GREY);
    sq_text(r, OP1_Q10(16), OP1_Q10(128), 1024, "PLAY WITH JAZZBAND AS THE MASTER FX", 1024, C_GREY);
}
const op1_screen_t SCREEN_jbpack = { jp_screen_init, jp_screen_update };
