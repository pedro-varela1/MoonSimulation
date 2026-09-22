#!/usr/bin/env python3
"""
Convert a NASA CGI Moon Kit colour map into the exact BMP flavour that
bmpToTexture.cpp can read.

The loader is hand-rolled and strict: it only accepts an uncompressed 24-bit
BMP with a 40-byte BITMAPINFOHEADER (it hard-codes fseek(fp, 14 + 40)). The
NASA downloads are LZW-compressed TIFFs, so they must be re-encoded.

Rows are written in top-to-bottom image order. Because BMP pixel data is
stored bottom-up, that makes the north pole the first row in the file, which
is texture coordinate t=0 in OpenGL -- matching the orientation the elevation
map is uploaded with, so colour and relief stay aligned.

The output is written a chunk of rows at a time rather than flipping the whole
image in memory, which keeps peak RAM near the size of one decoded copy
instead of two. That matters for the full-resolution map, where each copy is
over a gigabyte.

The elevation maps (ldem_*.tif) need no conversion: they are already
uncompressed 32-bit float TIFFs, which tifReader.cpp reads directly.

Usage:
    python convert_assets.py           # the map main.cpp expects
    python convert_assets.py full      # 16-bit sRGB variant, same resolution
    python convert_assets.py 8k        # 8k map
"""

import os
import struct
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required:  pip install pillow")

try:
    import numpy as np
except ImportError:
    sys.exit("NumPy is required:  pip install numpy")

Image.MAX_IMAGE_PIXELS = None  # these maps are far past the decompression-bomb guard

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "assets")

PRESETS = {
    "full":  ("lroc_color_16bit_srgb.tif", "lroc_color_full_flipped.bmp", (27360, 13680)),
    "poles": ("lroc_color_poles.tif",      "lroc_color_poles_flipped.bmp", (27360, 13680)),
    "8k":    ("lroc_color_poles_8k.tif",   "lroc_color_poles_8k_flipped.bmp", (8192, 4096)),
}

ROWS_PER_CHUNK = 256


def write_bmp_header(out, width, height, row_bytes):
    image_bytes = row_bytes * height
    out.write(b"BM")
    out.write(struct.pack("<IHHI", 54 + image_bytes, 0, 0, 54))
    out.write(struct.pack("<IiiHHIIiiII",
                          40,            # biSize -- the 40-byte header the loader assumes
                          width,
                          height,        # positive: bottom-up
                          1,             # biPlanes
                          24,            # biBitCount
                          0,             # biCompression = BI_RGB
                          image_bytes,
                          0, 0, 0, 0))


def convert(preset):
    src_name, dst_name, expected = PRESETS[preset]
    src = os.path.join(ASSETS, src_name)
    dst = os.path.join(ASSETS, dst_name)

    if not os.path.isfile(src):
        sys.exit("Missing %s\nDownload it from the NASA CGI Moon Kit first." % src)

    img = Image.open(src)
    print("source : %s  %s %s" % (src_name, img.size, img.mode))

    if img.size != expected:
        sys.exit("Expected %s but got %s." % (expected, img.size))

    width, height = img.size
    row_bytes = width * 3
    if row_bytes % 4:
        sys.exit("Width %d needs BMP row padding; this writer assumes none." % width)

    if img.mode not in ("RGB", "RGBA"):
        img = img.convert("RGB")

    print("decoding (this is the slow part)...")
    img.load()

    with open(dst, "wb") as out:
        write_bmp_header(out, width, height, row_bytes)
        for y in range(0, height, ROWS_PER_CHUNK):
            y1 = min(y + ROWS_PER_CHUNK, height)
            chunk = np.asarray(img.crop((0, y, width, y1)))
            # 2::-1 takes channels 2,1,0 -- RGB(A) to BGR, dropping any alpha
            out.write(chunk[:, :, 2::-1].tobytes())
            pct = 100.0 * y1 / height
            print("\r  writing %5.1f%%" % pct, end="", flush=True)
    print()

    size = os.path.getsize(dst)
    want = row_bytes * height + 54
    print("output : %s" % dst)
    print("bytes  : %d (expected %d)" % (size, want))
    if size != want:
        sys.exit("Unexpected file size.")
    print("OK")


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "poles"
    if which not in PRESETS:
        sys.exit("Unknown preset %r. Choose from: %s" % (which, ", ".join(PRESETS)))
    convert(which)
