#include "ltw.h"
#include "arith_coder.h"
#include "dwt.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ── Helpers ────────────────────────────────────────────────────────────── */

static int iabs(int x) { return x < 0 ? -x : x; }

/* ceil(log2(v)) for v > 0 */
static int ceil_log2(int v)
{
    int b = 0;
    v--;
    while (v > 0) { b++; v >>= 1; }
    return b;
}

/* ── Subband layout helpers (Algorithm 1, Oliver & Malumbres 2006) ────── */
/* At decomposition level lv (1 = finest, N = coarsest):
   The LL subband at level lv occupies rows [0, h>>lv) x cols [0, w>>lv).
   HL_lv: rows [0, h>>lv)  x cols [w>>lv, w>>(lv-1))
   LH_lv: rows [h>>lv, h>>(lv-1)) x cols [0, w>>lv)
   HH_lv: rows [h>>lv, h>>(lv-1)) x cols [w>>lv, w>>(lv-1))            */
void ltw_subband_rect(int width, int height, int level, int band,
                      int *r0, int *c0, int *sh, int *sw)
{
    int lv = level;
    int h_lo = height >> lv;
    int w_lo = width  >> lv;
    int h_hi = h_lo;   /* == height >> (lv) == (height>>(lv-1)) >> 1       */
    int w_hi = w_lo;
    switch (band) {
        case 0: /* HL: top-right */
            *r0 = 0;    *c0 = w_lo; *sh = h_hi; *sw = w_hi; break;
        case 1: /* LH: bottom-left */
            *r0 = h_lo; *c0 = 0;    *sh = h_hi; *sw = w_hi; break;
        case 2: /* HH: bottom-right */
            *r0 = h_lo; *c0 = w_lo; *sh = h_hi; *sw = w_hi; break;
        case 3: /* LL: top-left (only at level N) */
            *r0 = 0;    *c0 = 0;    *sh = h_lo; *sw = w_lo; break;
        default:
            *r0 = *c0 = *sh = *sw = 0;
    }
}

/* Returns top-left (cr,cc) of the 2x2 child block of (r,c) at level lv.
   Level 1 has no children (finest subbands).                              */
int ltw_children(int width, int height, int level,
                 int r, int c, int *child_r, int *child_c)
{
    (void)width; (void)height;
    if (level <= 1) return 0;

    /* The parent at level lv maps to position (pr, pc) within its subband.
       The children are the corresponding 2x2 block in the level-(lv-1) subbands.
       For band HL_lv at (r, c): pr = r, pc = c - (width >> lv).
       Child block top-left in HL_(lv-1):
         child_r = pr * 2,  child_c = (width >> (lv-1)) + pc * 2           */
    /* Simpler: children of any coeff (r,c) at level lv lie at rows 2r,2r+1
       and cols 2c, 2c+1 within the SAME spatial region at level lv-1.
       Because subbands tile in quadrants this maps directly.               */
    *child_r = r * 2;
    *child_c = c * 2;
    return 1;
}

/* ── LTW state allocation ──────────────────────────────────────────────── */

LTWState *ltw_state_alloc(int width, int height, int N, int rplanes)
{
    LTWState *s = (LTWState *)malloc(sizeof(LTWState));
    if (!s) return NULL;
    s->width   = width;
    s->height  = height;
    s->N       = N;
    s->rplanes = rplanes;
    s->maxplane = 0;
    s->coeff = (int *)calloc(width * height, sizeof(int));
    s->label = (uint8_t *)calloc(width * height, sizeof(uint8_t));
    if (!s->coeff || !s->label) { free(s->coeff); free(s->label); free(s); return NULL; }
    return s;
}

void ltw_state_free(LTWState *s)
{
    if (!s) return;
    free(s->coeff);
    free(s->label);
    free(s);
}

/* ── Pass E1: initialization ────────────────────────────────────────────── */
/* Algorithm 1, §III-A, Oliver & Malumbres 2006                             */
static int pass_e1_init(LTWState *s)
{
    int i, n = s->width * s->height;
    int maxabs = 0;

    for (i = 0; i < n; i++) {
        int a = iabs(s->coeff[i]);
        if (a > maxabs) maxabs = a;
        s->label[i] = SIGNIFICANT; /* default; E2 may downgrade            */
    }

    /* Mark insignificant coefficients */
    for (i = 0; i < n; i++) {
        if (iabs(s->coeff[i]) < (1 << s->rplanes))
            s->label[i] = LOWER_COMPONENT; /* placeholder; E2 will fix     */
    }

    s->maxplane = (maxabs > 0) ? ceil_log2(maxabs + 1) : 1;
    return 0;
}

