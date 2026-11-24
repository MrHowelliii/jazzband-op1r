/* jbsongs: holds the Jazz Band songbook (see jbsongs.h). As an FX it does nothing: audio passes through untouched.
 * Copyright (C) 2026 Brandon. GPL-3.0-or-later with the OP-1 firmware permission, like op1r. */
#include "op1_plugin.h"
#include "jbsongs.h"

static void js_init(int pos) { (void)pos; }
static void js_render(int pos, int16_t *buf, const int16_t *p) { (void)pos; (void)buf; (void)p; }
static void js_render_stereo(int pos, int16_t *l, int16_t *r, const int16_t *p) { (void)pos; (void)l; (void)r; (void)p; }

/* initialised data (the magic), so the whole songbook area is in the image for the companion to fill */
const jbsongs_t FX_jbsongs = { { js_init, js_render, JBS_PRESS,
                                 js_render_stereo }, { JBS_MAGIC0, JBS_MAGIC1 }, 0, 0, 0, { 0 } };

/* ---------------------------------------------------------------- screen */
#include "seq/seq_text.h"
static const uint8_t C_WHITE[4] = {0xFF, 0xFF, 0xFF, 0xFF}, C_GREY[4] = {0x6F, 0x71, 0x73, 0xFF},
                     C_GREEN[4] = {0x00, 0xED, 0x95, 0xFF};
static void js_screen_init(void *scr) { (void)scr; }
static void js_screen_update(void *scr, int dt) {
    op1_screen_draw(scr, dt);
    void *r = op1_raster();
    const volatile jbsongs_t *b = &FX_jbsongs;                    /* volatile: filled in by the companion, not by C */
    char buf[24], *p;
    sq_text(r, OP1_Q10(16), OP1_Q10(24), 1638, "JAZZ BAND SONGBOOK", OP1_Q10(1), C_WHITE);
    if (b->count) { p = buf; p = sq_num(p, b->count, 1); p = sq_cat(p, b->count == 1 ? " SONG" : " SONGS"); }
    else { p = buf; p = sq_cat(p, "EMPTY"); }
    sq_text(r, OP1_Q10(16), OP1_Q10(56), OP1_Q10(2), buf, OP1_Q10(2), b->count ? C_GREEN : C_GREY);
    sq_text(r, OP1_Q10(16), OP1_Q10(96), 1024, "SENT BY USB FROM THE JAZZ BAND COMPANION", 1024, C_GREY);
    sq_text(r, OP1_Q10(16), OP1_Q10(112), 1024, "THIS FX DOES NOTHING TO THE SOUND", 1024, C_GREY);
    sq_text(r, OP1_Q10(16), OP1_Q10(128), 1024, "PLAY WITH JAZZBAND AS THE MASTER FX", 1024, C_GREY);
}
const op1_screen_t SCREEN_jbsongs = { js_screen_init, js_screen_update };
