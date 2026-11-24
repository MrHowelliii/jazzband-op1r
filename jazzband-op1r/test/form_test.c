/* 1.4: the three screens (PLAY, SONGS, SETUP) and the switcher, explicit bar actions, queued key changes, red tempo
 * and transport, SETUP categories with red values, per-song session settings, and the musical form underneath them:
 * section / bar loops with a key cycle, chorus counts, turnarounds, intros, endings (final chord, tag), ° / ø. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int host_lines_n;
#include "../plugins/jazzband/jazzband.c"

static int16_t params[8] = {14894, 24000, 0, 0};       /* blue at position 5 */
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
static int wstep = 0, rstep = 0;
static void turn(int d) { wstep = ((wstep + d) % 127 + 127) % 127; params[2] = (int16_t)((wstep * 32768 + 16384) / 127); run(1); }
static void rturn(int d) { rstep = ((rstep + d) % 127 + 127) % 127; params[3] = (int16_t)((rstep * 32768 + 16384) / 127); run(1); }
static void push(void) { params[7] ^= 0x80; run(2); }
static void red(void) { params[7] = (int16_t)(params[7] ^ (int16_t)0x8000); run(1); }
static void bpush(void) { op1_fx_page[2][0] ^= 1; run(2); }
static void green(void) { op1_fx_page[2][1] ^= 1; run(2); }
static void blue_at(int i) { params[0] = (int16_t)(i * 32767 / 11); run(1); }
static jb_t *e;
static void go(int scr) { bpush(); turn(scr - e->sw_row); push(); }
static void play(void) { red(); long w = 0; while ((e->state != ST_PLAY || e->s_state != ST_PLAY) && w < 6000) { run(1); w++; } }
static void next_bar(void) {                            /* to just after the next bar line, its downbeat generated */
    long w = 0; int t = e->tick;
    while (e->state == ST_PLAY && w < 6000) { run(1); w++; if (e->tick < t) break; t = e->tick; }
    while (e->state == ST_PLAY && e->tick == 0 && w < 6000) { run(1); w++; }
}
static void until_stop(long max) { long w = 0; while (e->state != ST_STOP && w < max) { run(1); w++; } }
static int root(int beat) { return cur_chord(e, beat).root; }
static void setup_open(int cat) {                       /* SETUP, the category list, open one */
    if (e->screen != SCR_SETUP) go(SCR_SETUP);
    if (e->scat >= 0) { while (e->srow != 0) turn(-1); push(); }   /* BACK */
    turn(cat - e->srow); push();
}
static void set_value(int row, int clicks) {            /* white to the row, red turns its value */
    for (int k = 0; k < 20 && setup_row(e->scat, e->srow) != row; k++) turn(1);
    if (clicks) rturn(clicks);
}
static void pick(int d) {                               /* SONGS: d rows on, white press */
    if (e->screen != SCR_SONGS) go(SCR_SONGS);
    turn(d); push();
}

