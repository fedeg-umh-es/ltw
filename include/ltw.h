#ifndef LTW_H
#define LTW_H

/* Oliver & Malumbres (2006), IEEE TCSVT 16(11) — Lower-Tree Wavelet codec */

#include <stdint.h>
#include <stddef.h>

/* ── Symbol labels ─────────────────────────────────────────────────────── */
#define LOWER_COMPONENT  0   /* part of a lower-tree; not encoded           */
#define LOWER            1   /* root of a lower-tree                        */
#define ISOLATED_LOWER   2   /* insignificant with ≥1 significant descendant*/
#define SIGNIFICANT      3   /* |c| >= 2^rplanes                            */

/* ── Codec state ───────────────────────────────────────────────────────── */
typedef struct {
    int   width;      /* image width  (must be multiple of 2^N)             */
    int   height;     /* image height (must be multiple of 2^N)             */
    int   N;          /* DWT levels                                         */
    int   rplanes;    /* least-significant bits to discard                  */
    int   maxplane;   /* ceil(log2(max|c|)), computed after DWT             */
    int  *coeff;      /* DWT coefficient array [height*width], int Q-format */
    uint8_t *label;   /* symbol label array [height*width]                  */
} LTWState;

/* ── Public API ────────────────────────────────────────────────────────── */
LTWState *ltw_state_alloc(int width, int height, int N, int rplanes);
void      ltw_state_free(LTWState *s);

/* encoder: returns bytes written, or -1 on error */
int ltw_encode(LTWState *s, const uint8_t *pixels, uint8_t *out, int out_max);

/* decoder: returns 0 on success */
int ltw_decode(LTWState *s, const uint8_t *in, int in_len, uint8_t *pixels);

/* ── Subband helpers (used by both encoder and decoder) ─────────────────── */
/* Returns top-left (row,col) and (h,w) of subband at given level and band.
   band: 0=HL, 1=LH, 2=HH, 3=LL (only valid for level==N)                 */
void ltw_subband_rect(int width, int height, int level, int band,
                      int *r0, int *c0, int *sh, int *sw);

/* Linear index into coeff/label arrays */
static inline int ltw_idx(int width, int row, int col)
{
    return row * width + col;
}

/* Children of (r,c) at level lv are the 4 coefficients at level lv-1.
   Returns 1 if children exist (lv>1), 0 otherwise.
   child_r/child_c: top-left of the 2x2 child block.                       */
int ltw_children(int width, int height, int level,
                 int r, int c, int *child_r, int *child_c);

#endif /* LTW_H */
