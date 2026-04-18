#!/usr/bin/env bash
# make_test_image.sh — generate a synthetic 512×512 PGM for quick smoke-testing
# without needing the full dataset.  Writes data/synthetic.pgm.
#
# Produces a diagonal-gradient image: pixel(r,c) = (r+c) % 256.
# No external dependencies beyond printf/dd (POSIX).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/data/synthetic.pgm"
W=512; H=512

python3 - <<PYEOF
import struct, sys
w, h = $W, $H
header = f"P5\n{w} {h}\n255\n".encode()
pixels = bytes((r + c) & 0xFF for r in range(h) for c in range(w))
with open("$OUT", "wb") as f:
    f.write(header)
    f.write(pixels)
print(f"Written {len(pixels)} pixels → $OUT")
PYEOF
