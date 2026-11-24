/* jbpack: a Jazz Band sound pack (piano, bass, guitar, drums or nylon guitar), a data-only op1r FX plugin. One compiled template;
 * the packer (packs/make_packs.py) renames it (jbpiano, jbbass, jbguitar, jbdrums), fills the header and zone table,
 * and appends the audio after the image, so it is loaded into RAM with the plugin. jazzband.c finds packs through
 * op1_fxs[] and plays them (kept identical there).
 * Audio per zone: IMA ADPCM, 4 bits a sample, low nibble first, starting from (pred, idx); or 8-bit G.711 mu-law.
 * sum: FNV-1a 32 over the zone table (nzones entries) and then the data. */
#ifndef JBPACK_H
#define JBPACK_H
#include <stdint.h>
#define JBP_MAGIC0 0x4B50424Au     /* "JBPK" */
#define JBP_MAGIC1 0x31444E53u     /* "SND1" */
#define JBP_PRESS  { 0xCA, 0xC2, 0xD0, 0xCB }   /* signature in op1_fx_t: stepped knobs spelling "JBPK" + 0x80 */
#define JBP_MAXZ   160
enum { JBP_DRUMS, JBP_BASS, JBP_PIANO, JBP_GUITAR, JBP_NYLON };   /* NYLON (1.5): Joao Gilberto's guitar */
typedef struct {
    uint8_t  key, vlo, vhi, rr;    /* key: MIDI root note (pitched) or drum id (D_KICK..); velocity range; round robin */
    uint32_t step;                 /* Q16 read step at the root note: sample rate / 44100, tuning included */
    int16_t  pred;                 /* ADPCM start: predictor and step index */
    uint8_t  idx, codec;           /* codec 0: IMA ADPCM (4 bits), 1: G.711 mu-law (8 bits, for cymbals) */
    uint32_t off, len;             /* data byte offset, length in samples */
} jbzone_t;                        /* 20 bytes */
typedef struct {
    op1_fx_t fx;                   /* first: op1_fxs[] points here (16 bytes on the Blackfin) */
    uint32_t magic[2];
    uint32_t inst, nzones, data_off, data_len, sum, version;   /* data_off: from this struct to the audio */
    char     credit[96];           /* source and license, shown on the screen */
    jbzone_t zone[JBP_MAXZ];
} jbpack_t;
#endif
