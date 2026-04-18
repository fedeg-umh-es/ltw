#include "arith_coder.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ── Adaptive frequency model ───────────────────────────────────────────── */

void ac_model_init(ACModel *m, int nsymbols)
{
    int i;
    m->nsymbols = nsymbols;
    /* initialize all symbol frequencies to 1 (equal probability)          */
    for (i = 0; i <= nsymbols; i++)
        m->freq[i] = (uint32_t)i;   /* cumulative: freq[i] = i             */
    m->total = (uint32_t)nsymbols;
}

/* Rescale to avoid overflow: halve all counts (keep minimum of 1).        */
static void model_rescale(ACModel *m)
{
    int i;
    /* rebuild cumulative table from scratch after halving individual freqs  */
    uint32_t individual[AC_MAX_SYMBOLS];
    for (i = 0; i < m->nsymbols; i++) {
        uint32_t f = m->freq[i+1] - m->freq[i];
        individual[i] = (f > 1) ? (f >> 1) : 1;
    }
    m->freq[0] = 0;
    for (i = 0; i < m->nsymbols; i++)
        m->freq[i+1] = m->freq[i] + individual[i];
    m->total = m->freq[m->nsymbols];
}

void ac_model_update(ACModel *m, int symbol)
{
    int i;
    for (i = symbol + 1; i <= m->nsymbols; i++)
        m->freq[i]++;
    m->total++;
    if (m->total > 0x3FFF)   /* rescale when table grows too large         */
        model_rescale(m);
}

/* ── Range encoder ─────────────────────────────────────────────────────── */

#define RANGE_TOP  0x80000000U
#define RANGE_BOT  0x00800000U   /* minimum range before renorm            */

void ac_enc_init(ACEncoder *enc, uint8_t *buf, int buf_max)
{
    enc->buf     = buf;
    enc->buf_max = buf_max;
    enc->pos     = 0;
    enc->low     = 0;
    enc->range   = 0xFFFFFFFFU;
}

static void enc_emit_byte(ACEncoder *enc, uint8_t b)
{
    if (enc->pos < enc->buf_max)
        enc->buf[enc->pos] = b;
    enc->pos++;
}

/* Renormalize: shift out bytes from the top of the range.                 */
static void enc_renorm(ACEncoder *enc)
{
    while (enc->range < RANGE_BOT) {
        enc_emit_byte(enc, (uint8_t)(enc->low >> 24));
        enc->low   <<= 8;
        enc->range <<= 8;
    }
}

int ac_enc_symbol(ACEncoder *enc, ACModel *m, int symbol)
{
    uint32_t r, cum_lo, cum_hi, total;

    if (symbol < 0 || symbol >= m->nsymbols) return -1;

    total  = m->total;
    cum_lo = m->freq[symbol];
    cum_hi = m->freq[symbol + 1];

    r = enc->range / total;
    enc->low   += r * cum_lo;
    enc->range  = r * (cum_hi - cum_lo);

    enc_renorm(enc);
    ac_model_update(m, symbol);
    return 0;
}

int ac_enc_flush(ACEncoder *enc)
{
    int i;
    for (i = 0; i < 4; i++) {
        enc_emit_byte(enc, (uint8_t)(enc->low >> 24));
        enc->low <<= 8;
    }
    return enc->pos;
}

/* ── Raw bit I/O (interleaved in the AC byte stream) ───────────────────── */

void ac_enc_raw_bits(ACEncoder *enc, uint32_t bits, int nbits)
{
    /* Flush current range state first, then write raw bytes.
       We use a simple approach: write as raw bytes (not range-coded).
       To interleave cleanly we pack into whole bytes.                      */
    int shift = nbits - 1;
    while (shift >= 0) {
        int chunk = (shift >= 7) ? 8 : (shift + 1);
        uint8_t byte = (uint8_t)((bits >> (shift - chunk + 1)) & ((1u << chunk) - 1));
        /* Tag raw bytes with a sentinel in the range coder stream:
           we suspend the range coder and write verbatim bytes.             */
        enc_emit_byte(enc, byte);
        shift -= chunk;
    }
}

uint32_t ac_dec_raw_bits(ACDecoder *dec, int nbits)
{
    uint32_t result = 0;
    int remaining = nbits;
    while (remaining > 0) {
        int chunk = (remaining >= 8) ? 8 : remaining;
        uint8_t byte = 0;
        if (dec->pos < dec->buf_len)
            byte = dec->buf[dec->pos++];
        result = (result << chunk) | (byte & ((1u << chunk) - 1));
        remaining -= chunk;
    }
    return result;
}

/* ── Range decoder ─────────────────────────────────────────────────────── */

static uint8_t dec_read_byte(ACDecoder *dec)
{
    if (dec->pos < dec->buf_len)
        return dec->buf[dec->pos++];
    return 0;
}

void ac_dec_init(ACDecoder *dec, const uint8_t *buf, int buf_len)
{
    int i;
    dec->buf     = buf;
    dec->buf_len = buf_len;
    dec->pos     = 0;
    dec->low     = 0;
    dec->range   = 0xFFFFFFFFU;
    dec->code    = 0;
    for (i = 0; i < 4; i++)
        dec->code = (dec->code << 8) | dec_read_byte(dec);
}

/* Binary search for symbol in cumulative frequency table.                 */
static int model_find_symbol(const ACModel *m, uint32_t scaled)
{
    int lo = 0, hi = m->nsymbols - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (m->freq[mid] <= scaled)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

int ac_dec_symbol(ACDecoder *dec, ACModel *m)
{
    uint32_t r, total, scaled;
    int symbol;

    total  = m->total;
    r      = dec->range / total;
    scaled = (dec->code - dec->low) / r;
    if (scaled >= total) scaled = total - 1;

    symbol = model_find_symbol(m, scaled);

    dec->low   += r * m->freq[symbol];
    dec->range  = r * (m->freq[symbol + 1] - m->freq[symbol]);

    /* renormalize */
    while (dec->range < RANGE_BOT) {
        dec->code  = (dec->code  << 8) | dec_read_byte(dec);
        dec->low  <<= 8;
        dec->range <<= 8;
    }

    ac_model_update(m, symbol);
    return symbol;
}
