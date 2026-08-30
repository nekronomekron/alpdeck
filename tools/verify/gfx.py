"""Adafruit_GFX raster, reimplemented for off-device rendering.

Every primitive here follows the algorithm in Adafruit_GFX.cpp line for line --
Bresenham line, midpoint circle, the scanline triangle fill, the classic-font
glyph loop -- because the point of the harness is to see what the panel would
see. An "obviously equivalent" shortcut would render pixels the device never
draws and the goldens would quietly stop meaning anything.

The glyph data is not copied in: it is parsed out of the real glcdfont.c that
PlatformIO downloaded, so the text in a golden cannot drift from the text on
the device.

KNOWN LIMIT: this is a reimplementation, not the library. It tracks
Adafruit_GFX as of the version in .pio/libdeps. If that library updates its
rasterisation, goldens can shift without any change to alpdeck.
"""

import glob
import os
import re

BLACK = 0
WHITE = 1

_PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def _cdiv(numerator, denominator):
    """C integer division: truncates toward zero, unlike Python's floor."""
    quotient = abs(numerator) // abs(denominator)
    return quotient if (numerator < 0) == (denominator < 0) else -quotient


def _find_glcdfont():
    pattern = os.path.join(_PROJECT_ROOT, ".pio", "libdeps", "*", "Adafruit GFX Library", "glcdfont.c")
    matches = sorted(glob.glob(pattern))
    if not matches:
        raise RuntimeError(
            "glcdfont.c not found under .pio/libdeps. Run `pio run -e Alpdeck` once so "
            "PlatformIO fetches Adafruit GFX, then re-run the harness."
        )
    return matches[0]


def load_font():
    """Return the 5x7 classic font as a flat list of 5-byte column groups."""
    with open(_find_glcdfont(), "r", encoding="utf-8", errors="replace") as handle:
        source = handle.read()

    body = source[source.index("{") + 1 : source.rindex("}")]
    values = [int(token, 16) for token in re.findall(r"0[xX][0-9a-fA-F]{1,2}", body)]
    if len(values) < 256 * 5:
        raise RuntimeError("glcdfont.c yielded %d bytes, expected 1280" % len(values))
    return values


