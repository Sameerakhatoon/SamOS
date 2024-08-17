#!/usr/bin/env python3
"""Read the text on a SamOS screen from a QEMU `screendump` (PPM).

The terminal draws 8x16 glyphs from assets/sysfont.bmp on a grid
starting at (0,0). For every cell we take the "ink" pixels (those
different from the cell grid's background colour) and look the
pattern up among the font's glyphs. Cells that match nothing come
out as '?', blank cells as ' '. Prints one line per text row with
trailing spaces stripped.

Usage: screen_text.py SCREEN.ppm FONT.bmp
"""
import struct
import sys

CELL_W, CELL_H, FIRST = 8, 16, 32


def read_ppm(path):
    data = open(path, "rb").read()
    fields = []
    pos = 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(data[start:pos])
    pos += 1
    if fields[0] != b"P6":
        sys.exit("not a P6 ppm")
    w, h = int(fields[1]), int(fields[2])
    px = data[pos:pos + w * h * 3]
    return w, h, px


def read_font(path):
    d = open(path, "rb").read()
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0]
    if bpp != 24:
        sys.exit("font must be 24-bit")
    row = (w * 3 + 3) & ~3
    bottom_up = h > 0
    h = abs(h)

    def lit(x, y):
        yy = (h - 1 - y) if bottom_up else y
        b = d[off + yy * row + x * 3: off + yy * row + x * 3 + 3]
        return any(b)

    glyphs = {}
    cols = w // CELL_W
    for i in range((h // CELL_H) * cols):
        cx, cy = (i % cols) * CELL_W, (i // cols) * CELL_H
        bits = tuple(lit(cx + x, cy + y) for y in range(CELL_H) for x in range(CELL_W))
        ch = chr(FIRST + i)
        if any(bits) and bits not in glyphs:
            glyphs[bits] = ch
    return glyphs


def main():
    w, h, px = read_ppm(sys.argv[1])
    glyphs = read_font(sys.argv[2])
    # The most common colour, not pixel (0,0): the mouse cursor
    # sits at the top left until the mouse moves.
    counts = {}
    for i in range(0, len(px) - 2, 3 * 7):
        k = px[i:i + 3]
        counts[k] = counts.get(k, 0) + 1
    bg = max(counts, key=counts.get)

    def ink(x, y):
        i = (y * w + x) * 3
        return px[i:i + 3] != bg

    for row in range(h // CELL_H):
        line = []
        for col in range(w // CELL_W):
            bits = tuple(ink(col * CELL_W + x, row * CELL_H + y)
                         for y in range(CELL_H) for x in range(CELL_W))
            if not any(bits):
                line.append(" ")
            else:
                line.append(glyphs.get(bits, "?"))
        print("".join(line).rstrip())


if __name__ == "__main__":
    main()
