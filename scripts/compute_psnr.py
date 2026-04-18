#!/usr/bin/env python3
"""compute_psnr.py — PSNR and bitrate metrics for the LTW codec.

Modes
-----
  # Emit one CSV row (called by run_experiments.sh per image/rplanes pair):
  python3 scripts/compute_psnr.py \
      --original data/lena.pgm \
      --reconstructed results/lena_rp2.pgm \
      --compressed  results/lena_rp2.ltw \
      --csv-row "lena,2"

  # Print a formatted table from an existing CSV:
  python3 scripts/compute_psnr.py --print-table results/rd_table.csv

  # Quick standalone check (two PGM files):
  python3 scripts/compute_psnr.py --original a.pgm --reconstructed b.pgm
"""

import argparse
import math
import os
import struct
import sys


# ── Minimal PGM reader (no external libs) ───────────────────────────────────

def read_pgm(path):
    """Return (pixels_bytes, width, height) for a P5 PGM file."""
    with open(path, "rb") as f:
        def next_token():
            tok = b""
            while True:
                b = f.read(1)
                if not b:
                    return tok
                if b == b"#":
                    while f.read(1) not in (b"\n", b""):
                        pass
                    continue
                if b in (b" ", b"\t", b"\n", b"\r"):
                    if tok:
                        return tok
                else:
                    tok += b

        magic = next_token().decode()
        if magic != "P5":
            raise ValueError(f"Not a P5 PGM: {path}")
        width  = int(next_token())
        height = int(next_token())
        _maxval = int(next_token())
        pixels = f.read(width * height)
    return pixels, width, height


def psnr(original, reconstructed):
    """PSNR in dB between two equal-length byte sequences (8-bit images)."""
    assert len(original) == len(reconstructed), "Size mismatch"
    mse = sum((a - b) ** 2 for a, b in zip(original, reconstructed)) / len(original)
    if mse == 0:
        return float("inf")
    return 10.0 * math.log10(255.0 ** 2 / mse)


# ── CSV row mode ─────────────────────────────────────────────────────────────

def emit_csv_row(args):
    orig_pix, w, h = read_pgm(args.original)
    rec_pix,  _, _ = read_pgm(args.reconstructed)
    n_pixels = w * h

    orig_bytes = os.path.getsize(args.original)
    comp_bytes = os.path.getsize(args.compressed) if args.compressed else 0
    bpp = (comp_bytes * 8) / n_pixels if n_pixels else 0.0

    db = psnr(orig_pix, rec_pix)
    db_str = f"{db:.4f}" if db != float("inf") else "inf"

    prefix = args.csv_row + "," if args.csv_row else ""
    print(f"{prefix}{orig_bytes},{comp_bytes},{bpp:.6f},{db_str}")


# ── Table print mode ─────────────────────────────────────────────────────────

def print_table(csv_path):
    import csv as csv_mod
    rows = []
    with open(csv_path) as f:
        reader = csv_mod.DictReader(f)
        for row in reader:
            rows.append(row)

    if not rows:
        print("(empty table)")
        return

    # Group by image
    images = {}
    for row in rows:
        img = row.get("image", "?")
        images.setdefault(img, []).append(row)

    header = f"{'Image':<14} {'rplanes':>7} {'BPP':>8} {'PSNR (dB)':>10}"
    print(header)
    print("-" * len(header))
    for img, img_rows in sorted(images.items()):
        for row in sorted(img_rows, key=lambda r: int(r.get("rplanes", 0))):
            rp   = row.get("rplanes", "?")
            bpp  = float(row.get("bpp", 0))
            db   = row.get("psnr_db", "?")
            print(f"{img:<14} {rp:>7} {bpp:>8.4f} {db:>10}")
        print()


# ── Standalone PSNR mode ─────────────────────────────────────────────────────

def standalone(args):
    orig_pix, w, h = read_pgm(args.original)
    rec_pix,  _, _ = read_pgm(args.reconstructed)
    db = psnr(orig_pix, rec_pix)
    print(f"Image: {w}×{h}  PSNR: {db:.4f} dB")


# ── CLI ───────────────────────────────────────────────────────────────────────

def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--original",       help="Original PGM file")
    p.add_argument("--reconstructed",  help="Decoded PGM file")
    p.add_argument("--compressed",     help="Compressed .ltw file (for size)")
    p.add_argument("--csv-row",        dest="csv_row",
                   help="Prefix string prepended to CSV output (e.g. 'lena,2')")
    p.add_argument("--print-table",    dest="print_table", metavar="CSV",
                   help="Print formatted table from an existing CSV file")
    args = p.parse_args()

    if args.print_table:
        print_table(args.print_table)
        return

    if not args.original or not args.reconstructed:
        p.print_help()
        sys.exit(1)

    if args.csv_row is not None or args.compressed:
        emit_csv_row(args)
    else:
        standalone(args)


if __name__ == "__main__":
    main()
