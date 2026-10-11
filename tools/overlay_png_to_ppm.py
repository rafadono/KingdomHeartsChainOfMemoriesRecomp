#!/usr/bin/env python3
"""Converts an 8-bit RGB/RGBA non-interlaced PNG (e.g. a capture-harness native frame) to a binary PPM (P6),
the background format of tools/overlay_dump.cpp.   Usage: overlay_png_to_ppm.py in.png out.ppm"""
import struct
import sys
import zlib


def main():
    data = open(sys.argv[1], 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'not a PNG'
    pos, idat, w = 8, b'', 0
    while pos < len(data):
        n, tag = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if tag == b'IHDR':
            w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', body)
            assert depth == 8 and ctype in (2, 6) and inter == 0, 'only 8-bit RGB/RGBA, non-interlaced'
            bpp = 3 if ctype == 2 else 4
        elif tag == b'IDAT':
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    prev = bytearray(stride)
    out = bytearray()
    p = 0
    for _ in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        prev = line
        for x in range(w):
            out += line[x * bpp:x * bpp + 3]
    with open(sys.argv[2], 'wb') as fh:
        fh.write(b'P6 %d %d 255\n' % (w, h))
        fh.write(out)


if __name__ == '__main__':
    main()
