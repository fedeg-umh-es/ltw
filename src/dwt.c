#include "dwt.h"
#include <stdlib.h>
#include <string.h>

/* ── Daubechies 9/7 lifting coefficients (Cohen-Daubechies-Feauveau 9/7) ─ */
/* Standard lifting factorization, e.g. Daubechies & Sweldens (1998).      */
#define ALPHA  (-1.586134342f)
#define BETA   (-0.052980118f)
#define GAMMA  ( 0.882911075f)
#define DELTA  ( 0.443506852f)
#define K      ( 1.149604398f)   /* scaling factor for even samples        */
#define K_INV  (1.0f / K)

/* ── Float 1D forward DWT (in-place, length n, stride s) ───────────────── */
static void dwt1d_fwd_float(float *x, int n, int s)
{
    int i;
    /* Step 1: predict odd from even (alpha) */
    for (i = 1; i < n - 1; i += 2)
        x[i*s] += ALPHA * (x[(i-1)*s] + x[(i+1)*s]);
    if ((n & 1) == 0)
        x[(n-1)*s] += 2.0f * ALPHA * x[(n-2)*s];

    /* Step 2: update even from odd (beta) */
    x[0] += 2.0f * BETA * x[s];
    for (i = 2; i < n; i += 2)
        x[i*s] += BETA * (x[(i-1)*s] + x[(i+1 < n ? i+1 : i-1)*s]);

    /* Step 3: predict odd from updated even (gamma) */
    for (i = 1; i < n - 1; i += 2)
        x[i*s] += GAMMA * (x[(i-1)*s] + x[(i+1)*s]);
    if ((n & 1) == 0)
        x[(n-1)*s] += 2.0f * GAMMA * x[(n-2)*s];

    /* Step 4: update even from odd (delta) */
    x[0] += 2.0f * DELTA * x[s];
    for (i = 2; i < n; i += 2)
        x[i*s] += DELTA * (x[(i-1)*s] + x[(i+1 < n ? i+1 : i-1)*s]);

    /* Step 5: scale */
    for (i = 0; i < n; i += 2) x[i*s] *=  K_INV;
    for (i = 1; i < n; i += 2) x[i*s] *=  K;

    /* De-interleave: even → low-pass half, odd → high-pass half.
       We do an in-place polyphase split via a scratch buffer.              */
    {
        float *tmp = (float *)malloc(n * sizeof(float));
        if (!tmp) return;
        for (i = 0; i < n; i++) tmp[i] = x[i*s];
        for (i = 0; i < (n+1)/2; i++) x[i*s]         = tmp[2*i];
        for (i = 0; i < n/2;     i++) x[(i+( n+1)/2)*s] = tmp[2*i+1];
        free(tmp);
    }
}

/* ── Float 1D inverse DWT ─────────────────────────────────────────────── */
static void dwt1d_inv_float(float *x, int n, int s)
{
    int i;
    int half = (n + 1) / 2;

    /* Re-interleave */
    {
        float *tmp = (float *)malloc(n * sizeof(float));
        if (!tmp) return;
        for (i = 0; i < n; i++) tmp[i] = x[i*s];
        for (i = 0; i < half;   i++) x[(2*i)*s]   = tmp[i];
        for (i = 0; i < n/2;    i++) x[(2*i+1)*s] = tmp[half + i];
        free(tmp);
    }

    /* Undo scale */
    for (i = 0; i < n; i += 2) x[i*s] *=  K;
    for (i = 1; i < n; i += 2) x[i*s] *=  K_INV;

    /* Undo step 4 */
    x[0] -= 2.0f * DELTA * x[s];
    for (i = 2; i < n; i += 2)
        x[i*s] -= DELTA * (x[(i-1)*s] + x[(i+1 < n ? i+1 : i-1)*s]);

    /* Undo step 3 */
    for (i = 1; i < n - 1; i += 2)
        x[i*s] -= GAMMA * (x[(i-1)*s] + x[(i+1)*s]);
    if ((n & 1) == 0)
        x[(n-1)*s] -= 2.0f * GAMMA * x[(n-2)*s];

    /* Undo step 2 */
    x[0] -= 2.0f * BETA * x[s];
    for (i = 2; i < n; i += 2)
        x[i*s] -= BETA * (x[(i-1)*s] + x[(i+1 < n ? i+1 : i-1)*s]);

    /* Undo step 1 */
    for (i = 1; i < n - 1; i += 2)
        x[i*s] -= ALPHA * (x[(i-1)*s] + x[(i+1)*s]);
    if ((n & 1) == 0)
        x[(n-1)*s] -= 2.0f * ALPHA * x[(n-2)*s];
}

