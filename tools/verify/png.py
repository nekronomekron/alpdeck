"""Minimal PNG writer, standard library only.

Pillow is not installed in PlatformIO's penv and pulling it in would make the
verification harness unrunnable wherever it is missing. A greyscale PNG is a
handful of chunks, so it is cheaper to emit one directly than to own a
dependency.
"""

import struct
import zlib


def _chunk(tag, payload):
    body = tag + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))


def write_gray_png(path, width, height, pixels, scale=1):
    """Write an 8-bit greyscale PNG.

    ``pixels`` is a row-major buffer of length ``width * height`` holding 0..255
    intensities. ``scale`` repeats each pixel, which makes 400x300 goldens
    readable when opened at 100%.
    """
    if len(pixels) != width * height:
        raise ValueError(
            "pixel buffer is %d bytes, expected %d" % (len(pixels), width * height)
        )

    raw = bytearray()
    for y in range(height):
        row = pixels[y * width : (y + 1) * width]
        if scale != 1:
            scaled = bytearray()
            for value in row:
                scaled.extend(bytes([value]) * scale)
            row = scaled
        for _ in range(scale):
            raw.append(0)  # filter type 0 (None)
            raw.extend(row)

    header = struct.pack(">IIBBBBB", width * scale, height * scale, 8, 0, 0, 0, 0)
    data = (
        b"\x89PNG\r\n\x1a\n"
        + _chunk(b"IHDR", header)
        + _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + _chunk(b"IEND", b"")
    )

    with open(path, "wb") as handle:
        handle.write(data)


def read_gray_png(path):
    """Read back a PNG written by :func:`write_gray_png`.

    Only the narrow subset this module produces is supported -- that is all the
    golden comparison needs, and a general decoder would be dead weight.
    """
    with open(path, "rb") as handle:
        blob = handle.read()

    if blob[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s is not a PNG" % path)

    offset = 8
    width = height = 0
    compressed = bytearray()
    while offset < len(blob):
        (length,) = struct.unpack(">I", blob[offset : offset + 4])
        tag = blob[offset + 4 : offset + 8]
        payload = blob[offset + 8 : offset + 8 + length]
        offset += 12 + length

        if tag == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", payload[:10])
            if depth != 8 or colour != 0:
                raise ValueError("%s is not 8-bit greyscale" % path)
        elif tag == b"IDAT":
            compressed.extend(payload)
        elif tag == b"IEND":
            break

    raw = zlib.decompress(bytes(compressed))
    stride = width + 1
    pixels = bytearray()
    for y in range(height):
        if raw[y * stride] != 0:
            raise ValueError("%s uses a PNG row filter this reader does not support" % path)
        pixels.extend(raw[y * stride + 1 : (y + 1) * stride])
    return width, height, pixels
