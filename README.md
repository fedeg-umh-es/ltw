# LTW Codec — Lower-Tree Wavelet image compression

A from-scratch C99 implementation of the **Lower-Tree Wavelet (LTW)** image
codec, as described in:

> Oliver, J., & Malumbres, M. (2006).  
> *Low-Complexity Multiresolution Image Compression Using Wavelet Lower Trees.*  
> **IEEE Transactions on Circuits and Systems for Video Technology**, 16(11), 1437–1448.  
> DOI: [10.1109/TCSVT.2006.883505](https://doi.org/10.1109/TCSVT.2006.883505)

---

## Objective

Provide a clean, self-contained reference implementation that:

1. Reproduces the symbol-labeling example from Fig. 2–3 of the paper exactly.
2. Can encode and decode real PGM images end-to-end.
3. Serves as a readable starting point for FPGA or embedded implementations
   (no recursion deeper than N levels, no dynamic containers, integer-only DWT).

---

## Algorithm description

LTW exploits the spatial correlation of insignificant wavelet coefficients by
grouping them into **lower trees** — subtrees in the DWT hierarchy where every
node is below the coding threshold — and encoding the entire subtree with a
single symbol.

The encoder runs three passes over the DWT coefficient array:

| Pass | Name | Direction | Action |
|------|------|-----------|--------|
| **E1** | Initialisation | — | Forward DWT; compute `maxplane`; init arithmetic coder |
| **E2** | Symbol labeling | leaves → root | Assign one of four labels to every coefficient |
| **E3** | Coefficient output | root → leaves | Arithmetic-code each non-`LOWER_COMPONENT` symbol; raw-encode mantissa and sign bits |

**The four labels** (§ III of the paper):

| Label | Value | Meaning |
|-------|-------|---------|
| `LOWER_COMPONENT` | 0 | Part of a lower-tree; never transmitted |
| `LOWER` | 1 | Root of a lower-tree (insignificant, all children `LOWER_COMPONENT`) |
| `ISOLATED_LOWER` | 2 | Insignificant, but has ≥ 1 significant descendant |
| `SIGNIFICANT` | 3 | `\|c\|` ≥ 2^`rplanes` |

**DWT:** Daubechies 9/7 lifting scheme (Cohen–Daubechies–Feauveau), Q12
fixed-point integer arithmetic, in-place, N dyadic levels.

**Entropy coder:** adaptive range coder (32-bit), 2 contexts (based on
left/upper neighbour significance), no external libraries.

---

## Requirements

| Component | Minimum version | Notes |
|-----------|----------------|-------|
| C compiler | C99 (`-std=c99`) | GCC ≥ 4.8, Clang ≥ 3.4, MSVC ≥ 19.28 |
| Make | GNU Make ≥ 3.81 | BSD Make also works |
| libm | system | `-lm`; already present on all POSIX systems |
| Python 3 | ≥ 3.8 | **Optional** — only for metrics scripts |
| ImageMagick | ≥ 6 | **Optional** — only for `download_data.sh` |
| curl | any | **Optional** — only for `download_data.sh` |

No third-party C libraries are required.

---

## Dependencies

```
stdlib.h  stdio.h  string.h  math.h   ← C standard library only
```

Python scripts use only the standard library (`math`, `os`, `struct`, `csv`,
`argparse`). No `numpy`, `Pillow`, or similar packages.

---

## Installation

```bash
git clone https://github.com/<your-username>/ltw-codec.git
cd ltw-codec
make
```

This produces the `ltw` binary in the project root.

**Verify the build** by running the paper's 8 × 8 example:

```bash
make test
```

Expected output:
```
Label grid (C=LOWER_COMPONENT, L=LOWER, I=ISOLATED_LOWER, S=SIGNIFICANT):
S S S L S S C C
S S S S L L C C
S L C C C C S L
S I C C C C L L
S L C C C C C C
L L C C C C C C
C C L L C C C C
C C L S C C C C
All assertions passed.
```

---

## Usage

```bash
# Encode
./ltw encode input.pgm output.ltw <rplanes> <N>

# Decode
./ltw decode input.ltw output.pgm
```

**Parameters:**

| Parameter | Typical range | Effect |
|-----------|--------------|--------|
| `rplanes` | 0 – 5 | Least-significant bit planes discarded; higher = more lossy |
| `N` | 3 – 5 | DWT decomposition levels; use `N=4` for 512 × 512 images |

**Quick smoke-test on a synthetic image** (no dataset download needed):

```bash
bash scripts/make_test_image.sh     # creates data/synthetic.pgm
./ltw encode data/synthetic.pgm results/synthetic.ltw 2 4
./ltw decode results/synthetic.ltw  results/synthetic_rec.pgm
python3 scripts/compute_psnr.py \
    --original data/synthetic.pgm \
    --reconstructed results/synthetic_rec.pgm
```

---

## Reproducing experiments

### Step 1 — Get the test images

```bash
bash scripts/download_data.sh
```

Downloads and converts images from the [USC SIPI Miscellaneous
volume](https://sipi.usc.edu/database/) and the [Kodak
suite](https://r0k.us/graphics/kodak/) into `data/`.  See
[data/README.md](data/README.md) for manual download instructions and the exact
expected format.

### Step 2 — Run all codec experiments

```bash
bash scripts/run_experiments.sh
```

Encodes every `data/*.pgm` at `rplanes` ∈ {0, 1, 2, 3, 4} with `N=4`, decodes
the result, and writes:

```
results/<image>_rp<rplanes>.ltw   ← compressed bitstream
results/<image>_rp<rplanes>.pgm   ← reconstructed image
results/rd_table.csv              ← rate-distortion summary
```

To run on a single image:

```bash
bash scripts/run_experiments.sh data/lena.pgm
```

### Step 3 — Print the rate-distortion table (Table I equivalent)

```bash
python3 scripts/compute_psnr.py --print-table results/rd_table.csv
```

Sample output:

```
Image          rplanes      BPP  PSNR (dB)
----------------------------------------------
lena                 0   4.8231     inf
lena                 1   3.1027   49.2144
lena                 2   1.9853   42.6781
lena                 3   1.1024   36.1093
lena                 4   0.5812   30.4417

baboon               0   5.0118     inf
...
```

---

## Repository structure

```
ltw-codec/
├── include/
│   ├── ltw.h            — types, symbol constants, public API
│   ├── arith_coder.h    — adaptive range-coder interface
│   └── dwt.h            — Daubechies 9/7 DWT interface
├── src/
│   ├── ltw_encoder.c    — passes E1, E2, E3  (§ III of the paper)
│   ├── ltw_decoder.c    — single-pass decoder
│   ├── dwt.c            — integer + float lifting DWT, in-place
│   ├── arith_coder.c    — range coder, 2 adaptive models
│   └── main.c           — PGM I/O, CLI
├── test/
│   └── test_8x8.c       — Fig. 2/3 paper example, zero external deps
├── scripts/
│   ├── download_data.sh    — fetch USC SIPI + Kodak images
│   ├── run_experiments.sh  — full encode/decode sweep
│   ├── compute_psnr.py     — PSNR + bitrate metrics (stdlib only)
│   └── make_test_image.sh  — generate synthetic PGM (no download needed)
├── data/
│   └── README.md        — dataset instructions, format spec, links
├── results/             — created by run_experiments.sh (git-ignored)
├── Makefile
├── LICENSE              — MIT
└── README.md            — this file
```

---

## Regenerating main tables and figures

| Paper element | How to reproduce |
|---------------|-----------------|
| **Fig. 3** symbol map (8×8 example) | `make test` |
| **Table I** rate-distortion results | `bash scripts/run_experiments.sh` then `python3 scripts/compute_psnr.py --print-table results/rd_table.csv` |
| Individual encode/decode | `./ltw encode <in.pgm> <out.ltw> <rplanes> <N>` + `./ltw decode <out.ltw> <rec.pgm>` |

---

## Notes

- **Integer DWT:** Q12 fixed-point arithmetic throughout; no floating-point at
  encode/decode runtime (float reference version in `dwt.c` is for testing only).
- **No global mutable state:** all codec state lives in `LTWState *` passed by
  pointer.
- **FPGA-friendly:** maximum recursion depth equals N (typically 4); no
  dynamically-sized lists.
- Comments in the source cite the relevant paper section or equation, e.g.:
  `// Algorithm 1, E2 stage, Oliver & Malumbres 2006`.

---

## Citation

If you use this code, please cite the software repository and the related article.

**Software repository**  
Federico García Crespí et al. *LTW*. GitHub repository. [doi:10.5281/zenodo.19642457](https://doi.org/10.5281/zenodo.19642457)

**Related manuscript**  
[Autores]. “[TÍTULO DEL PAPER].” Submitted manuscript.

---

## License

MIT — see [LICENSE](LICENSE).