class Canvas:
    """A 1-bit framebuffer with the Adafruit_GFX drawing surface."""

    def __init__(self, width, height, font=None):
        self.width = width
        self.height = height
        self.buffer = bytearray([WHITE]) * (width * height)
        self._font = font if font is not None else load_font()

        self.cursor_x = 0
        self.cursor_y = 0
        self.text_size_x = 1
        self.text_size_y = 1
        self.text_color = BLACK
        self.text_bg = BLACK  # setTextColor(c) sets both -> transparent
        self.wrap = True
        self.gfx_font = None  # None = the built-in 6x8 face

        # Clip window, used by the e-paper partial-window emulation.
        self.clip = (0, 0, width, height)

    # ------------------------------------------------------------- pixel level

    def set_clip(self, x, y, width, height):
        self.clip = (x, y, width, height)

    def draw_pixel(self, x, y, color):
        cx, cy, cw, ch = self.clip
        if not (cx <= x < cx + cw and cy <= y < cy + ch):
            return
        if not (0 <= x < self.width and 0 <= y < self.height):
            return
        self.buffer[y * self.width + x] = color

    def fill_screen(self, color):
        self.fill_rect(0, 0, self.width, self.height, color)

    def draw_fast_hline(self, x, y, w, color):
        if w < 0:
            x += w + 1
            w = -w
        for offset in range(w):
            self.draw_pixel(x + offset, y, color)

    def draw_fast_vline(self, x, y, h, color):
        if h < 0:
            y += h + 1
            h = -h
        for offset in range(h):
            self.draw_pixel(x, y + offset, color)

    def fill_rect(self, x, y, w, h, color):
        for row in range(h):
            self.draw_fast_hline(x, y + row, w, color)

    # ------------------------------------------------------------------ shapes

    def draw_line(self, x0, y0, x1, y1, color):
        steep = abs(y1 - y0) > abs(x1 - x0)
        if steep:
            x0, y0 = y0, x0
            x1, y1 = y1, x1
        if x0 > x1:
            x0, x1 = x1, x0
            y0, y1 = y1, y0

        dx = x1 - x0
        dy = abs(y1 - y0)
        err = _cdiv(dx, 2) if dx else 0
        ystep = 1 if y0 < y1 else -1

        while x0 <= x1:
            if steep:
                self.draw_pixel(y0, x0, color)
            else:
                self.draw_pixel(x0, y0, color)
            err -= dy
            if err < 0:
                y0 += ystep
                err += dx
            x0 += 1

    def draw_rect(self, x, y, w, h, color):
        self.draw_fast_hline(x, y, w, color)
        self.draw_fast_hline(x, y + h - 1, w, color)
        self.draw_fast_vline(x, y, h, color)
        self.draw_fast_vline(x + w - 1, y, h, color)

    def draw_circle(self, x0, y0, r, color):
        f = 1 - r
        ddf_x = 1
        ddf_y = -2 * r
        x = 0
        y = r

        self.draw_pixel(x0, y0 + r, color)
        self.draw_pixel(x0, y0 - r, color)
        self.draw_pixel(x0 + r, y0, color)
        self.draw_pixel(x0 - r, y0, color)

        while x < y:
            if f >= 0:
                y -= 1
                ddf_y += 2
                f += ddf_y
            x += 1
            ddf_x += 2
            f += ddf_x

            for px, py in (
                (x0 + x, y0 + y), (x0 - x, y0 + y), (x0 + x, y0 - y), (x0 - x, y0 - y),
                (x0 + y, y0 + x), (x0 - y, y0 + x), (x0 + y, y0 - x), (x0 - y, y0 - x),
            ):
                self.draw_pixel(px, py, color)

    def _draw_circle_helper(self, x0, y0, r, corners, color):
        f = 1 - r
        ddf_x = 1
        ddf_y = -2 * r
        x = 0
        y = r
        while x < y:
            if f >= 0:
                y -= 1
                ddf_y += 2
                f += ddf_y
            x += 1
            ddf_x += 2
            f += ddf_x
            if corners & 0x4:
                self.draw_pixel(x0 + x, y0 + y, color)
                self.draw_pixel(x0 + y, y0 + x, color)
            if corners & 0x2:
                self.draw_pixel(x0 + x, y0 - y, color)
                self.draw_pixel(x0 + y, y0 - x, color)
            if corners & 0x8:
                self.draw_pixel(x0 - y, y0 + x, color)
                self.draw_pixel(x0 - x, y0 + y, color)
            if corners & 0x1:
                self.draw_pixel(x0 - y, y0 - x, color)
                self.draw_pixel(x0 - x, y0 - y, color)

    def _fill_circle_helper(self, x0, y0, r, corners, delta, color):
        f = 1 - r
        ddf_x = 1
        ddf_y = -2 * r
        x = 0
        y = r
        px = x
        py = y
        delta += 1

        while x < y:
            if f >= 0:
                y -= 1
                ddf_y += 2
                f += ddf_y
            x += 1
            ddf_x += 2
            f += ddf_x

            if x < y + 1:
                if corners & 1:
                    self.draw_fast_vline(x0 + x, y0 - y, 2 * y + delta, color)
                if corners & 2:
                    self.draw_fast_vline(x0 - x, y0 - y, 2 * y + delta, color)
            if y != py:
                if corners & 1:
                    self.draw_fast_vline(x0 + py, y0 - px, 2 * px + delta, color)
                if corners & 2:
                    self.draw_fast_vline(x0 - py, y0 - px, 2 * px + delta, color)
                py = y
            px = x

    def fill_circle(self, x0, y0, r, color):
        self.draw_fast_vline(x0, y0 - r, 2 * r + 1, color)
        self._fill_circle_helper(x0, y0, r, 3, 0, color)

    def draw_round_rect(self, x, y, w, h, r, color):
        max_radius = _cdiv(w if w < h else h, 2)
        if r > max_radius:
            r = max_radius
        self.draw_fast_hline(x + r, y, w - 2 * r, color)
        self.draw_fast_hline(x + r, y + h - 1, w - 2 * r, color)
        self.draw_fast_vline(x, y + r, h - 2 * r, color)
        self.draw_fast_vline(x + w - 1, y + r, h - 2 * r, color)
        self._draw_circle_helper(x + r, y + r, r, 1, color)
        self._draw_circle_helper(x + w - r - 1, y + r, r, 2, color)
        self._draw_circle_helper(x + w - r - 1, y + h - r - 1, r, 4, color)
        self._draw_circle_helper(x + r, y + h - r - 1, r, 8, color)

    def fill_round_rect(self, x, y, w, h, r, color):
        max_radius = _cdiv(w if w < h else h, 2)
        if r > max_radius:
            r = max_radius
        self.fill_rect(x + r, y, w - 2 * r, h, color)
        self._fill_circle_helper(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, color)
        self._fill_circle_helper(x + r, y + r, r, 2, h - 2 * r - 1, color)

    def draw_triangle(self, x0, y0, x1, y1, x2, y2, color):
        self.draw_line(x0, y0, x1, y1, color)
        self.draw_line(x1, y1, x2, y2, color)
        self.draw_line(x2, y2, x0, y0, color)

    def fill_triangle(self, x0, y0, x1, y1, x2, y2, color):
        if y0 > y1:
            y0, y1 = y1, y0
            x0, x1 = x1, x0
        if y1 > y2:
            y2, y1 = y1, y2
            x2, x1 = x1, x2
        if y0 > y1:
            y0, y1 = y1, y0
            x0, x1 = x1, x0

        if y0 == y2:  # degenerate: all on one line
            a = b = x0
            a = min(a, x1, x2)
            b = max(b, x1, x2)
            self.draw_fast_hline(a, y0, b - a + 1, color)
            return

        dx01, dy01 = x1 - x0, y1 - y0
        dx02, dy02 = x2 - x0, y2 - y0
        dx12, dy12 = x2 - x1, y2 - y1
        sa = sb = 0

        last = y1 if y1 == y2 else y1 - 1

        y = y0
        while y <= last:
            a = x0 + _cdiv(sa, dy01)
            b = x0 + _cdiv(sb, dy02)
            sa += dx01
            sb += dx02
            if a > b:
                a, b = b, a
            self.draw_fast_hline(a, y, b - a + 1, color)
            y += 1

        sa = dx12 * (y - y1)
        sb = dx02 * (y - y0)
        while y <= y2:
            a = x1 + _cdiv(sa, dy12)
            b = x0 + _cdiv(sb, dy02)
            sa += dx12
            sb += dx02
            if a > b:
                a, b = b, a
            self.draw_fast_hline(a, y, b - a + 1, color)
            y += 1

    # -------------------------------------------------------------------- text

    def set_text_size(self, size):
        self.text_size_x = self.text_size_y = max(1, int(size))

    def set_text_color(self, color, background=None):
        self.text_color = color
        self.text_bg = color if background is None else background

    def set_cursor(self, x, y):
        self.cursor_x = x
        self.cursor_y = y

    def set_font(self, font):
        self.gfx_font = font

    def _draw_char_custom(self, x, y, char, color, size_x, size_y):
        """Adafruit_GFX's custom-font branch: a bit stream, MSB first.

        Note it ignores the background entirely -- custom fonts are always
        drawn transparent, unlike the built-in one.
        """
        font = self.gfx_font
        glyph = font.glyph(ord(char) & 0xFF)
        if glyph is None:
            return
        offset, gw, gh, _advance, x_offset, y_offset = glyph

        bits = 0
        bit = 0
        for row in range(gh):
            for column in range(gw):
                if not (bit & 7):
                    bits = font.bitmaps[offset]
                    offset += 1
                bit += 1
                if bits & 0x80:
                    if size_x == 1 and size_y == 1:
                        self.draw_pixel(x + x_offset + column,
                                        y + y_offset + row, color)
                    else:
                        self.fill_rect(x + (x_offset + column) * size_x,
                                       y + (y_offset + row) * size_y,
                                       size_x, size_y, color)
                bits = (bits << 1) & 0xFF

    def draw_char(self, x, y, char, color, background, size_x, size_y):
        if self.gfx_font is not None:
            self._draw_char_custom(x, y, char, color, size_x, size_y)
            return

        code = ord(char) & 0xFF
        if x >= self.width or y >= self.height:
            return
        if x + 6 * size_x - 1 < 0 or y + 8 * size_y - 1 < 0:
            return
        if code >= 176:
            code += 1  # classic charset quirk, _cp437 is false by default

        for i in range(5):
            line = self._font[code * 5 + i]
            for j in range(8):
                if line & 1:
                    if size_x == 1 and size_y == 1:
                        self.draw_pixel(x + i, y + j, color)
                    else:
                        self.fill_rect(x + i * size_x, y + j * size_y, size_x, size_y, color)
                elif background != color:
                    if size_x == 1 and size_y == 1:
                        self.draw_pixel(x + i, y + j, background)
                    else:
                        self.fill_rect(x + i * size_x, y + j * size_y, size_x, size_y, background)
                line >>= 1

        if background != color:
            if size_x == 1 and size_y == 1:
                self.draw_fast_vline(x + 5, y, 8, background)
            else:
                self.fill_rect(x + 5 * size_x, y, size_x, 8 * size_y, background)

    def print(self, text):
        for char in str(text):
            if self.gfx_font is None:
                if char == "\n":
                    self.cursor_x = 0
                    self.cursor_y += self.text_size_y * 8
                elif char != "\r":
                    if self.wrap and (self.cursor_x + self.text_size_x * 6) > self.width:
                        self.cursor_x = 0
                        self.cursor_y += self.text_size_y * 8
                    self.draw_char(
                        self.cursor_x, self.cursor_y, char,
                        self.text_color, self.text_bg,
                        self.text_size_x, self.text_size_y,
                    )
                    self.cursor_x += self.text_size_x * 6
                continue

            font = self.gfx_font
            if char == "\n":
                self.cursor_x = 0
                self.cursor_y += self.text_size_y * font.y_advance
            elif char != "\r":
                glyph = font.glyph(ord(char) & 0xFF)
                if glyph is None:
                    continue
                _offset, gw, gh, advance, x_offset, _y_offset = glyph
                if gw > 0 and gh > 0:
                    if self.wrap and (self.cursor_x + self.text_size_x *
                                      (x_offset + gw)) > self.width:
                        self.cursor_x = 0
                        self.cursor_y += self.text_size_y * font.y_advance
                    self.draw_char(self.cursor_x, self.cursor_y, char,
                                   self.text_color, self.text_bg,
                                   self.text_size_x, self.text_size_y)
                self.cursor_x += advance * self.text_size_x

    def get_text_bounds(self, text, x, y):
        """Adafruit_GFX::getTextBounds -> (x1, y1, width, height)."""
        min_x = min_y = 0x7FFF
        max_x = max_y = -0x8000
        cursor_x, cursor_y = x, y

        for char in str(text):
            if self.gfx_font is None:
                if char == "\n":
                    cursor_x = x
                    cursor_y += self.text_size_y * 8
                elif char != "\r":
                    if self.wrap and (cursor_x + self.text_size_x * 6) > self.width:
                        cursor_x = x
                        cursor_y += self.text_size_y * 8
                    x2 = cursor_x + self.text_size_x * 6 - 1
                    y2 = cursor_y + self.text_size_y * 8 - 1
                    min_x, min_y = min(min_x, cursor_x), min(min_y, cursor_y)
                    max_x, max_y = max(max_x, x2), max(max_y, y2)
                    cursor_x += self.text_size_x * 6
                continue

            font = self.gfx_font
            if char == "\n":
                cursor_x = x
                cursor_y += self.text_size_y * font.y_advance
            elif char != "\r":
                glyph = font.glyph(ord(char) & 0xFF)
                if glyph is None:
                    continue
                _offset, gw, gh, advance, x_offset, y_offset = glyph
                if gw > 0 and gh > 0:
                    if self.wrap and (cursor_x + self.text_size_x *
                                      (x_offset + gw)) > self.width:
                        cursor_x = x
                        cursor_y += self.text_size_y * font.y_advance
                    x1 = cursor_x + x_offset * self.text_size_x
                    y1 = cursor_y + y_offset * self.text_size_y
                    x2 = x1 + gw * self.text_size_x - 1
                    y2 = y1 + gh * self.text_size_y - 1
                    min_x, min_y = min(min_x, x1), min(min_y, y1)
                    max_x, max_y = max(max_x, x2), max(max_y, y2)
                cursor_x += advance * self.text_size_x

        if max_x < min_x:
            return x, y, 0, 0
        return min_x, min_y, max_x - min_x + 1, max_y - min_y + 1

    def draw_bitmap(self, x, y, data, w, h, color, background=None):
        """Adafruit_GFX::drawBitmap. background=None leaves clear bits alone."""
        byte_width = (w + 7) // 8
        for row in range(h):
            for column in range(w):
                byte = data[row * byte_width + (column >> 3)]
                if byte & (0x80 >> (column & 7)):
                    self.draw_pixel(x + column, y + row, color)
                elif background is not None:
                    self.draw_pixel(x + column, y + row, background)

    # ------------------------------------------------------------------ export

    def to_gray(self):
        """Convert to the 0..255 buffer png.write_gray_png expects."""
        return bytearray(0 if value == BLACK else 255 for value in self.buffer)

    def ink_pixels(self):
        return sum(1 for value in self.buffer if value == BLACK)


