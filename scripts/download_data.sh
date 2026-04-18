#!/usr/bin/env bash
# download_data.sh — download and prepare standard codec test images.
# Requires: curl, ImageMagick (convert).
set -euo pipefail

DATADIR="$(cd "$(dirname "$0")/.." && pwd)/data"
mkdir -p "$DATADIR"

die() { echo "ERROR: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "'$1' not found. Install it first."; }

need curl
need convert   # ImageMagick

# ── USC SIPI Miscellaneous volume ────────────────────────────────────────
# Free for research: https://sipi.usc.edu/database/database.php?volume=misc
SIPI_BASE="https://sipi.usc.edu/database/download.php?vol=misc&img="
declare -A SIPI_IMAGES=(
    ["lena"]="4.2.06"
    ["baboon"]="4.2.03"
    ["airplane"]="4.2.05"
    ["peppers"]="4.2.07"
    ["elaine"]="4.2.01"
    ["house"]="4.1.05"
    ["tree"]="4.1.06"
    ["girl"]="4.1.01"
)

echo "Downloading USC SIPI Miscellaneous images..."
for name in "${!SIPI_IMAGES[@]}"; do
    id="${SIPI_IMAGES[$name]}"
    tiff="$DATADIR/${name}.tiff"
    pgm="$DATADIR/${name}.pgm"
    if [ -f "$pgm" ]; then
        echo "  ✓ $pgm already exists, skipping."
        continue
    fi
    echo "  Downloading $name ($id)..."
    curl -fsSL "${SIPI_BASE}${id}" -o "$tiff" \
        || { echo "  WARNING: failed to download $name, skipping."; continue; }
    convert "$tiff" -depth 8 -type Grayscale "$pgm"
    rm -f "$tiff"
    echo "  → $pgm"
done

# ── Kodak image suite ─────────────────────────────────────────────────────
# 24 images, 768x512 or 512x768 colour PNG.
# We convert to 512x512 grayscale (centre-crop).
KODAK_BASE="https://r0k.us/graphics/kodak/kodak"
echo ""
echo "Downloading Kodak images (1–24)..."
for i in $(seq -w 1 24); do
    png="$DATADIR/kodim${i}.png"
    pgm="$DATADIR/kodim${i}.pgm"
    if [ -f "$pgm" ]; then
        echo "  ✓ $pgm already exists, skipping."
        continue
    fi
    url="${KODAK_BASE}/kodim${i}.png"
    echo "  Downloading kodim${i}..."
    curl -fsSL "$url" -o "$png" \
        || { echo "  WARNING: failed to download kodim${i}, skipping."; continue; }
    # centre-crop to 512x512 and convert to grayscale
    convert "$png" -gravity Center -extent 512x512 \
            -colorspace Gray -depth 8 "$pgm"
    rm -f "$png"
    echo "  → $pgm"
done

echo ""
echo "Done. Images written to $DATADIR/"
ls "$DATADIR/"*.pgm 2>/dev/null | wc -l | xargs -I{} echo "  {} PGM files ready."
