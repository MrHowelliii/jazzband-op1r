/* IMA ADPCM, 4 bits a sample, low nibble first: the packer's encoder and the reference decoder (the same steps as the
 * jazzband plugin's pack voice). Build: cc -O2 -shared -fPIC adpcm.c -o adpcm.so */
#include <stdint.h>
static const int16_t STEP[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60,
    66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767};
static const int8_t IDX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static int nib_decode(int nib, int *pred, int *idx) {
    int step = STEP[*idx], diff = step >> 3;
    if (nib & 4) diff += step; if (nib & 2) diff += step >> 1; if (nib & 1) diff += step >> 2;
    int p = *pred + ((nib & 8) ? -diff : diff);
    if (p > 32767) p = 32767; if (p < -32768) p = -32768;
    *pred = p; *idx += IDX[nib & 7]; if (*idx < 0) *idx = 0; if (*idx > 88) *idx = 88;
    return p;
}
/* encode n samples (the first is the start predictor, emitted too); returns squared error sum */
double encode(const int16_t *x, int n, uint8_t *out, int pred, int idx) {
    double err = 0;
    for (int i = 0; i < n; i++) {
        int d = x[i] - pred, nib = 0, step = STEP[idx];
        if (d < 0) { nib = 8; d = -d; }
        if (d >= step) { nib |= 4; d -= step; } if (d >= step >> 1) { nib |= 2; d -= step >> 1; } if (d >= step >> 2) nib |= 1;
        int y = nib_decode(nib, &pred, &idx);
        err += (double)(y - x[i]) * (y - x[i]);
        if (i & 1) out[i >> 1] |= (uint8_t)(nib << 4); else out[i >> 1] = (uint8_t)nib;
    }
    return err;
}
void decode(const uint8_t *in, int n, int16_t *y, int pred, int idx) {
    for (int i = 0; i < n; i++) y[i] = (int16_t)nib_decode((in[i >> 1] >> ((i & 1) * 4)) & 15, &pred, &idx);
}