/* ── Float 2D DWT ─────────────────────────────────────────────────────── */
void dwt_fwd_float(float *buf, int width, int height, int levels)
{
    int lv, r, c;
    int w = width, h = height;
    for (lv = 0; lv < levels; lv++) {
        /* rows */
        for (r = 0; r < h; r++)
            dwt1d_fwd_float(buf + r * width, w, 1);
        /* columns */
        for (c = 0; c < w; c++)
            dwt1d_fwd_float(buf + c, h, width);
        w >>= 1;
        h >>= 1;
    }
}

void dwt_inv_float(float *buf, int width, int height, int levels)
{
    int lv, r, c;
    int scales[32];
    int ws[32], hs[32];
    int w = width, h = height;
    for (lv = 0; lv < levels; lv++) { ws[lv] = w; hs[lv] = h; w >>= 1; h >>= 1; }
    (void)scales;
    for (lv = levels - 1; lv >= 0; lv--) {
        w = ws[lv]; h = hs[lv];
        for (c = 0; c < w; c++)
            dwt1d_inv_float(buf + c, h, width);
        for (r = 0; r < h; r++)
            dwt1d_inv_float(buf + r * width, w, 1);
    }
}

/* ── Integer (fixed-point) lifting — Q12 arithmetic ────────────────────── */
/* Coefficients scaled by 2^12 = 4096                                      */
#define FP_ALPHA  (-6497)   /* round(-1.586134342 * 4096)                  */
#define FP_BETA   (  -217)  /* round(-0.052980118 * 4096)                  */
#define FP_GAMMA  (  3616)  /* round( 0.882911075 * 4096)                  */
#define FP_DELTA  (  1817)  /* round( 0.443506852 * 4096)                  */
/* K ≈ 1.1496: scale even * (1/K) ≈ 0.8699 → Q12 = 3563                   */
/* scale odd  * K        ≈ 1.1496 → Q12 = 4709                             */
#define FP_SCALE_EVEN  3563
#define FP_SCALE_ODD   4709
#define FP_SHIFT       12

static void dwt1d_fwd_int(int *x, int n, int s)
{
    int i;

    /* Step 1: predict odd (alpha) */
    for (i = 1; i < n - 1; i += 2)
        x[i*s] += (FP_ALPHA * (x[(i-1)*s] + x[(i+1)*s]) + 2048) >> FP_SHIFT;
    if ((n & 1) == 0)
        x[(n-1)*s] += (2 * FP_ALPHA * x[(n-2)*s] + 2048) >> FP_SHIFT;

    /* Step 2: update even (beta) */
    x[0] += (2 * FP_BETA * x[s] + 2048) >> FP_SHIFT;
    for (i = 2; i < n; i += 2) {
        int next = (i+1 < n) ? i+1 : i-1;
        x[i*s] += (FP_BETA * (x[(i-1)*s] + x[next*s]) + 2048) >> FP_SHIFT;
    }

    /* Step 3: predict odd (gamma) */
    for (i = 1; i < n - 1; i += 2)
        x[i*s] += (FP_GAMMA * (x[(i-1)*s] + x[(i+1)*s]) + 2048) >> FP_SHIFT;
    if ((n & 1) == 0)
        x[(n-1)*s] += (2 * FP_GAMMA * x[(n-2)*s] + 2048) >> FP_SHIFT;

    /* Step 4: update even (delta) */
    x[0] += (2 * FP_DELTA * x[s] + 2048) >> FP_SHIFT;
    for (i = 2; i < n; i += 2) {
        int next = (i+1 < n) ? i+1 : i-1;
        x[i*s] += (FP_DELTA * (x[(i-1)*s] + x[next*s]) + 2048) >> FP_SHIFT;
    }

    /* Step 5: scale */
    for (i = 0; i < n; i += 2)
        x[i*s] = (FP_SCALE_EVEN * x[i*s] + 2048) >> FP_SHIFT;
    for (i = 1; i < n; i += 2)
        x[i*s] = (FP_SCALE_ODD  * x[i*s] + 2048) >> FP_SHIFT;

    /* De-interleave */
    {
        int *tmp = (int *)malloc(n * sizeof(int));
        if (!tmp) return;
        for (i = 0; i < n; i++) tmp[i] = x[i*s];
        for (i = 0; i < (n+1)/2; i++) x[i*s]              = tmp[2*i];
        for (i = 0; i < n/2;     i++) x[(i+(n+1)/2)*s]    = tmp[2*i+1];
        free(tmp);
    }
}

