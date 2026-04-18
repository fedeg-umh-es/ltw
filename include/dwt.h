#ifndef DWT_H
#define DWT_H

/* Daubechies 9/7 lifting-scheme DWT — in-place, separable 2D.
   Oliver & Malumbres (2006) uses this transform.                          */

/* Forward DWT: pixels → coefficients (N levels, in-place).
   buf[height*width] is modified in-place.
   Integer lifting (fixed-point, FPGA-friendly).                           */
void dwt_fwd(int *buf, int width, int height, int levels);

/* Inverse DWT: coefficients → pixels (N levels, in-place).               */
void dwt_inv(int *buf, int width, int height, int levels);

/* Floating-point reference versions (for testing).                        */
void dwt_fwd_float(float *buf, int width, int height, int levels);
void dwt_inv_float(float *buf, int width, int height, int levels);

#endif /* DWT_H */
