/* jbsongs: the Jazz Band songbook, sent over USB by the Jazz Band companion as a tiny op1r FX plugin.
 * Shared layout (jbsongs.c defines it, jazzband.c finds it through op1_fxs[]). The companion fills len, sum, count and
 * text in a compiled template and fixes the file's CRC; nothing else changes.
 * text: one song per line, "TITLE;key;minor;ballad;chart\n" (the same text as the slot 5 songbook).
 * sum: FNV-1a 32 over text[0..len). */
#ifndef JBSONGS_H
#define JBSONGS_H
#include <stdint.h>
#define JBS_MAGIC0 0x534A424Au     /* "JBJS" */
#define JBS_MAGIC1 0x31474E4Fu     /* "ONG1" */
#define JBS_MAX    65536
/* a signature inside op1_fx_t itself, so a finder reads past an FX's op1_fx_t only when it is jbsongs: the four knob
 * press bytes are stepped knobs (OP1_FX_STEPS, harmless: jbsongs ignores its knobs) spelling "JBSG" + 0x80 */
#define JBS_PRESS  { 0xCA, 0xC2, 0xD3, 0xC7 }
typedef struct {
    op1_fx_t fx;                   /* first: op1_fxs[] points here */
    uint32_t magic[2];
    uint32_t len, sum, count;
    char     text[JBS_MAX];
} jbsongs_t;
#endif