static const char BOOK[] =
    "SECTIONS;0;0;0;*A C^7 | A-7 | D-7 | G7 |*B E-7 | A7 | D-7 | G7 |*A C^7 | A-7 | D-7 | G7 |;120;100\n"
    "BLUES;5;0;0;*A F7 | Bb7 | F7 | F7 | Bb7 | Bo7 | F7 | F7 | C7 | Bb7 | F7 | F7 |;140\n"
    "MINOR;9;1;1;A-6 | Bh7 E7b9 | A-6 | A-6 |;132\n";

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    host_sine_init(); jb_host_op1_tempo = 0;
    op1_beat_t *bc = host_beat(); bc->len = (int32_t)((2646000LL * 16384) / 90); bc->next = 0; bc->step = 0;
    FX_jazzband.init(2); SCREEN_jazzband.init(0); run(2);
    e = &S[2];
    B.bt = BOOK; B.got = (uint32_t)(sizeof BOOK - 1); B.src = BOOK_USB; B.usb_state = 2; B.ok = 1;
    book_index(); run(2);
    frames = fopen("out/form_frames.txt", "w");
    CHECK(B.n == 3 && e->song == 0 && e->nbars == 12 && e->nsec == 3, "songbook: 3 songs, SECTIONS has 3 sections (%d)", e->nsec);
    CHECK(e->screen == SCR_PLAY && e->bpm == 120 && e->orig_bpm == 100, "PLAY first; practice tempo 120, original 100");
    frame("PLAY stopped");

    /* ---- the switcher: blue opens it, white chooses, white opens; blue again cancels; nothing musical changes */
    bpush();
    CHECK(e->sw && e->sw_row == SCR_PLAY && e->screen == SCR_PLAY, "blue press: the switcher, on PLAY");
    frame("switcher");
    turn(2);
    CHECK(e->sw_row == SCR_SETUP && e->screen == SCR_PLAY, "white turn chooses SETUP (not opened yet)");
    bpush();
    CHECK(!e->sw && e->screen == SCR_PLAY, "blue again: cancelled, still PLAY");
    run(20000);
    CHECK(e->screen == SCR_PLAY && !e->sw, "no timeouts: 30 s later nothing has moved");
    bpush(); rturn(5); green();
    CHECK(e->sw && e->bpm == 120 && e->s_muted, "in the switcher: red turn does nothing, green mutes");
    green(); bpush();
    CHECK(!e->s_muted && !e->sw, "green unmutes; blue closes");

    /* ---- SETUP: categories, red turns values, white acts; session-only; no timeout */
    setup_open(SC_PRACTICE);
    CHECK(e->screen == SCR_SETUP && e->scat == SC_PRACTICE && e->srow == 0, "SETUP PRACTICE opens on BACK");
    set_value(R_LOOP, 2);
    CHECK(e->loop == 2 && loop_lo(e) == 4 && loop_hi(e) == 7, "LOOP: red 2 clicks = B section, bars 5-8");
    set_value(R_KSTEP, 1);
    CHECK(e->kstep == KS_UP, "KEY CYCLE: up a half step");
    frame("SETUP practice: loop B, key cycle");
    push();                                              /* white press on a value row does nothing */
    CHECK(e->kstep == KS_UP && e->scat == SC_PRACTICE, "white press on a value row: no change, no exit");
    run(20000);
    CHECK(e->screen == SCR_SETUP && e->scat == SC_PRACTICE, "SETUP stays open (no 12 s timeout)");
    while (e->srow != 0) turn(-1);
    push();
    CHECK(e->scat < 0 && e->srow == SC_PRACTICE, "BACK: the category list, on PRACTICE");
    frame("SETUP categories");
    go(SCR_PLAY);
    play();
    CHECK(e->bar == 4 && root(0) == 4, "count-in into the loop: bar 5 (E-7)");
    frame("PLAY loop B");
    next_bar(); next_bar(); next_bar(); next_bar();
    CHECK(e->bar == 4 && e->shift == 1 && root(0) == 5 && e->pass == 1, "pass 2 a half step up: F-7");
    for (int i = 0; i < 4 * 11; i++) next_bar();
    CHECK(e->bar == 4 && e->shift == 0 && e->pass == 12, "after 12 passes: back in the written key");

    /* ---- blue while playing: queued for the next bar, KEY CYCLE OFF, cancel by turning back */
    next_bar();
    blue_at(7);
    CHECK(e->key_pend && e->key_tgt == 2 && e->transpose == 0 && e->kstep == KS_OFF && e->kmsg > 0, "blue +2 while playing: queued; KEY CYCLE OFF");
    frame("PLAY key queued, key cycle off");
    blue_at(5);
    CHECK(!e->key_pend, "turned back to the key that plays: the change is cancelled");
    blue_at(8);
    CHECK(e->key_pend && e->key_tgt == 3, "a further turn updates the target (+3)");
    { int b0 = e->bar; next_bar();
      CHECK(e->bar != b0 && !e->key_pend && wrapk(e->key_off + e->shift) == 3, "at the bar line: +3 plays"); }

    /* ---- red: tempo at once, the form goes on */
    { int b0 = e->bar, c0 = e->chorus, p0 = e->pass; rturn(4);
      CHECK(e->bpm == 124 && e->state == ST_PLAY && e->bar == b0 && e->chorus == c0 && e->pass == p0 && TADJ[0] == 124,
            "red 4 clicks: 124 BPM, same bar, no restart"); }
    for (int i = 0; i < 60; i++) rturn(-5);
    CHECK(e->bpm == 40 && e->state == ST_PLAY, "300 clicks down: held at 40 BPM, still playing");
    for (int i = 0; i < 60; i++) rturn(5);
    CHECK(e->bpm == 320, "300 up: held at 320");
    while (e->bpm > 124) rturn(-1);

    /* ---- white: bar cursor selects only; actions explicit; jump at a bar line; cancel */
    { int b0 = e->bar; turn(2);
      CHECK(e->bcur == (b0 + 2) % e->nbars && e->jump < 0 && e->bar == b0, "white turn: the cursor moves, nothing else");
      run(5000);
      CHECK(e->jump < 0, "no automatic jump (5000 blocks later)"); }
    push();
    CHECK(e->bm && bar_actions(e, 0) == 2 && bar_actions(e, 1) == BA_JUMP && bar_actions(e, 2) == BA_CANCEL, "white press: JUMP NEXT BAR / CANCEL");
    frame("PLAY bar menu (playing)");
    turn(1); push();
    CHECK(!e->bm && e->bcur < 0 && e->jump < 0, "CANCEL: menu, cursor and jump gone");
    { turn(3); int target = e->bcur; push(); push();     /* JUMP NEXT BAR (row 0) */
      CHECK(e->jump == target && !e->bm, "JUMP NEXT BAR: queued (bar %d)", target + 1);
      frame("PLAY jump queued");
      next_bar();
      CHECK(e->bar == target && e->jump < 0 && e->bcur < 0, "the band jumps at the bar line"); }

    /* ---- precedence at one bar line: a song beats a jump; a queued key never reaches the new song */
    { turn(1); push(); push();                           /* a jump queued */
      int jumped = e->jump; blue_at(9);                  /* and a key */
      CHECK(jumped >= 0 && e->key_pend, "jump and key both queued");
      pick(1);                                           /* SONGS: BLUES queued (the jump goes) */
      CHECK(e->pend == 1 && e->jump < 0 && e->key_pend, "a queued song drops the queued jump");
      frame("SONGS song queued");
      next_bar();
      CHECK(e->song == 1 && !e->key_pend && wrapk(e->key_off + e->shift) == 0 && e->bar == 0, "at the bar line: BLUES in its own key, no stale key or jump");
      go(SCR_PLAY); }

    /* ---- SONGS: CANCEL QUEUE; stopped: browse without loading, press loads without starting */
    pick(1);
    CHECK(e->pend == 2 && row_song(e->view, e->row - 1) == 2, "MINOR queued; the cursor stays on it, CANCEL QUEUE above");
    { while (e->row != 0) turn(-1); push();
      CHECK(e->pend < 0 && e->song == 1, "CANCEL QUEUE: nothing switches"); }
    run(3000); CHECK(e->song == 1, "still BLUES");
    red(); red(); CHECK(e->state == ST_STOP, "red, red: finish requested then stopped");
    turn(1);
    CHECK(e->song == 1 && e->cursor == 2, "stopped: browsing does not load");
    push();
    CHECK(e->song == 2 && e->state == ST_STOP && e->ballad == 1, "white press loads MINOR (not started); feel from the songbook: ballad");
    frame("SONGS loaded, stopped");
    turn(-2); push();
    CHECK(e->song == 0, "SECTIONS loaded again");
    CHECK(e->loop == 2 && e->kstep == KS_OFF && wrapk(e->key_off) == 3 && e->bpm == 124, "SECTIONS keeps its own loop, key and tempo from earlier");
    turn(1); push();
    CHECK(e->song == 1 && !looping(e) && e->key_off == 0 && e->bpm == 140, "BLUES does not inherit them");
    go(SCR_PLAY);

    /* ---- red presses in every state: count-in, intro, playing, finishing */
    red(); { long w = 0; while (e->state != ST_COUNT && w < 4000) { run(1); w++; } }
    CHECK(e->state == ST_COUNT && red_next(e) == 2, "count-in: red would stop");
    red(); CHECK(e->state == ST_STOP, "red in the count-in: stop");
    setup_open(SC_ARRANGE); set_value(R_INTRO, 2); go(SCR_PLAY);
    CHECK(e->intro == IN_TURN, "ARRANGEMENT INTRO: I-VI-II-V");
    red(); { long w = 0; while (e->state != ST_PLAY && w < 4000) { run(1); w++; } } run(2);
    CHECK(e->kind == BK_INTRO && root(0) == 5 && root(2) == 2, "intro (no count-in): F7 D7");
    frame("PLAY intro");
    red(); CHECK(e->state == ST_STOP, "red in the intro: stop");
    setup_open(SC_ARRANGE); set_value(R_INTRO, 1); set_value(R_TURN, 1);
    setup_open(SC_PRACTICE); set_value(R_CHORUS, 2); go(SCR_PLAY);
    CHECK(e->intro == IN_COUNT && e->turn == TA_IIV && e->choruses == 2, "count-in, II-V turnaround, 2 choruses");
    play();
    CHECK(red_next(e) == 1, "playing: red would finish");
    while (e->bar != 11 && e->state == ST_PLAY) next_bar();
    CHECK(e->chorus == 1 && root(0) == 7 && cur_chord(e, 0).q == Q_MIN7 && root(2) == 0, "chorus 1, bar 12: the turnaround G-7 C7");
    next_bar();
    red();
    CHECK(e->ending == 1 && red_next(e) == 2, "red: finish this pass (then red would stop)");
    frame("PLAY finishing");
    { int fin = 0; long w = 0; while (e->state == ST_PLAY && w < 30000) { run(1); w++; if (e->kind == BK_FINAL) fin = 1; }
      CHECK(fin && e->state == ST_STOP && e->chorus == 2, "the chorus ends on the final chord, then it stops"); }
    setup_open(SC_ARRANGE); set_value(R_END, 1); setup_open(SC_PRACTICE); set_value(R_CHORUS, -1); go(SCR_PLAY);
    play();
    while (e->kind == BK_CHART && e->state == ST_PLAY) next_bar();
    CHECK(e->kind == BK_TAG && root(0) == 7 && cur_chord(e, 0).q == Q_MIN7, "1 chorus, TAG ending: G-7 C7 into F7");
    frame("PLAY tag");
    red();
    CHECK(e->state == ST_STOP, "red while finishing: stop now");

    /* ---- stopped: START HERE; the destination is kept for red */
    turn(4); push();
    CHECK(e->bm && bar_actions(e, 1) == BA_START, "stopped: START HERE / CANCEL");
    frame("PLAY bar menu (stopped)");
    { int target = e->bcur; push();
      long w = 0; while (e->state != ST_PLAY && w < 6000) { run(1); w++; }
      CHECK(e->bar == target && e->sbar == target, "START HERE: count-in, then bar %d", target + 1); }
    red(); red();

    /* ---- stopped key: at once; mute on every screen */
    { int a0 = wrapk(e->key_off + e->shift), b0 = e->bidx_prev, d = b0 > 0 ? -1 : 1;
      blue_at(b0 + d);
      CHECK(!e->key_pend && wrapk(e->key_off + e->shift) == wrapk(a0 + d), "stopped: the key changes at once (%+d)", d); }
    for (int s = 0; s < SCR_N; s++) { go(s); green(); int m = e->s_muted; green(); CHECK(m && !e->s_muted && e->screen == s, "green mutes and unmutes on screen %d", s); }

    /* ---- BAND (1.5): one row per instrument, players only (samples; a synth only stands in for a missing pack) */
    setup_open(SC_BAND);
    CHECK(setup_rows(SC_BAND) == 5 && setup_row(SC_BAND, 1) == R_DPLAY && setup_row(SC_BAND, 4) == R_GPLAY, "SETUP BAND: BACK + 4 player rows, no sound rows");
    { set_value(R_PPLAY, 1);
      CHECK(e->sty[I_PIANO] == 1, "PIANO +1: RED GARLAND");
      set_value(R_PPLAY, 9);
      CHECK(e->sty[I_PIANO] == 0, "PIANO: 10 players, then around to BILL EVANS");
      set_value(R_DPLAY, -2);
      CHECK(e->sty[I_DRUMS] == DR_ROACH, "DRUMS -2 from JIMMY COBB: MAX ROACH (12 drummers)");
      frame("SETUP band roach");
      set_value(R_GPLAY, -1);
      CHECK(e->sty[I_GUITAR] == GT_GILBERTO, "GUITAR -1: JOAO GILBERTO");
      frame("SETUP band");
      set_value(R_DPLAY, 2); set_value(R_GPLAY, 1); }
    setup_open(SC_ARRANGE);
    { int b0 = e->ballad, s0 = e->bossa;
      set_value(R_FEEL, 0); while (e->ballad || e->bossa) rturn(1);
      rturn(1); CHECK(e->ballad && !e->bossa, "FEEL: SWING -> BALLAD");
      rturn(1); CHECK(!e->ballad && e->bossa, "FEEL: BALLAD -> BOSSA NOVA"); frame("SETUP feel bossa");
      rturn(1); CHECK(!e->ballad && !e->bossa, "FEEL: BOSSA NOVA -> SWING");
      while (e->ballad != b0 || e->bossa != s0) rturn(1); }
    setup_open(SC_SONG);
    set_value(R_RTEMPO, 0); push();
    CHECK(e->bpm == 140 && TADJ[1] == 0, "SONG RESET TEMPO: 140");
    set_value(R_RKEY, 0); frame("SETUP song"); push();
    CHECK(e->key_off == 0, "SONG RESET KEY: the written key");
    go(SCR_PLAY);

    /* drawn symbols: diminished and half-diminished */
    CHECK(QUALS[Q_DIM7][0] == '@' && QUALS[Q_HALFDIM][0] == '&', "dim7 and half-dim use the drawn symbols");
    CHECK(host_missing_glyphs == 0, "every screen uses only font glyphs (%d missing)", host_missing_glyphs);
    fclose(frames);
    puts(fails ? "FAILED" : "all ok");
    return fails != 0;
}
