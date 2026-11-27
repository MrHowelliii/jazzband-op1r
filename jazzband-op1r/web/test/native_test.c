/* native build check (the iOS app builds the engine the same way): load the packs from the .jbp files, a 4-bar ii-V-I and
 * a melody, render 10 s. cc -O2 -Iweb -Imock -Iplugins/jazzband -o /tmp/nt web/test/native_test.c web/jb_web.c -lm && /tmp/nt web/packs */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
void jbw_init(void); uint8_t *jbw_alloc(uint32_t); int jbw_add_pack(const uint8_t *); int jbw_add_pack_file(const uint8_t *, uint32_t); int jbw_pack_ok(int);
uint8_t *jbw_bars(void); void jbw_load(int, int, int, int); void *jbw_lead_buf(void); void jbw_lead_set(int, int);
void jbw_lead_sound(int, int, int); void jbw_play(int); void jbw_render(void); float *jbw_out_l(void); float *jbw_out_r(void);
int jbw_state(void); int jbw_bar(void);
static uint8_t *slurp(const char *p, long *n) { FILE *f = fopen(p, "rb"); if (!f) return 0; fseek(f, 0, 2); *n = ftell(f); rewind(f); uint8_t *b = malloc(*n); fread(b, 1, *n, f); fclose(f); return b; }
int main(int argc, char **argv) {
    jbw_init();
    const char *packs[] = {"jbdrums", "jbbass", "jbpiano", "jbguitar", "jblead", "jbgtrlead"};
    for (int i = 0; i < 6; i++) { char p[512]; snprintf(p, sizeof p, "%s/%s.jbp", argv[1], packs[i]); long n; uint8_t *b = slurp(p, &n); if (!b) { printf("missing %s\n", p); continue; }
        if (!jbw_add_pack_file(b, (uint32_t)n)) printf("bad %s\n", p); free(b); }
    for (int i = 0; i < 3000; i++) jbw_render();
    int ok = 0; const int slot[6] = {0, 1, 2, 3, 5, 6}; for (int i = 0; i < 6; i++) ok += jbw_pack_ok(slot[i]);
    uint8_t *bars = jbw_bars(); const int ch[4][2] = {{2, 1}, {7, 2}, {0, 0}, {0, 0}};
    for (int b = 0; b < 4; b++) for (int k = 0; k < 4; k++) { bars[9 * b + 2 * k] = ch[b][0]; bars[9 * b + 2 * k + 1] = ch[b][1]; bars[9 * b + 8] = b ? 0 : 65; }
    jbw_load(4, 4, 120, 0);
    uint8_t *ln = jbw_lead_buf(); for (int b = 0; b < 4; b++) { uint8_t *o = ln + 8 * b; o[0] = b; o[1] = 0; o[2] = 0; o[3] = 0; o[4] = 180; o[5] = 0; o[6] = 69; o[7] = 100; }
    jbw_lead_set(4, 0); jbw_lead_sound(2, 26000, 0); jbw_play(-1);
    double e = 0; float pk = 0; int n = 44100 * 10 / 128, firstbar = -1;
    for (int i = 0; i < n; i++) { jbw_render(); float *l = jbw_out_l(); for (int k = 0; k < 128; k++) { e += l[k] * l[k]; if (fabsf(l[k]) > pk) pk = fabsf(l[k]); }
        if (firstbar < 0 && (jbw_state() & 15) == 3) firstbar = i * 128; }
    printf("packs ok %d/6, peak %.3f, rms %.3f, bar 1 at %.2f s\n", ok, pk, sqrt(e / (n * 128.0)), firstbar / 44100.0);
    return !(ok == 6 && pk > 0.05 && pk <= 1 && firstbar > 80000 && firstbar < 96000);
}