/* ── Check if ALL 2x2 descendants of (r,c,level) are LOWER_COMPONENT ──── */
/* Recursive descent; returns 1 if all descendants are LOWER_COMPONENT
   or if (r,c) has no descendants at all.                                   */
static int all_descendants_lower_component(const LTWState *s,
                                            int r, int c, int level)
{
    int cr, cc, br, bc;
    int r0, c0, sh, sw;
    int band;

    if (level <= 1) return 1;  /* no children below level 1               */

    /* The 4 direct children of (r,c) at level lv are at level (lv-1).
       They occupy the same relative (row,col) position scaled by 2.
       We need to find which subband each child belongs to by checking
       what subband (r,c) itself is in, then mapping to level-1 subbands.  */

    /* Find the subband of (r,c) */
    for (band = 0; band < 3; band++) {
        ltw_subband_rect(s->width, s->height, level, band, &r0, &c0, &sh, &sw);
        if (r >= r0 && r < r0+sh && c >= c0 && c < c0+sw) break;
    }
    if (band == 3) {
        /* LL subband */
        ltw_subband_rect(s->width, s->height, level, 3, &r0, &c0, &sh, &sw);
        if (!(r >= r0 && r < r0+sh && c >= c0 && c < c0+sw)) return 1;
        band = 3;
    }

    /* Local position within the subband */
    int pr = r - r0;
    int pc = c - c0;
    int child_level = level - 1;

    /* The 4 children occupy positions (2*pr, 2*pc), (2*pr, 2*pc+1),
       (2*pr+1, 2*pc), (2*pr+1, 2*pc+1) in the SAME band at level-1.      */
    int child_r0, child_c0, child_sh, child_sw;
    ltw_subband_rect(s->width, s->height, child_level, (band == 3 ? 3 : band),
                     &child_r0, &child_c0, &child_sh, &child_sw);

    for (br = 0; br < 2; br++) {
        for (bc = 0; bc < 2; bc++) {
            cr = child_r0 + 2*pr + br;
            cc = child_c0 + 2*pc + bc;
            if (cr >= child_r0+child_sh || cc >= child_c0+child_sw) continue;
            if (s->label[ltw_idx(s->width, cr, cc)] != LOWER_COMPONENT)
                return 0;
            if (!all_descendants_lower_component(s, cr, cc, child_level))
                return 0;
        }
    }
    return 1;
}

