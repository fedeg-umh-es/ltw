# Test datasets

The codec is evaluated on two standard, freely available image sets used in the
original paper and throughout the image-compression literature.

---

## 1 · USC SIPI Miscellaneous volume (512 × 512, 8-bit grayscale)

**License:** free for research and educational use  
**URL:** <https://sipi.usc.edu/database/database.php?volume=misc>

Images used in the paper (Table I):

| Filename (SIPI)   | Common name | SIPI ID |
|-------------------|-------------|---------|
| 4.1.01.tiff       | Girl        | 4.1.01  |
| 4.1.04.tiff       | Girl 2      | 4.1.04  |
| 4.1.05.tiff       | House       | 4.1.05  |
| 4.1.06.tiff       | Tree        | 4.1.06  |
| 4.2.01.tiff       | Elaine      | 4.2.01  |
| 4.2.03.tiff       | Baboon      | 4.2.03  |
| 4.2.05.tiff       | Airplane    | 4.2.05  |
| 4.2.06.tiff       | Lena        | 4.2.06  |
| 4.2.07.tiff       | Peppers     | 4.2.07  |

### Download and convert to P5 PGM

```bash
bash scripts/download_data.sh
```

The script downloads TIFF files from USC SIPI, converts them to 8-bit P5 PGM
using ImageMagick (`convert`), and places them in `data/`.

**Manual alternative** — download from the SIPI website and convert:

```bash
convert 4.2.06.tiff -depth 8 -type Grayscale data/lena.pgm
```

---

## 2 · Kodak Lossless True Color Image Suite

**License:** free for non-commercial research  
**URL:** <https://r0k.us/graphics/kodak/>

24 images (768 × 512 or 512 × 768, colour).  
For this codec (grayscale) convert to luminance:

```bash
convert kodim01.png -colorspace Gray -depth 8 data/kodim01.pgm
```

`scripts/download_data.sh` handles this automatically.

---

## Expected format

All images consumed by `./ltw encode` must be:

- **Format:** P5 (binary PGM), single-channel 8-bit  
- **Dimensions:** width and height both multiples of `2^N`  
  (e.g., 512 × 512 for `N=4`; 256 × 256 for `N=3`)  
- **Max value:** 255  

Header example:
```
P5
512 512
255
<binary pixel data: 512×512 bytes>
```

If an image has dimensions that are not a power-of-two multiple, crop or pad
before encoding:

```bash
convert input.png -gravity NorthWest -extent 512x512 \
        -depth 8 -type Grayscale data/image.pgm
```

---

## Directory layout after download

```
data/
├── README.md          ← this file
├── lena.pgm
├── baboon.pgm
├── airplane.pgm
├── peppers.pgm
├── elaine.pgm
├── house.pgm
├── kodim01.pgm
├── kodim02.pgm
└── ...
```

All `.pgm` files are excluded from version control (see `.gitignore`).
