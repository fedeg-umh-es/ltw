#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ltw.h"

/* ── Minimal PGM reader/writer ─────────────────────────────────────────── */

static int pgm_skip_comment(FILE *f)
{
    int c;
    while ((c = fgetc(f)) == '#') {
        while ((c = fgetc(f)) != '\n' && c != EOF) {}
    }
    ungetc(c, f);
    return 0;
}

static uint8_t *pgm_read(const char *path, int *out_w, int *out_h)
{
    FILE *f = fopen(path, "rb");
    char magic[3];
    int w, h, maxval;
    uint8_t *buf;

    if (!f) { fprintf(stderr, "Cannot open %s\n", path); return NULL; }
    if (fscanf(f, "%2s", magic) != 1 || strcmp(magic, "P5") != 0) {
        fprintf(stderr, "Not a P5 PGM: %s\n", path); fclose(f); return NULL;
    }
    pgm_skip_comment(f);
    if (fscanf(f, " %d %d %d", &w, &h, &maxval) != 3) {
        fprintf(stderr, "Bad PGM header\n"); fclose(f); return NULL;
    }
    fgetc(f); /* consume whitespace after maxval */

    buf = (uint8_t *)malloc(w * h);
    if (!buf) { fclose(f); return NULL; }
    if ((int)fread(buf, 1, w * h, f) != w * h) {
        fprintf(stderr, "Short read\n"); free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *out_w = w; *out_h = h;
    return buf;
}

static int pgm_write(const char *path, const uint8_t *buf, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "Cannot write %s\n", path); return -1; }
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    fwrite(buf, 1, w * h, f);
    fclose(f);
    return 0;
}

/* ── Encode command ─────────────────────────────────────────────────────── */
static int cmd_encode(const char *inpgm, const char *outltw,
                      int rplanes, int N)
{
    int w, h;
    uint8_t *pixels = pgm_read(inpgm, &w, &h);
    if (!pixels) return 1;

    LTWState *s = ltw_state_alloc(w, h, N, rplanes);
    if (!s) { free(pixels); return 1; }

    int out_max = w * h * 4 + 64;
    uint8_t *out = (uint8_t *)malloc(out_max);
    if (!out) { ltw_state_free(s); free(pixels); return 1; }

    int bytes = ltw_encode(s, pixels, out, out_max);
    if (bytes < 0) {
        fprintf(stderr, "Encode failed\n");
    } else {
        FILE *f = fopen(outltw, "wb");
        if (f) { fwrite(out, 1, bytes, f); fclose(f); }
        printf("Encoded %d x %d → %d bytes (%.2f bpp)\n",
               w, h, bytes, (bytes * 8.0) / (w * h));
    }

    free(out); ltw_state_free(s); free(pixels);
    return (bytes < 0) ? 1 : 0;
}

/* ── Decode command ─────────────────────────────────────────────────────── */
static int cmd_decode(const char *inltw, const char *outpgm)
{
    FILE *f = fopen(inltw, "rb");
    if (!f) { fprintf(stderr, "Cannot open %s\n", inltw); return 1; }
    fseek(f, 0, SEEK_END);
    long flen = ftell(f);
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc(flen);
    if (!buf) { fclose(f); return 1; }
    fread(buf, 1, flen, f);
    fclose(f);

    /* Peek at header for dimensions */
    if (flen < 7) { free(buf); return 1; }
    int w = ((int)buf[2] << 8) | buf[3];
    int h = ((int)buf[4] << 8) | buf[5];
    int N = buf[6];

    LTWState *s = ltw_state_alloc(w, h, N, buf[0]);
    if (!s) { free(buf); return 1; }

    uint8_t *pixels = (uint8_t *)malloc(w * h);
    if (!pixels) { ltw_state_free(s); free(buf); return 1; }

    int rc = ltw_decode(s, buf, (int)flen, pixels);
    if (rc == 0)
        pgm_write(outpgm, pixels, w, h);
    else
        fprintf(stderr, "Decode failed\n");

    free(pixels); ltw_state_free(s); free(buf);
    return rc;
}

/* ── main ─────────────────────────────────────────────────────────────── */
int main(int argc, char **argv)
{
    if (argc < 2) goto usage;

    if (strcmp(argv[1], "encode") == 0) {
        if (argc != 6) goto usage;
        return cmd_encode(argv[2], argv[3], atoi(argv[4]), atoi(argv[5]));
    }
    if (strcmp(argv[1], "decode") == 0) {
        if (argc != 4) goto usage;
        return cmd_decode(argv[2], argv[3]);
    }

usage:
    fprintf(stderr,
        "Usage:\n"
        "  ltw encode input.pgm output.ltw rplanes N\n"
        "  ltw decode input.ltw output.pgm\n");
    return 1;
}
