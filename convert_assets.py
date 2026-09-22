#!/usr/bin/env python3
"""
Convert the NASA CGI Moon Kit color map into the exact BMP flavour that
bmpToTexture.cpp can read.

The loader is hand-rolled and strict: it only accepts an uncompressed
24-bit BMP with a 40-byte BITMAPINFOHEADER (it hard-codes fseek(fp, 14 + 40)).
The NASA download is an LZW-compressed TIFF, so it must be re-encoded.

The image is also flipped vertically. BMP pixel data is stored bottom-up,
so flipping first makes the north pole the first row in the file, which is
texture coordinate t=0 in OpenGL. That matches the orientation the elevation
map is uploaded with, keeping colour and relief aligned.

The elevation map (ldem_16.tif) needs no conversion: it is already an
uncompressed 32-bit float TIFF, which tifReader.cpp reads directly.

Usage:  python convert_assets.py
"""

import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required:  pip install pillow")

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "assets")

SRC = os.path.join(ASSETS, "lroc_color_poles_8k.tif")
DST = os.path.join(ASSETS, "lroc_color_poles_8k_flipped.bmp")

EXPECTED_SIZE = (8192, 4096)


def main():
    if not os.path.isfile(SRC):
        sys.exit("Missing %s\nDownload it from the NASA CGI Moon Kit first." % SRC)

    elevation = os.path.join(ASSETS, "ldem_16.tif")
    if not os.path.isfile(elevation):
        print("Warning: %s not found; the program needs it too." % elevation)

    img = Image.open(SRC)
    print("source : %s %s" % (img.size, img.mode))

    if img.size != EXPECTED_SIZE:
        sys.exit("Expected %s but got %s. main.cpp is wired to the 8k map."
                 % (EXPECTED_SIZE, img.size))

    if img.mode != "RGB":
        img = img.convert("RGB")

    img = img.transpose(Image.FLIP_TOP_BOTTOM)
    img.save(DST, format="BMP")

    size = os.path.getsize(DST)
    expected = EXPECTED_SIZE[0] * EXPECTED_SIZE[1] * 3 + 54
    print("output : %s" % DST)
    print("bytes  : %d (expected %d)" % (size, expected))

    if size != expected:
        sys.exit("Unexpected file size -- the BMP header is probably not the "
                 "40-byte form the loader assumes.")
    print("OK")


if __name__ == "__main__":
    main()