/* ── Pass E2: label computation (leaves to root) ────────────────────────── */
/* Algorithm 1, §III-B, Oliver & Malumbres 2006                             */
static void pass_e2_level(LTWState *s, int level)
{
    int band, r0, c0, sh, sw;
    int max_band = (level == s->N) ? 4 : 3;

    for (band = 0; band < max_band; band++) {
        int r, c;
        ltw_subband_rect(s->width, s->height, level, band, &r0, &c0, &sh, &sw);

        /* Scan in 2x2 blocks */
        for (r = r0; r < r0 + sh; r += 2) {
            for (c = c0; c < c0 + sw; c += 2) {
                int br, bc;
                int all_insig = 1;
                int all_desc_lc[2][2];

                /* First pass: check significance and descendant status     */
                for (br = 0; br < 2 && r+br < r0+sh; br++) {
                    for (bc = 0; bc < 2 && c+bc < c0+sw; bc++) {
                        int idx = ltw_idx(s->width, r+br, c+bc);
                        int sig = (iabs(s->coeff[idx]) >= (1 << s->rplanes));
                        if (sig) all_insig = 0;
                        all_desc_lc[br][bc] =
                            all_descendants_lower_component(s, r+br, c+bc, level);
                    }
                }

                if (all_insig) {
                    /* Check CASE A: all 4 have no children or all desc = LC */
                    int case_a = 1;
                    for (br = 0; br < 2 && r+br < r0+sh; br++) {
                        for (bc = 0; bc < 2 && c+bc < c0+sw; bc++) {
                            if (!all_desc_lc[br][bc]) { case_a = 0; break; }
                        }
                        if (!case_a) break;
                    }

                    if (case_a) {
                        /* CASE A: entire block → LOWER_COMPONENT          */
                        for (br = 0; br < 2 && r+br < r0+sh; br++)
                            for (bc = 0; bc < 2 && c+bc < c0+sw; bc++)
                                s->label[ltw_idx(s->width, r+br, c+bc)] = LOWER_COMPONENT;
                    } else {
                        /* CASE B: mixed block — individual labeling        */
                        for (br = 0; br < 2 && r+br < r0+sh; br++) {
                            for (bc = 0; bc < 2 && c+bc < c0+sw; bc++) {
                                int idx = ltw_idx(s->width, r+br, c+bc);
                                if (all_desc_lc[br][bc])
                                    s->label[idx] = LOWER;
                                else
                                    s->label[idx] = ISOLATED_LOWER;
                            }
                        }
                    }
                } else {
                    /* Mixed block with significant coefficients:
                       label insignificant ones per CASE B rules            */
                    for (br = 0; br < 2 && r+br < r0+sh; br++) {
                        for (bc = 0; bc < 2 && c+bc < c0+sw; bc++) {
                            int idx = ltw_idx(s->width, r+br, c+bc);
                            int sig = (iabs(s->coeff[idx]) >= (1 << s->rplanes));
                            if (!sig) {
                                if (all_desc_lc[br][bc])
                                    s->label[idx] = LOWER;
                                else
                                    s->label[idx] = ISOLATED_LOWER;
                            } else {
                                s->label[idx] = SIGNIFICANT;
                            }
                        }
                    }
                }
            }
        }
    }
}

static void pass_e2(LTWState *s)
{
    /* Algorithm 1, E2: scan from level 1 (finest) upward to level N       */
    int lv;
    for (lv = 1; lv <= s->N; lv++)
        pass_e2_level(s, lv);
}

/* ── Context selection ─────────────────────────────────────────────────── */
/* Context 0: left AND upper neighbors both insignificant.
   Context 1: otherwise.                                                    */
static int get_context(const LTWState *s, int r, int c)
{
    int left_sig  = 0, up_sig = 0;
    if (c > 0) left_sig = (s->label[ltw_idx(s->width, r,   c-1)] == SIGNIFICANT);
    if (r > 0) up_sig   = (s->label[ltw_idx(s->width, r-1, c  )] == SIGNIFICANT);
    return (left_sig || up_sig) ? 1 : 0;
}

/* ── Check if all direct children of (r,c,level) are LOWER_COMPONENT ─── */
static int direct_children_all_lc(const LTWState *s, int r, int c, int level)
{
    int band, r0, c0, sh, sw, pr, pc;
    int child_r0, child_c0, child_sh, child_sw;
    int br, bc;

    if (level <= 1) return 1;

    for (band = 0; band < 4; band++) {
        ltw_subband_rect(s->width, s->height, level, band, &r0, &c0, &sh, &sw);
        if (r >= r0 && r < r0+sh && c >= c0 && c < c0+sw) break;
    }
    if (band == 4) return 1;

    pr = r - r0;
    pc = c - c0;
    int child_level = level - 1;
    int child_band  = (band == 3) ? 3 : band;
    ltw_subband_rect(s->width, s->height, child_level, child_band,
                     &child_r0, &child_c0, &child_sh, &child_sw);

    for (br = 0; br < 2; br++) {
        for (bc = 0; bc < 2; bc++) {
            int cr = child_r0 + 2*pr + br;
            int cc = child_c0 + 2*pc + bc;
            if (cr >= child_r0+child_sh || cc >= child_c0+child_sw) continue;
            if (s->label[ltw_idx(s->width, cr, cc)] != LOWER_COMPONENT) return 0;
        }
    }
    return 1;
}

