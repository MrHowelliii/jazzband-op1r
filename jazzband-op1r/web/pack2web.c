/* pack2web: a Jazz Band sound pack (.op1p) -> the bytes the web build maps into memory (.jbp).
 *
 * The .op1p is parsed, CRC-checked and relocated by op1r's own loader (mock/op1r-loader), exactly as on the OP-1.
 * The output is the loaded plugin entry (jbpack_t: op1_fx_t, magic, header, zone table) followed by the audio, from
 * entry_off to the end of the audio. wasm32 and the Blackfin share the layout (32-bit pointers, little endian, a
 * 16-byte op1_fx_t), so jazzband.c reads it in place; the function pointers in op1_fx_t are never called.
 *
 *   cc -O2 -Imock/op1r-loader web/pack2web.c -o out/pack2web && out/pack2web packs/out/jbpiano.op1p web/packs/jbpiano.jbp */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "op1r-loader/op1p_load.c"

static uint32_t rd(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: pack2web in.op1p out.jbp\n"); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; }
    static uint8_t file[4 << 20] __attribute__((aligned(4)));
    uint32_t len = (uint32_t)fread(file, 1, sizeof file, f); fclose(f);
    op1p_view v; int r = op1p_parse(file, len, &v);
    if (r) { fprintf(stderr, "%s: the op1r loader refuses it (%d)\n", argv[1], r); return 1; }
    uint8_t *mem = calloc(1, v.mem_size); uint32_t imp[OP1P_MAX_IMPORTS];
    for (uint32_t i = 0; i < v.hdr->n_imports; i++) imp[i] = 0x01000000u + 16 * i;
    if (op1p_relocate(&v, mem, 0x00400000u, imp)) { fprintf(stderr, "%s: relocation failed\n", argv[1]); return 1; }
    const uint8_t *e = mem + v.hdr->entry_off;
    uint32_t magic0 = rd(e + 16), data_off = rd(e + 32), data_len = rd(e + 36);
    if (magic0 != 0x4B50424Au || v.hdr->entry_off + data_off + data_len > v.mem_size) {
        fprintf(stderr, "%s: not a jbpack\n", argv[1]); return 1;
    }
    FILE *o = fopen(argv[2], "wb"); if (!o) { perror(argv[2]); return 1; }
    fwrite(e, 1, data_off + data_len, o); fclose(o);
    printf("%s: %u bytes (header %u, audio %u)\n", argv[2], data_off + data_len, data_off, data_len);
    return 0;
}
