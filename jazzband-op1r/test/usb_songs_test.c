/* End to end: a jbsongs.op1p built by the companion (tests/jbsongs-test.op1p) is parsed, CRC-checked and relocated by
 * op1r's own loader code (mock/op1r-loader: plugload/op1p_load.c, unchanged), then jazzband finds it through op1_fxs[]
 * and plays its songs. Also: USB songs win over a slot 5 songbook; a damaged checksum falls back to slot 5.
 * The device struct is 32-bit (op1_fx_t = 16 bytes on the Blackfin); the host view copies its fields at those offsets. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int host_lines_n;
#include "../plugins/jazzband/jazzband.c"
#include "op1r-loader/op1p_load.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } else { printf("ok:   " __VA_ARGS__); printf("\n"); } } while (0)
static int16_t params[8] = {14894, 24000, 0, 0};
static host_sbuf_t slot; static int16_t sdata[OP1_SAMPLE_GUARD + OP1_SAMPLE_MAX + 16];
static jbsongs_t usb;                                   /* host view of the loaded plugin's songbook */

static void scan(int n) { for (int i = 0; i < n; i++) { int16_t l[64] = {0}, r[64] = {0}; FX_jazzband.render_stereo(2, l, r, params); } }
static void reset_book(void) { memset(&B, 0, sizeof B); FX_jazzband.init(2); }
static int load_slot(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); return 0; }
    memset(sdata, 0, sizeof sdata); memset(&slot, 0, sizeof slot); slot.data = sdata;
    size_t n = fread(sdata + OP1_SAMPLE_GUARD, 2, OP1_SAMPLE_MAX, f); fclose(f);
    for (int b = 0; b < OP1_PEAK_BUCKETS; b++) { int pk = 0;
        for (size_t i = (size_t)b * 256; i < (size_t)(b + 1) * 256 && i < n; i++) { int a = abs(sdata[OP1_SAMPLE_GUARD + i]); if (a > pk) pk = a; }
        slot.peaks[b] = (int16_t)pk; }
    host_sb[4] = &slot; return 1;
}
static uint32_t rd(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: usb_songs_test jbsongs.op1p slot-songbook.raw\n"); return 2; }
    host_sine_init();
    static uint8_t file[1 << 20] __attribute__((aligned(4)));
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 2; }
    uint32_t len = (uint32_t)fread(file, 1, sizeof file, f); fclose(f);

    op1p_view v; int r = op1p_parse(file, len, &v);
    CHECK(r == 0 && v.hdr->kind == OP1P_FX && !strcmp(v.hdr->name, "jbsongs"), "op1r loader accepts the companion's jbsongs.op1p (%d, %u B, %u imports)", r, len, v.hdr->n_imports);
    static uint8_t mem[1 << 20]; uint32_t imp[OP1P_MAX_IMPORTS];
    for (uint32_t i = 0; i < v.hdr->n_imports; i++) imp[i] = 0x01000000u + 16 * i;
    r = op1p_relocate(&v, mem, 0x00400000u, imp);
    CHECK(r == 0, "op1r loader relocates it (%d)", r);
    const uint8_t *e = mem + v.hdr->entry_off;               /* device layout: op1_fx_t (16 B), magic[2], len, sum, count, text */
    CHECK(rd(e + 16) == JBS_MAGIC0 && rd(e + 20) == JBS_MAGIC1, "entry magic after the 16-byte op1_fx_t");
    memcpy(usb.fx.press, e + 8, 4);                         /* after init and render (2 x 4 B pointers) */
    CHECK(!memcmp(usb.fx.press, JBS_PRESS, 4), "signature in the knob press bytes");
    usb.magic[0] = rd(e + 16); usb.magic[1] = rd(e + 20); usb.len = rd(e + 24); usb.sum = rd(e + 28); usb.count = rd(e + 32);
    memcpy(usb.text, e + 36, BOOK_MAX);
    CHECK(usb.count == 12 && usb.len > 0 && usb.len < 4096, "12 songs, %u bytes of charts", usb.len);

    /* jbsongs among other FX; a slot 5 songbook is loaded too */
    op1_fxs[1] = &FX_jazzband; op1_fxs[3] = &usb.fx; op1_fx_count = 4;
    if (!load_slot(argv[2])) return 2;
    reset_book(); scan(800);
    const char *t, *c; int tl, key, minor, cl;
    song_info(0, &t, &tl, &key, &minor, &c, &cl);
    CHECK(B.src == BOOK_USB && B.ok && nsongs() == 12, "jazzband plays the USB songbook (12 songs) ahead of slot 5 (src %d, %d songs)", B.src, nsongs());
    CHECK(tl == 22 && !strncmp(t, "ALL THE THINGS YOU ARE", 22), "song 1 title: %.*s", tl, t);
    CHECK(S[2].nbars == 4 && S[2].bars[0].beat[0].root == 0 && S[2].bars[0].beat[0].q == Q_MIN7, "song 1 chart loaded (%d bars, C-7 first)", S[2].nbars);
    song_info(11, &t, &tl, &key, &minor, &c, &cl);
    CHECK(tl > 20 && !strncmp(t, "THERE WILL NEVER BE ANOTHER YOU", tl), "song 12: %.*s, key %d", tl, t, key);

    load_song(&S[2], 2);                                      /* song 3 is a T34 chart from the companion */
    song_info(2, &t, &tl, &key, &minor, &c, &cl);
    CHECK(S[2].nb == 3 && S[2].tpbar == 36 && S[2].bars[2].beat[0].root == 7 && S[2].bars[2].beat[0].q == Q_MIN7 &&
          S[2].bars[2].beat[2].root == 0 && S[2].bars[2].beat[2].q == Q_DOM7, "song 3 (%.*s) arrives as 3/4: G-7 for 2 beats, C7 for 1", tl, t);
    load_song(&S[2], 0);
    CHECK(S[2].nb == 4, "song 1 stays 4/4");
    CHECK(SETS.n == 1 && SETS.count[0] == 2 && SETS.song[0][0] == 3 && SETS.song[0][1] == 0 && SETS.nlen[0] == 10 && !strncmp(B.bt + SETS.name[0], "FRIDAY GIG", 10),
          "the companion's set list arrives: FRIDAY GIG = songs 4, 1 (empty set dropped; %d sets)", SETS.n);
    jb_host_op1_tempo = 0;
    CHECK(song_bpm(0) == 140 && song_bpm(11) == 140, "song tempo read from the line's last field (%d)", song_bpm(0));
    jb_host_op1_tempo = 1;

    /* a damaged checksum: jbsongs is ignored, the slot 5 songbook plays */
    usb.sum ^= 1; reset_book(); scan(800);
    CHECK(B.src == BOOK_SLOT && B.ok && nsongs() == 1, "bad checksum: falls back to the slot 5 songbook (src %d, %d songs)", B.src, nsongs());
    usb.sum ^= 1;

    /* no jbsongs, no slot: built-in songs */
    op1_fx_count = 2; host_sb[4] = 0; reset_book(); scan(800);
    CHECK(!B.ok && nsongs() == JB_NSONGS, "neither: the %d built-in songs", nsongs());
    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