/* ── Pass E3: output coefficients (level N down to level 1) ─────────────── */
/* Algorithm 1, §III-C, Oliver & Malumbres 2006                              */
static int pass_e3(const LTWState *s, ACModel *models,
                   ACEncoder *enc, int num_symbols)
{
    int lv, band, r, c, r0, c0, sh, sw;
    (void)num_symbols;

    for (lv = s->N; lv >= 1; lv--) {
        int max_band = (lv == s->N) ? 4 : 3;
        for (band = 0; band < max_band; band++) {
            int is_ll = (lv == s->N && band == 3);
            ltw_subband_rect(s->width, s->height, lv, band, &r0, &c0, &sh, &sw);

            for (r = r0; r < r0 + sh; r++) {
                for (c = c0; c < c0 + sw; c++) {
                    int idx = ltw_idx(s->width, r, c);
                    int label = s->label[idx];
                    int ctx   = get_context(s, r, c);

                    if (label == LOWER_COMPONENT) continue;

                    if (label == LOWER) {
                        ac_enc_symbol(enc, &models[ctx], LOWER);
                        continue;
                    }

                    if (label == ISOLATED_LOWER) {
                        ac_enc_symbol(enc, &models[ctx], ISOLATED_LOWER);
                        continue;
                    }

                    /* SIGNIFICANT */
                    {
                        int val   = iabs(s->coeff[idx]);
                        int nbits = ceil_log2(val + 1);
                        if (nbits < 1) nbits = 1;

                        int all_dc_lc = direct_children_all_lc(s, r, c, lv);
                        /* Symbol index:
                           0 = LOWER_COMPONENT (not sent)
                           1 = LOWER
                           2 = ISOLATED_LOWER
                           3..maxplane+2 = nbits (regular)
                           maxplane+3..2*maxplane+2 = nbits^LOWER variant  */
                        int sym;
                        if (all_dc_lc)
                            sym = s->maxplane + 2 + nbits; /* L-variant    */
                        else
                            sym = 2 + nbits;               /* regular       */

                        ac_enc_symbol(enc, &models[ctx], sym);

                        /* Raw bits: bits [nbits-2 .. rplanes] of val
                           (skip MSB; the rplanes LSBs are discarded)       */
                        if (nbits - 2 >= s->rplanes) {
                            int raw_bits = nbits - 2 - s->rplanes + 1;
                            if (raw_bits > 0) {
                                uint32_t mantissa = (uint32_t)(val >> s->rplanes)
                                                    & ((1u << (nbits - 1 - s->rplanes)) - 1);
                                ac_enc_raw_bits(enc, mantissa, raw_bits);
                            }
                        }

                        /* Sign bit (except LL subband) */
                        if (!is_ll) {
                            int sign = (s->coeff[idx] < 0) ? 1 : 0;
                            ac_enc_raw_bits(enc, (uint32_t)sign, 1);
                        }
                    }
                }
            }
        }
    }
    return 0;
}

/* ── Top-level encode ────────────────────────────────────────────────────── */
int ltw_encode(LTWState *s, const uint8_t *pixels, uint8_t *out, int out_max)
{
    int i, n = s->width * s->height;
    ACModel models[2];
    ACEncoder enc;
    int num_symbols;

    /* Copy pixels to coefficient array */
    for (i = 0; i < n; i++) s->coeff[i] = (int)pixels[i];

    /* Forward DWT */
    dwt_fwd(s->coeff, s->width, s->height, s->N);

    /* Pass E1 */
    pass_e1_init(s);

    /* Pass E2 */
    pass_e2(s);

    /* num_symbols = 2*(2^maxplane - 2^rplanes) + 2, §III-A              */
    num_symbols = 2 * ((1 << s->maxplane) - (1 << s->rplanes)) + 2;
    if (num_symbols < 4) num_symbols = 4;
    if (num_symbols >= AC_MAX_SYMBOLS) num_symbols = AC_MAX_SYMBOLS - 1;

    ac_model_init(&models[0], num_symbols);
    ac_model_init(&models[1], num_symbols);

    /* Write header: rplanes (1 byte), maxplane (1 byte), width (2), height (2), N (1) */
    out[0] = (uint8_t)s->rplanes;
    out[1] = (uint8_t)s->maxplane;
    out[2] = (uint8_t)(s->width  >> 8);
    out[3] = (uint8_t)(s->width  & 0xFF);
    out[4] = (uint8_t)(s->height >> 8);
    out[5] = (uint8_t)(s->height & 0xFF);
    out[6] = (uint8_t)s->N;

    ac_enc_init(&enc, out + 7, out_max - 7);

    /* Pass E3 */
    pass_e3(s, models, &enc, num_symbols);

    int ac_bytes = ac_enc_flush(&enc);
    return 7 + ac_bytes;
}
