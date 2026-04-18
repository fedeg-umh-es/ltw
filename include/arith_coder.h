#ifndef ARITH_CODER_H
#define ARITH_CODER_H

/* Adaptive range coder — no external libraries.
   Oliver & Malumbres (2006): 2 contexts, adaptive symbol probabilities.   */

#include <stdint.h>
#include <stddef.h>

#define AC_MAX_SYMBOLS  4096   /* generous upper bound                      */

typedef struct {
    int   nsymbols;
    uint32_t freq[AC_MAX_SYMBOLS + 1];  /* cumulative frequency table       */
    uint32_t total;
} ACModel;

typedef struct {
    uint8_t  *buf;       /* output buffer                                   */
    int       buf_max;   /* capacity                                        */
    int       pos;       /* bytes written                                   */
    uint32_t  low;
    uint32_t  range;
} ACEncoder;

typedef struct {
    const uint8_t *buf;
    int            buf_len;
    int            pos;
    uint32_t       low;
    uint32_t       range;
    uint32_t       code;  /* current window                                 */
} ACDecoder;

/* Model */
void ac_model_init(ACModel *m, int nsymbols);
void ac_model_update(ACModel *m, int symbol);

/* Encoder */
void ac_enc_init(ACEncoder *enc, uint8_t *buf, int buf_max);
int  ac_enc_symbol(ACEncoder *enc, ACModel *m, int symbol);
int  ac_enc_flush(ACEncoder *enc);   /* returns total bytes written         */

/* Decoder */
void ac_dec_init(ACDecoder *dec, const uint8_t *buf, int buf_len);
int  ac_dec_symbol(ACDecoder *dec, ACModel *m); /* returns symbol or -1    */

/* Raw bit I/O (for sign bits and mantissa bits outside AC) */
/* These operate on a separate byte stream appended after AC flush.
   For simplicity we embed them inside the AC byte stream as literal bytes. */
void ac_enc_raw_bits(ACEncoder *enc, uint32_t bits, int nbits);
uint32_t ac_dec_raw_bits(ACDecoder *dec, int nbits);

#endif /* ARITH_CODER_H */
