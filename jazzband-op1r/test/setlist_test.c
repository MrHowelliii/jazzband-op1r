/* Set lists: "@NAME|3 0 7" lines in the songbook, the browser's set menu, white push and turns, set positions.
 * Organisation only: nothing plays on by itself. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int host_lines_n;
#include "../plugins/jazzband/jazzband.c"

static int16_t params[8] = {14894, 24000, 0, 0};
static FILE *frames;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } else { printf("ok:   " __VA_ARGS__); printf("\n"); } } while (0)
static void frame(const char *label) {
    host_lines_n = 0; SCREEN_jazzband.update(0, 16);
    fprintf(frames, "F %s\n", label);
    for (int i = 0; i < host_lines_n && i < 4096; i++) { host_line_t *l = &host_line_log[i];
        fprintf(frames, "%d %d %d %d %d %08x\n", l->x0, l->y0, l->x1, l->y1, l->w, l->rgba); }
}
static void run(int n) { for (int i = 0; i < n; i++) { int16_t l[64] = {0}, r[64] = {0}; FX_jazzband.render_stereo(2, l, r, params); } }
static int step = 0;
static void turn(int d) { step = ((step + d) % 127 + 127) % 127; params[2] = (int16_t)((step * 32768 + 16384) / 127); run(1); }
static void push(void) { params[7] ^= 0x80; run(2); }
static void red(void) { params[7] = (int16_t)(params[7] ^ (int16_t)0x8000); run(1); }
static void bpush(void) { op1_fx_page[2][0] ^= 1; run(2); }
static jb_t *ee;
static void go(int scr) { bpush(); turn(scr - ee->sw_row); push(); }

static const char BOOK[] =
    "ALL THE THINGS YOU ARE;8;0;0;F-7 | Bb-7 | Eb7 | Ab^7 |;160\n"
    "BLUE BOSSA;0;1;0;C-7 | F-7 | Dh7 | G7 |;150\n"
    "SO WHAT;2;1;0;D-7 | D-7 | D-7 | D-7 |;136\n"
    "MISTY;3;0;1;Eb^7 | Bb-7 Eb7 | Ab^7 | Ab-7 Db7 |;72\n"
    "OLEO;10;0;0;Bb6 G-7 | C-7 F7 | Bb6 G-7 | C-7 F7 |;240\n"
    "TUNE UP;2;0;0;E-7 | A7 | D^7 | D^7 |;180\n"
    "@FRIDAY GIG|4 1 3\n"
    "@PRACTICE|0 5\n"
    "@BROKEN|9 99\n"
    "@|1 2\n";

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    host_sine_init(); jb_host_op1_tempo = 0;
    op1_beat_t *bc = host_beat(); bc->len = (int32_t)((2646000LL * 16384) / 140); bc->next = 0; bc->step = 0;
    FX_jazzband.init(2); SCREEN_jazzband.init(0); run(2);
    jb_t *e = &S[2];
    B.bt = BOOK; B.got = (uint32_t)(sizeof BOOK - 1); B.src = BOOK_USB; B.usb_state = 2;
    book_index(); run(2);
    frames = fopen("out/setlist_frames.txt", "w");

    CHECK(B.n == 6 && SETS.n == 2, "6 songs, 2 set lists (empty or nameless sets dropped: %d)", SETS.n);
    CHECK(SETS.count[0] == 3 && SETS.song[0][0] == 4 && SETS.song[0][2] == 3 && SETS.nlen[0] == 10 && !strncmp(B.bt + SETS.name[0], "FRIDAY GIG", 10),
          "FRIDAY GIG: OLEO, BLUE BOSSA, MISTY");
    CHECK(SETS.count[1] == 2 && SETS.song[1][1] == 5, "PRACTICE: ALL THE THINGS, TUNE UP");

    ee = e;
    /* SONGS with no set in use: the set list menu */
    go(SCR_SONGS);
    CHECK(e->screen == SCR_SONGS && e->view == VIEW_MENU && e->row == 0, "SONGS opens on the set lists (view %d row %d)", e->view, e->row);
    frame("set lists");
    push();
    CHECK(e->view == 0 && e->row == 1 && e->song == 0 && e->set < 0, "push opens FRIDAY GIG; nothing loads by browsing (song %d)", e->song + 1);
    frame("friday gig");
    push();
    CHECK(e->song == 4 && e->set == 0 && e->spos == 0 && e->state == ST_STOP, "push loads OLEO, 1 of 3, not started");
    frame("oleo loaded, 1 of 3");

    /* playing: SONGS opens the set on the next song; push queues it for the next bar; nothing advances by itself */
    go(SCR_PLAY);
    red(); long w = 0; while (e->state != ST_PLAY && w < 4000) { run(1); w++; }
    go(SCR_SONGS);
    CHECK(e->view == 0 && e->row == 2 && e->song == 4, "playing: SONGS opens the set on the next song (row %d), OLEO plays on", e->row);
    frame("next in set");
    push();
    CHECK(e->pend == 1 && e->row == 3 && e->state == ST_PLAY, "push queues BLUE BOSSA; CANCEL QUEUE appears above");
    frame("blue bossa queued");
    w = 0; while (e->song != 1 && w < 4000) { run(1); w++; }
    CHECK(e->song == 1 && e->set == 0 && e->spos == 1 && e->bpm == 150 && e->row == 2, "BLUE BOSSA plays, 2 of 3 in FRIDAY GIG, at its 150 bpm");
    frame("blue bossa, 2 of 3");
    int bars = 0, last = e->bar; w = 0;
    while (w < 30000) { run(1); w++; if (e->bar != last) { last = e->bar; bars++; } }
    CHECK(e->song == 1 && e->state == ST_PLAY && bars > 10 && e->screen == SCR_SONGS, "no auto-advance, no timeout: still BLUE BOSSA after %d bars", bars);

    /* back to the menu, then every song */
    turn(-2);
    CHECK(e->view == 0 && e->row == 0, "the first row of a set goes back");
    frame("back row");
    push();
    CHECK(e->view == VIEW_MENU && e->row == 0, "push on it: the set lists, cursor on the set");
    turn(2);
    CHECK(e->row == 2, "ALL SONGS is the last row of the menu");
    push();
    CHECK(e->view == VIEW_ALL && row_song(VIEW_ALL, e->row) == 1, "ALL SONGS opens on the playing song");
    frame("all songs");
    turn(3); push();
    w = 0; while (e->song != 4 && w < 4000) { run(1); w++; }
    CHECK(e->song == 4 && e->set == -1, "a song from ALL SONGS plays outside any set");

    /* a book without sets: the plain list, as before */
    static const char PLAIN[] = "A;0;0;0;C^7 |;120\nB;0;0;0;D-7 |;120\n";
    B.bt = PLAIN; B.got = (uint32_t)(sizeof PLAIN - 1); book_index(); run(2);
    go(SCR_PLAY); go(SCR_SONGS);
    CHECK(SETS.n == 0 && e->view == VIEW_ALL && row_song(VIEW_ALL, e->row) >= 0, "no set lists: SONGS is the plain song list");
    fclose(frames);
    puts(fails ? "FAILED" : "all ok");
    return fails != 0;
}
