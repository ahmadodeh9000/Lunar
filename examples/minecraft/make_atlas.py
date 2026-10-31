#!/usr/bin/env python3
"""Generates atlas.png: a 4x4 grid of 16x16 tiles, using only the standard library.

Run from this directory: python3 make_atlas.py
"""
import random
import struct
import zlib

TILE = 16
GRID = 4
rng = random.Random(1234)


def speckle(base, spread):
    return lambda x, y: tuple(c + rng.randint(-spread, spread) for c in base) + (255,)


def grass_top(x, y):
    return speckle((95, 159, 53), 22)(x, y)


def dirt(x, y):
    return speckle((134, 96, 67), 18)(x, y)


def grass_side(x, y):
    edge = 3 + (x * 7 + 3) % 3
    return grass_top(x, y) if y < edge else dirt(x, y)


def stone(x, y):
    return speckle((125, 125, 125), 14)(x, y)


def log_side(x, y):
    shade = 16 if x % 4 == 1 else 0
    return speckle((104 - shade, 80 - shade, 48 - shade), 6)(x, y)


def log_top(x, y):
    d = max(abs(x - 7.5), abs(y - 7.5))
    if d > 6:
        return log_side(x, y)
    return speckle((176, 144, 92) if int(d) % 2 else (156, 124, 76), 6)(x, y)


def leaves(x, y):
    return speckle((56, 118, 40), 28)(x, y)


def sand(x, y):
    return speckle((219, 207, 163), 9)(x, y)


def planks(x, y):
    seam = y % 4 == 3 or (x + (y // 4) * 6) % 8 == 7
    return speckle((122, 94, 56) if seam else (172, 138, 86), 7)(x, y)


def brick(x, y):
    seam = y % 4 == 3 or (x + (y // 4 % 2) * 4) % 8 == 7
    return speckle((176, 170, 160) if seam else (152, 76, 62), 7)(x, y)


def highlight(x, y):
    on_edge = x == 0 or y == 0 or x == TILE - 1 or y == TILE - 1
    return (255, 255, 255, 230) if on_edge else (255, 255, 255, 34)


def empty(x, y):
    return (0, 0, 0, 0)


# The tile numbers here must match the TILE_ tables in minecraft.lunar.
TILES = [grass_top, grass_side, dirt, stone, log_side, log_top, leaves, sand,
         planks, brick, empty, empty, empty, empty, highlight, empty]


def chunk(kind, data):
    return (struct.pack(">I", len(data)) + kind + data +
            struct.pack(">I", zlib.crc32(kind + data)))


def main():
    size = TILE * GRID
    pixels = [[(0, 0, 0, 0)] * size for _ in range(size)]
    for n, tile in enumerate(TILES):
        ox, oy = (n % GRID) * TILE, (n // GRID) * TILE
        for y in range(TILE):
            for x in range(TILE):
                r, g, b, a = tile(x, y)
                pixels[oy + y][ox + x] = (max(0, min(255, r)), max(0, min(255, g)),
                                          max(0, min(255, b)), a)
    raw = b"".join(b"\0" + bytes(c for px in row for c in px) for row in pixels)
    with open("atlas.png", "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


if __name__ == "__main__":
    main()