static void dwt1d_inv_int(int *x, int n, int s)
{
    int i;
    int half = (n + 1) / 2;

    /* Re-interleave */
    {
        int *tmp = (int *)malloc(n * sizeof(int));
        if (!tmp) return;
        for (i = 0; i < n; i++) tmp[i] = x[i*s];
        for (i = 0; i < half; i++) x[(2*i)*s]   = tmp[i];
        for (i = 0; i < n/2;  i++) x[(2*i+1)*s] = tmp[half + i];
        free(tmp);
    }

    /* Undo scale (inverse: even *= K, odd *= 1/K) */
    for (i = 0; i < n; i += 2)
        x[i*s] = (FP_SCALE_ODD  * x[i*s] + 2048) >> FP_SHIFT;  /* K */
    for (i = 1; i < n; i += 2)
        x[i*s] = (FP_SCALE_EVEN * x[i*s] + 2048) >> FP_SHIFT;  /* 1/K */

    /* Undo step 4 */
    x[0] -= (2 * FP_DELTA * x[s] + 2048) >> FP_SHIFT;
    for (i = 2; i < n; i += 2) {
        int next = (i+1 < n) ? i+1 : i-1;
        x[i*s] -= (FP_DELTA * (x[(i-1)*s] + x[next*s]) + 2048) >> FP_SHIFT;
    }

    /* Undo step 3 */
    for (i = 1; i < n - 1; i += 2)
        x[i*s] -= (FP_GAMMA * (x[(i-1)*s] + x[(i+1)*s]) + 2048) >> FP_SHIFT;
    if ((n & 1) == 0)
        x[(n-1)*s] -= (2 * FP_GAMMA * x[(n-2)*s] + 2048) >> FP_SHIFT;

    /* Undo step 2 */
    x[0] -= (2 * FP_BETA * x[s] + 2048) >> FP_SHIFT;
    for (i = 2; i < n; i += 2) {
        int next = (i+1 < n) ? i+1 : i-1;
        x[i*s] -= (FP_BETA * (x[(i-1)*s] + x[next*s]) + 2048) >> FP_SHIFT;
    }

    /* Undo step 1 */
    for (i = 1; i < n - 1; i += 2)
        x[i*s] -= (FP_ALPHA * (x[(i-1)*s] + x[(i+1)*s]) + 2048) >> FP_SHIFT;
    if ((n & 1) == 0)
        x[(n-1)*s] -= (2 * FP_ALPHA * x[(n-2)*s] + 2048) >> FP_SHIFT;
}

void dwt_fwd(int *buf, int width, int height, int levels)
{
    int lv, r, c, w = width, h = height;
    for (lv = 0; lv < levels; lv++) {
        for (r = 0; r < h; r++)
            dwt1d_fwd_int(buf + r * width, w, 1);
        for (c = 0; c < w; c++)
            dwt1d_fwd_int(buf + c, h, width);
        w >>= 1; h >>= 1;
    }
}

void dwt_inv(int *buf, int width, int height, int levels)
{
    int lv, r, c;
    int ws[32], hs[32];
    int w = width, h = height;
    for (lv = 0; lv < levels; lv++) { ws[lv] = w; hs[lv] = h; w >>= 1; h >>= 1; }
    for (lv = levels - 1; lv >= 0; lv--) {
        w = ws[lv]; h = hs[lv];
        for (c = 0; c < w; c++)
            dwt1d_inv_int(buf + c, h, width);
        for (r = 0; r < h; r++)
            dwt1d_inv_int(buf + r * width, w, 1);
    }
}
