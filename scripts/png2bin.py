"""Convert a PNG into the 1-bit-per-pixel format display.bitmap() expects.

    python scripts/png2bin.py sprite.png sprite.bin [--threshold 128] [--invert]

Prints the width and height to pass to display.bitmap(). Standard library
only -- no Pillow -- so it runs anywhere the rest of the tooling does.

Output layout: rows top to bottom, each row padded to a whole number of bytes,
most significant bit leftmost. A set bit is ink. That is exactly what
Adafruit_GFX drawBitmap reads, which is why there is no header: the dimensions
travel in the display.bitmap() call.

Supported PNGs: 8-bit greyscale, RGB and RGBA, non-interlaced. That covers what
image editors produce by default; anything else, re-export.
"""

import struct
import sys
import zlib


CHANNELS = {0: 1, 2: 3, 4: 2, 6: 4}  # colour type -> samples per pixel


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def _undo_filters(raw, width, height, bpp):
    """Reverse the per-row PNG filters into a flat sample buffer."""
    stride = width * bpp
    out = bytearray()
    previous = bytearray(stride)

    offset = 0
    for _ in range(height):
        filter_type = raw[offset]
        offset += 1
        line = bytearray(raw[offset:offset + stride])
        offset += stride

        for i in range(stride):
            left = line[i - bpp] if i >= bpp else 0
            up = previous[i]
            up_left = previous[i - bpp] if i >= bpp else 0

            if filter_type == 0:
                pass
            elif filter_type == 1:
                line[i] = (line[i] + left) & 0xFF
            elif filter_type == 2:
                line[i] = (line[i] + up) & 0xFF
            elif filter_type == 3:
                line[i] = (line[i] + (left + up) // 2) & 0xFF
            elif filter_type == 4:
                line[i] = (line[i] + _paeth(left, up, up_left)) & 0xFF
            else:
                raise ValueError("unsupported PNG filter %d" % filter_type)

        out.extend(line)
        previous = line

    return out


def read_png(path):
    """Return (width, height, luminance bytes) for a supported PNG."""
    with open(path, "rb") as handle:
        blob = handle.read()

    if blob[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s is not a PNG" % path)

    offset = 8
    width = height = depth = colour = 0
    compressed = bytearray()

    while offset < len(blob):
        (length,) = struct.unpack(">I", blob[offset:offset + 4])
        tag = blob[offset + 4:offset + 8]
        payload = blob[offset + 8:offset + 8 + length]
        offset += 12 + length

        if tag == b"IHDR":
            width, height, depth, colour, _comp, _filt, interlace = struct.unpack(
                ">IIBBBBB", payload[:13])
            if depth != 8:
                raise ValueError("only 8-bit PNGs are supported (got %d)" % depth)
            if colour not in CHANNELS:
                raise ValueError("unsupported colour type %d" % colour)
            if interlace:
                raise ValueError("interlaced PNGs are not supported")
        elif tag == b"IDAT":
            compressed.extend(payload)
        elif tag == b"IEND":
            break

    bpp = CHANNELS[colour]
    samples = _undo_filters(zlib.decompress(bytes(compressed)), width, height, bpp)

    luminance = bytearray(width * height)
    for index in range(width * height):
        base = index * bpp
        if colour in (0, 4):  # grey, grey+alpha
            luminance[index] = samples[base]
        else:                 # rgb, rgba
            r, g, b = samples[base], samples[base + 1], samples[base + 2]
            luminance[index] = (r * 299 + g * 587 + b * 114) // 1000

    return width, height, luminance


def pack(width, height, luminance, threshold=128, invert=False):
    """Pack luminance into 1bpp rows, MSB leftmost. Dark pixels become ink."""
    byte_width = (width + 7) // 8
    out = bytearray(byte_width * height)

    for y in range(height):
        for x in range(width):
            dark = luminance[y * width + x] < threshold
            if invert:
                dark = not dark
            if dark:
                out[y * byte_width + (x >> 3)] |= 0x80 >> (x & 7)

    return bytes(out)


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip())
        return 2

    source, target = argv[0], argv[1]
    threshold = 128
    invert = "--invert" in argv
    if "--threshold" in argv:
        threshold = int(argv[argv.index("--threshold") + 1])

    width, height, luminance = read_png(source)
    data = pack(width, height, luminance, threshold, invert)

    with open(target, "wb") as handle:
        handle.write(data)

    print("%s: %dx%d, %d bytes" % (target, width, height, len(data)))
    print("  display.bitmap(x, y, %d, %d, fs.read(\"%s\"))"
          % (width, height, target.replace("\\", "/").split("/")[-1]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
