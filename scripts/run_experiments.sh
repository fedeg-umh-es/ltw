#!/usr/bin/env bash
# run_experiments.sh — encode/decode all test images at several rplanes values,
# then call compute_psnr.py to build the rate-distortion table (Table I).
#
# Usage:
#   bash scripts/run_experiments.sh            # uses data/*.pgm
#   bash scripts/run_experiments.sh data/lena.pgm  # single image
#
# Outputs:
#   results/<image>_rp<N>.ltw   — compressed bitstream
#   results/<image>_rp<N>.pgm   — reconstructed image
#   results/rd_table.csv        — rate-distortion summary
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/ltw"
RESULTS="$ROOT/results"
DATA="$ROOT/data"
SCRIPTS="$ROOT/scripts"

[ -x "$BIN" ] || { echo "Binary '$BIN' not found. Run 'make' first." >&2; exit 1; }
mkdir -p "$RESULTS"

# DWT levels — paper uses N=4 for 512×512 images
N=4
# rplanes values to sweep (0 = lossless-approximation, higher = more lossy)
RPLANES_LIST="0 1 2 3 4"

# Collect images
if [ "$#" -gt 0 ]; then
    IMAGES=("$@")
else
    mapfile -t IMAGES < <(ls "$DATA"/*.pgm 2>/dev/null)
fi

if [ "${#IMAGES[@]}" -eq 0 ]; then
    echo "No PGM images found in $DATA/"
    echo "Run 'bash scripts/download_data.sh' first, or provide image paths as arguments."
    exit 1
fi

CSV="$RESULTS/rd_table.csv"
echo "image,rplanes,original_bytes,compressed_bytes,bpp,psnr_db" > "$CSV"

for img in "${IMAGES[@]}"; do
    name="$(basename "$img" .pgm)"
    echo "── $name ────────────────────────────────"
    for rp in $RPLANES_LIST; do
        stem="${name}_rp${rp}"
        ltw_out="$RESULTS/${stem}.ltw"
        rec_out="$RESULTS/${stem}.pgm"

        # Encode
        "$BIN" encode "$img" "$ltw_out" "$rp" "$N" 2>/dev/null

        # Decode
        "$BIN" decode "$ltw_out" "$rec_out" 2>/dev/null

        # Metrics
        python3 "$SCRIPTS/compute_psnr.py" \
            --original "$img" \
            --reconstructed "$rec_out" \
            --compressed "$ltw_out" \
            --csv-row "$name,$rp" \
            >> "$CSV"
    done
done

echo ""
echo "Results written to $RESULTS/"
echo "Rate-distortion table: $CSV"
echo ""
python3 "$SCRIPTS/compute_psnr.py" --print-table "$CSV"
