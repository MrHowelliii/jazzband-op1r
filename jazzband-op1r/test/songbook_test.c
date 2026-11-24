/* Reads the companion's generated PCM directly through the real plugin decoder. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int host_lines_n;
#include "../plugins/jazzband/jazzband.c"

static host_sbuf_t slot;
static int16_t data[OP1_SAMPLE_GUARD + OP1_SAMPLE_MAX + 16];
static int16_t params[8] = {14894, 24000, 0, 0};
static FILE *frames;
static void frame(const char *label) {
    host_lines_n = 0; SCREEN_jazzband.update(0, 16);
    fprintf(frames, "F %s\n", label);
    for (int i = 0; i < host_lines_n && i < 4096; i++) { host_line_t *l = &host_line_log[i];
        fprintf(frames, "%d %d %d %d %d %08x\n", l->x0, l->y0, l->x1, l->y1, l->w, l->rgba); }
}
static int16_t knob_for(int song) { return (int16_t)((song * 32768 + 16384) / 127); }   /* stepped white knob, one song per click */

static int load(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); return 0; }
    memset(data, 0, sizeof data); memset(&slot, 0, sizeof slot); slot.data = data;
    size_t n = fread(data + OP1_SAMPLE_GUARD, 2, OP1_SAMPLE_MAX, f); fclose(f);
    for (int b = 0; b < OP1_PEAK_BUCKETS; b++) {
        int peak = 0;
        for (size_t i = (size_t)b * 256; i < (size_t)(b + 1) * 256 && i < n; i++) {
            int a = abs(data[OP1_SAMPLE_GUARD + i]); if (a > peak) peak = a;
        }
        slot.peaks[b] = (int16_t)peak;
    }
    host_sb[4] = &slot; return 1;
}
static void scan(void) {
    for (int i = 0; i < 400; i++) { int16_t l[64] = {0}, r[64] = {0}; FX_jazzband.render_stereo(2,l,r,params); }
}
int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) return 2;
    host_sine_init(); FX_jazzband.init(2); SCREEN_jazzband.init(0); scan();
    if (nsongs() != JB_NSONGS) return 1;
    if (!load(argv[1])) return 1;
    scan();
    const char *title, *chart; int tl,key,minor,cl;
    song_info(0,&title,&tl,&key,&minor,&chart,&cl);
    if (!B.ok || B.n != 1 || S[2].nbars != 4 || tl != 8 || strncmp(title,"PRACTICE",8) ||
        S[2].bars[0].beat[0].root != 2 || S[2].bars[1].beat[0].q != Q_DOM7) return 1;
    puts("ok: companion songbook detected in slot 5; title, harmony, and 4 bars match without plugin reinitialization");
    int generation = B.gen;
    if (!load(argv[2])) return 1;
    scan();
    if (!B.ok || B.gen <= generation || S[2].bars[0].beat[0].root != 4) {
        fputs("FAIL: same-size songbook edit was not detected\n", stderr); return 1;
    }
    puts("ok: same-size chart edit at the same slot address detected without restarting the plugin");
    host_sb[4] = 0; scan();
    if (B.ok || nsongs() != JB_NSONGS) return 1;
    puts("ok: removing songbook restores built-in songs");
    if (argc == 4) {                                   /* the song browser with a 12-song book */
        jb_t *e = &S[2];
        op1_beat_t *bc = host_beat(); bc->len = (int32_t)((2646000LL * 16384) / 140); bc->next = 0; bc->step = 0;
        if (!load(argv[3])) return 1;
        scan();
        if (!B.ok || B.n != 12) { fputs("FAIL: 12-song book not read\n", stderr); return 1; }
        frames = fopen("out/browser_frames.txt", "w");
        params[2] = knob_for(0); scan();
        params[7] = (int16_t)(params[7] ^ (int16_t)0x8000); scan();   /* red push: play song 1 */
        op1_fx_page[2][0] ^= 1; scan();                /* blue press: the switcher */
        params[2] = knob_for(1); scan(); params[7] ^= 0x80; scan();   /* white to SONGS, white push: open */
        if (e->screen != SCR_SONGS) { fputs("FAIL: switcher did not open SONGS\n", stderr); return 1; }
        int base = e->row;
        for (int k = 1; k <= 9; k++) { params[2] = knob_for(1 + k); scan(); }
        if (!(e->cursor == (base + 9) % 12 && e->song == 0 && e->state == ST_PLAY)) { fprintf(stderr, "FAIL: browsing while playing changed the song (%d)\n", e->cursor); return 1; }
        frame("browser - song 10 of 12, band playing song 1");
        params[2] = knob_for(1 + 11); scan(); frame("browser - last song");
        params[2] = knob_for(1 + 12); scan(); frame("browser - wrapped to song 1 (now playing)");
        params[2] = knob_for(1 + 9); scan();
        params[7] ^= 0x80; scan();                     /* white push: switch at the next bar */
        long w = 0;
        while (e->song != 9 && w < 3000) { int16_t l[64] = {0}, r[64] = {0}; FX_jazzband.render_stereo(2, l, r, params); w++; }
        if (e->song != 9 || e->state != ST_PLAY) { fputs("FAIL: white did not switch to the browsed song\n", stderr); return 1; }
        scan(); frame("playing song 10 after the switch");
        fclose(frames);
        puts("ok: browser scrolls a 12-song book while the band plays, wraps, and a white push switches at the bar line");
    }
    return 0;
}
