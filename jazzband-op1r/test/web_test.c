/* The browser build (web/jb_wasm.c) on the PC: the same scripted session as tests/web.test.cjs runs on the WebAssembly
 * build, so the two outputs must hash the same. Prints the hash and the state at each checkpoint. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../web/jb_wasm.c"

static uint32_t hash = 2166136261u;
static void run(int blocks) {
    for (int b = 0; b < blocks; b++) {
        int n = jbw_render(1);
        for (int i = 0; i < n; i++) {
            int l = (int)lrintf(OUTL[i] * 32768), r = (int)lrintf(OUTR[i] * 32768);
            hash = (hash ^ (uint32_t)(l & 0xFFFF)) * 16777619u; hash = (hash ^ (uint32_t)(r & 0xFFFF)) * 16777619u;
        }
    }
}
static void cp(const char *label) {
    int32_t *s = jbw_state();
    printf("%s state=%d bar=%d song=%d bpm=%d key=%d chorus=%d pass=%d kind=%d hash=%08x\n", label, s[0], s[1], s[14], s[12], s[53], s[4], s[5], s[7], hash);
}
int main(int argc, char **argv) {
    (void)argc;
    FILE *f = fopen(argv[1], "rb"); static char text[65536]; size_t len = fread(text, 1, sizeof text, f); fclose(f);
    for (int i = 0; i < 8192; i++) { double v = 32767 * sin(2 * M_PI * i / 8192); host_sine[i] = (int16_t)(v < 0 ? -floor(-v + 0.5) : floor(v + 0.5)); }
    jbw_init();
    memcpy(jbw_book_text(), text, len);
    printf("songs %d\n", jbw_book_set((uint32_t)len, 3));
#include "web_script.h"
    return 0;
}