# ---------------------------------------------------------------- GFX fonts

class GfxFont:
    """A parsed Adafruit GFXfont: glyph bitmaps plus per-glyph metrics."""

    def __init__(self, bitmaps, glyphs, first, last, y_advance):
        self.bitmaps = bitmaps
        self.glyphs = glyphs  # list of (offset, w, h, xAdvance, xOffset, yOffset)
        self.first = first
        self.last = last
        self.y_advance = y_advance

    def glyph(self, code):
        if code < self.first or code > self.last:
            return None
        return self.glyphs[code - self.first]


def _signed_byte(value):
    return value - 256 if value > 127 else value


def load_gfx_font(name):
    """Parse one of Adafruit_GFX's Fonts/*.h headers.

    Parsed rather than transcribed for the same reason as glcdfont: the goldens
    must show the glyphs the device draws, not a copy that can drift.
    """
    pattern = os.path.join(_PROJECT_ROOT, ".pio", "libdeps", "*",
                           "Adafruit GFX Library", "Fonts", name + ".h")
    matches = sorted(glob.glob(pattern))
    if not matches:
        raise RuntimeError(
            "font %s.h not found under .pio/libdeps. Run `pio run -e Alpdeck` "
            "once so PlatformIO fetches Adafruit GFX." % name
        )

    with open(matches[0], "r", encoding="utf-8", errors="replace") as handle:
        source = handle.read()

    # Bitmaps: the first brace-delimited array in the file.
    bitmap_start = source.index("Bitmaps[]")
    bitmap_body = source[source.index("{", bitmap_start) + 1:
                         source.index("};", bitmap_start)]
    bitmaps = [int(token, 16) for token in re.findall(r"0[xX][0-9a-fA-F]{1,2}", bitmap_body)]

    # Glyphs: rows of six comma-separated numbers, some negative.
    glyph_start = source.index("Glyphs[]")
    glyph_body = source[source.index("{", glyph_start) + 1:
                        source.index("};", glyph_start)]
    glyphs = []
    for row in re.findall(r"\{([^{}]*)\}", glyph_body):
        numbers = [int(value) for value in re.findall(r"-?\d+", row)]
        if len(numbers) == 6:
            glyphs.append(tuple(numbers))

    # Font record: first, last, yAdvance are the trailing three numbers.
    # Stop at the closing brace: these headers end with an "// Approx. N bytes"
    # comment whose number would otherwise be read as yAdvance.
    font_start = source.rindex("PROGMEM = {")
    record = source[font_start:source.index("};", font_start)]
    numbers = re.findall(r"0[xX][0-9a-fA-F]+|\b\d+\b", record)
    first = int(numbers[-3], 0)
    last = int(numbers[-2], 0)
    y_advance = int(numbers[-1], 0)

    return GfxFont(bitmaps, glyphs, first, last, y_advance)
