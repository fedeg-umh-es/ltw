#include "ltw.h"
#include "arith_coder.h"
#include "dwt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declarations of helpers defined in ltw_encoder.c                */
void ltw_subband_rect(int width, int height, int level, int band,
                      int *r0, int *c0, int *sh, int *sw);

static int get_ctx(const LTWState *s, int r, int c)
{
    int ls = 0, us = 0;
    if (c > 0) ls = (s->label[ltw_idx(s->width, r,   c-1)] == SIGNIFICANT);
    if (r > 0) us = (s->label[ltw_idx(s->width, r-1, c  )] == SIGNIFICANT);
    return (ls || us) ? 1 : 0;
}

/* Set all direct children of (r,c) at level lv to LOWER_COMPONENT.        */
static void zero_children(LTWState *s, int r, int c, int level)
{
    int band, r0, c0, sh, sw, pr, pc, child_r0, child_c0, child_sh, child_sw;
    int child_level, child_band, br, bc;

    if (level <= 1) return;

    for (band = 0; band < 4; band++) {
        ltw_subband_rect(s->width, s->height, level, band, &r0, &c0, &sh, &sw);
        if (r >= r0 && r < r0+sh && c >= c0 && c < c0+sw) break;
    }
    if (band == 4) return;

    pr = r - r0; pc = c - c0;
    child_level = level - 1;
    child_band  = (band == 3) ? 3 : band;
    ltw_subband_rect(s->width, s->height, child_level, child_band,
                     &child_r0, &child_c0, &child_sh, &child_sw);

    for (br = 0; br < 2; br++) {
        for (bc = 0; bc < 2; bc++) {
            int cr = child_r0 + 2*pr + br;
            int cc = child_c0 + 2*pc + bc;
            if (cr < child_r0+child_sh && cc < child_c0+child_sw) {
                s->label[ltw_idx(s->width, cr, cc)] = LOWER_COMPONENT;
                s->coeff[ltw_idx(s->width, cr, cc)] = 0;
            }
        }
    }
}

/* Recursively zero all descendants of (r,c) at level lv.                  */
static void zero_descendants(LTWState *s, int r, int c, int level)
{
    int band, r0, c0, sh, sw, pr, pc, child_r0, child_c0, child_sh, child_sw;
    int child_level, child_band, br, bc;

    if (level <= 1) return;

    for (band = 0; band < 4; band++) {
        ltw_subband_rect(s->width, s->height, level, band, &r0, &c0, &sh, &sw);
        if (r >= r0 && r < r0+sh && c >= c0 && c < c0+sw) break;
    }
    if (band == 4) return;

    pr = r - r0; pc = c - c0;
    child_level = level - 1;
    child_band  = (band == 3) ? 3 : band;
    ltw_subband_rect(s->width, s->height, child_level, child_band,
                     &child_r0, &child_c0, &child_sh, &child_sw);

    for (br = 0; br < 2; br++) {
        for (bc = 0; bc < 2; bc++) {
            int cr = child_r0 + 2*pr + br;
            int cc = child_c0 + 2*pc + bc;
            if (cr < child_r0+child_sh && cc < child_c0+child_sw) {
                s->label[ltw_idx(s->width, cr, cc)] = LOWER_COMPONENT;
                s->coeff[ltw_idx(s->width, cr, cc)] = 0;
                zero_descendants(s, cr, cc, child_level);
            }
        }
    }
}

int ltw_decode(LTWState *s, const uint8_t *in, int in_len, uint8_t *pixels)
{
    int lv, band, r, c, r0, c0, sh, sw;
    ACModel models[2];
    ACDecoder dec;
    int num_symbols;

    if (in_len < 7) return -1;

    /* Parse header */
    s->rplanes  = in[0];
    s->maxplane = in[1];
    s->width    = ((int)in[2] << 8) | in[3];
    s->height   = ((int)in[4] << 8) | in[5];
    s->N        = in[6];

    num_symbols = 2 * ((1 << s->maxplane) - (1 << s->rplanes)) + 2;
    if (num_symbols < 4) num_symbols = 4;
    if (num_symbols >= AC_MAX_SYMBOLS) num_symbols = AC_MAX_SYMBOLS - 1;

    ac_model_init(&models[0], num_symbols);
    ac_model_init(&models[1], num_symbols);

    /* Initialize coefficients and labels to zero */
    memset(s->coeff, 0, s->width * s->height * sizeof(int));
    memset(s->label, LOWER_COMPONENT, s->width * s->height);

    ac_dec_init(&dec, in + 7, in_len - 7);

    /* Single-pass decode: level N down to level 1                          */
    for (lv = s->N; lv >= 1; lv--) {
        int max_band = (lv == s->N) ? 4 : 3;
        for (band = 0; band < max_band; band++) {
            int is_ll = (lv == s->N && band == 3);
            ltw_subband_rect(s->width, s->height, lv, band, &r0, &c0, &sh, &sw);

            for (r = r0; r < r0 + sh; r++) {
                for (c = c0; c < c0 + sw; c++) {
                    int idx = ltw_idx(s->width, r, c);

                    /* If this coeff was already marked LOWER_COMPONENT by
                       ancestor propagation, skip it.                        */
                    if (s->label[idx] == LOWER_COMPONENT) continue;

                    int ctx = get_ctx(s, r, c);
                    int sym = ac_dec_symbol(&dec, &models[ctx]);
                    if (sym < 0) return -1;

                    if (sym == LOWER) {
                        s->label[idx] = LOWER_COMPONENT;
                        s->coeff[idx] = 0;
                        zero_descendants(s, r, c, lv);
                        continue;
                    }

                    if (sym == ISOLATED_LOWER) {
                        /* keep as ISOLATED_LOWER; set coeff to 0 but don't
                           propagate yet — children will be decoded normally */
                        s->label[idx] = ISOLATED_LOWER;
                        s->coeff[idx] = 0;
                        continue;
                    }

                    /* Numeric symbol: either regular or L-variant          */
                    int nbits;
                    int is_lower_variant;
                    if (sym > s->maxplane + 2) {
                        /* L-variant: sym = maxplane + 2 + nbits            */
                        nbits = sym - s->maxplane - 2;
                        is_lower_variant = 1;
                    } else {
                        /* regular: sym = 2 + nbits                         */
                        nbits = sym - 2;
                        is_lower_variant = 0;
                    }

                    /* Reconstruct magnitude */
                    int val = (1 << (nbits - 1));  /* MSB                  */
                    /* Read raw mantissa bits */
                    if (nbits - 2 >= s->rplanes) {
                        int raw_bits = nbits - 2 - s->rplanes + 1;
                        if (raw_bits > 0) {
                            uint32_t mantissa = ac_dec_raw_bits(&dec, raw_bits);
                            val |= (int)(mantissa << s->rplanes);
                        }
                    }
                    /* rplanes LSBs set to 0 (discarded)                    */

                    /* Sign (except LL) */
                    if (!is_ll) {
                        int sign = (int)ac_dec_raw_bits(&dec, 1);
                        if (sign) val = -val;
                    }

                    s->coeff[idx] = val;
                    s->label[idx] = SIGNIFICANT;

                    /* If L-variant: zero direct children                   */
                    if (is_lower_variant)
                        zero_children(s, r, c, lv);
                }
            }
        }
    }

    /* Inverse DWT */
    dwt_inv(s->coeff, s->width, s->height, s->N);

    /* Clamp and write pixels */
    {
        int i, n = s->width * s->height;
        for (i = 0; i < n; i++) {
            int v = s->coeff[i];
            if (v < 0)   v = 0;
            if (v > 255) v = 255;
            pixels[i] = (uint8_t)v;
        }
    }
    return 0;
}
