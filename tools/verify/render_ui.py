"""Preview the C++-drawn screens: the mark, the bootscreen, the error screen.

    python tools/verify/render_ui.py [out_dir]

These are PREVIEWS, not goldens. run.py does not compare them and they are not
committed. Layout constants are parsed out of the C++, so a layout change does
show up here -- but the drawing procedure is a port, and a change to HOW
Logo.cpp or Bootscreen.cpp draws will not appear until this file is updated
too. Treat a preview as "roughly what the panel will show"; the device is the
authority.

The geometry is not copied: it is parsed out of the LOGO_GEOMETRY block in
src/ui/Logo.cpp, so the preview cannot drift from what the firmware draws.

What IS duplicated is the drawing procedure -- the stroke weight rule, the
flank interpolation, the cap polyline. That is about forty lines, and it is
duplicated because there is no host compiler on this machine to build the real
Logo.cpp against. If a native toolchain ever appears, delete this and render
from the firmware itself.
"""

import os
import re
import sys

from gfx import BLACK, WHITE, Canvas
from png import write_gray_png

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(os.path.dirname(HERE))
LOGO_SOURCE = os.path.join(PROJECT_ROOT, "src", "ui", "Logo.cpp")
BOOTSCREEN_SOURCE = os.path.join(PROJECT_ROOT, "src", "ui", "Bootscreen.cpp")
CONFIG_SOURCE = os.path.join(PROJECT_ROOT, "src", "config", "AppConfig.h")

SIMPLIFY_BELOW_WIDTH = 90


def geometry():
    """Parse the constants between the LOGO_GEOMETRY markers."""
    with open(LOGO_SOURCE, "r", encoding="utf-8") as handle:
        source = handle.read()

    block = source[source.index("LOGO_GEOMETRY_BEGIN"):
                   source.index("LOGO_GEOMETRY_END")]
    values = dict(re.findall(r"(k[A-Za-z]+)\s*=\s*(-?[\d.]+)f", block))
    if not values:
        raise RuntimeError("no geometry found in %s" % LOGO_SOURCE)
    return {name: float(value) for name, value in values.items()}


class Peak:
    def __init__(self, apex_x, apex_y, left_foot_x, right_foot_x, foot_y):
        self.apex_x = apex_x
        self.apex_y = apex_y
        self.left_foot_x = left_foot_x
        self.right_foot_x = right_foot_x
        self.foot_y = foot_y

    def flank_x(self, foot_x, depth):
        return self.apex_x + (foot_x - self.apex_x) * depth

    def depth_y(self, depth):
        return self.apex_y + (self.foot_y - self.apex_y) * depth


def _stroke(canvas, x0, y0, x1, y1, color, weight):
    mostly_horizontal = abs(x1 - x0) >= abs(y1 - y0)
    for step in range(weight):
        dx = 0 if mostly_horizontal else step
        dy = step if mostly_horizontal else 0
        canvas.draw_line(x0 + dx, y0 + dy, x1 + dx, y1 + dy, color)


def draw_logo(canvas, x, y, width, geo, color=BLACK):
    height = int(width * geo["kAspect"] + 0.5)
    simplified = width < SIMPLIFY_BELOW_WIDTH
    weight = 2 if width >= 150 else 1

    def px(value):
        return x + int(value * width + 0.5)

    def py(value):
        return y + int(value * height + 0.5)

    peaks = (
        Peak(geo["kRightApexX"], geo["kRightApexY"], geo["kRightLeftFootX"],
             geo["kRightRightFootX"], geo["kRightFootY"]),
        Peak(geo["kLeftApexX"], geo["kLeftApexY"], geo["kLeftLeftFootX"],
             geo["kLeftRightFootX"], geo["kLeftFootY"]),
    )

    for peak in peaks:
        _stroke(canvas, px(peak.left_foot_x), py(peak.foot_y),
                px(peak.apex_x), py(peak.apex_y), color, weight)
        _stroke(canvas, px(peak.apex_x), py(peak.apex_y),
                px(peak.right_foot_x), py(peak.foot_y), color, weight)
        _stroke(canvas, px(peak.left_foot_x), py(peak.foot_y),
                px(peak.right_foot_x), py(peak.foot_y), color, weight)

        left_x = peak.flank_x(peak.left_foot_x, geo["kCapFlank"])
        right_x = peak.flank_x(peak.right_foot_x, geo["kCapFlank"])
        flank_y = peak.depth_y(geo["kCapFlank"])
        notch_y = peak.depth_y(geo["kCapNotch"])
        rise_y = peak.depth_y(geo["kCapRise"])
        span = right_x - left_x

        if simplified:
            xs = (left_x, left_x + span * 0.5, right_x)
            ys = (flank_y, notch_y, flank_y)
        else:
            xs = (left_x, left_x + span * 0.25, left_x + span * 0.5,
                  left_x + span * 0.75, right_x)
            ys = (flank_y, notch_y, rise_y, notch_y, flank_y)

        for i in range(len(xs) - 1):
            _stroke(canvas, px(xs[i]), py(ys[i]), px(xs[i + 1]), py(ys[i + 1]),
                    color, weight)

    return height


def bootscreen_layout():
    """Parse the layout constants out of Bootscreen.cpp."""
    with open(BOOTSCREEN_SOURCE, "r", encoding="utf-8") as handle:
        source = handle.read()
    values = dict(re.findall(r"constexpr \w+ (k[A-Za-z]+) = (-?\d+);", source))
    return {name: int(value) for name, value in values.items()}


def app_strings():
    with open(CONFIG_SOURCE, "r", encoding="utf-8") as handle:
        source = handle.read()
    name = re.search(r'APP_NAME = "([^"]*)"', source).group(1)
    subtitle = re.search(r'APP_SUBTITLE = "([^"]*)"', source).group(1)
    major = re.search(r"APP_VERSION_MAJOR = (\d+)", source).group(1)
    minor = re.search(r"APP_VERSION_MINOR = (\d+)", source).group(1)
    return name, subtitle, "%s v%s.%s" % (name, major, minor)


def _centered(canvas, text, y, size, font):
    canvas.set_font(font)
    canvas.set_text_size(size)
    _x1, y1, width, _height = canvas.get_text_bounds(text, 0, 0)
    canvas.set_text_color(BLACK)
    canvas.set_cursor(canvas.width // 2 - width // 2, y - y1)
    canvas.print(text)
    return _height


def draw_bootscreen(canvas, geo, layout, error=None):
    from gfx import load_gfx_font

    canvas.fill_screen(WHITE)
    name, subtitle, version = app_strings()

    logo_width = layout["kLogoWidth"]
    logo_top = layout["kLogoTop"]
    logo_height = draw_logo(canvas, canvas.width // 2 - logo_width // 2,
                            logo_top, logo_width, geo)

    title_top = logo_top + logo_height + layout["kTitleGap"]
    title_height = _centered(canvas, name, title_top, layout["kTitleSize"],
                             load_gfx_font("FreeSansBold9pt7b"))
    _centered(canvas, subtitle,
              title_top + title_height + layout["kSubtitleGap"], 1, None)

    canvas.set_font(None)
    canvas.set_text_size(1)
    canvas.set_text_color(BLACK)
    canvas.set_cursor(6, canvas.height - layout["kVersionBottomInset"])
    canvas.print(version)

    if error is None:
        return

    lines = error.split(chr(10))
    longest = max(len(line) for line in lines)
    block_w = layout["kSignWidth"] + layout["kSignTextGap"] + 6 * longest
    block_h = layout["kSignHeight"] + 2 * layout["kFramePadY"]
    left = max(layout["kFramePadX"] + 2, canvas.width // 2 - block_w // 2)

    band_top = title_top + 8 * layout["kTitleSize"] + layout["kSubtitleGap"] + 24
    band_bottom = canvas.height - layout["kVersionBottomInset"] - 6
    frame_top = band_top + (band_bottom - band_top - block_h) // 2
    content_top = frame_top + layout["kFramePadY"]

    canvas.draw_rect(left - layout["kFramePadX"], frame_top,
                     block_w + 2 * layout["kFramePadX"], block_h, BLACK)

    sign_w, sign_h = layout["kSignWidth"], layout["kSignHeight"]
    canvas.fill_triangle(left + sign_w // 2, content_top,
                         left, content_top + sign_h - 1,
                         left + sign_w - 1, content_top + sign_h - 1, BLACK)
    canvas.set_text_size(2)
    canvas.set_text_color(WHITE)
    canvas.set_cursor(left + sign_w // 2 - 5, content_top + 9)
    canvas.print("!")

    canvas.set_text_size(1)
    canvas.set_text_color(BLACK)
    text_x = left + sign_w + layout["kSignTextGap"]
    if len(lines) > 1:
        canvas.set_cursor(text_x, content_top + 4)
        canvas.print(lines[0])
        canvas.set_cursor(text_x, content_top + 16)
        canvas.print(lines[1])
    else:
        canvas.set_cursor(text_x, content_top + 10)
        canvas.print(lines[0])


def main(argv):
    out_dir = argv[0] if argv else os.path.join(HERE, "out")
    os.makedirs(out_dir, exist_ok=True)
    geo = geometry()
    layout = bootscreen_layout()

    # One sheet, every size that matters: bootscreen, half, and the navbar icon
    # the launcher may want back.
    widths = (200, 120, 90, 64, 32, 16)
    canvas = Canvas(400, 300)
    canvas.fill_screen(WHITE)

    cursor_y = 12
    for width in widths:
        height = draw_logo(canvas, 12, cursor_y, width, geo)
        canvas.set_font(None)
        canvas.set_text_size(1)
        canvas.set_text_color(BLACK)
        canvas.set_cursor(240, cursor_y + max(0, height // 2 - 4))
        canvas.print("%d x %d px" % (width, height))
        cursor_y += height + 10

    path = os.path.join(out_dir, "logo-sizes.png")
    write_gray_png(path, canvas.width, canvas.height, canvas.to_gray())
    print("wrote", path)

    for filename, error in (("bootscreen.png", None),
                            ("bootscreen-error.png",
                             "no input controller found" + chr(10) +
                             "connect a rotary or gamepad")):
        screen = Canvas(400, 300)
        draw_bootscreen(screen, geo, layout, error)
        path = os.path.join(out_dir, filename)
        write_gray_png(path, screen.width, screen.height, screen.to_gray())
        print("wrote", path)

    return 0


if __name__ == "__main__":
    sys.path.insert(0, HERE)
    raise SystemExit(main(sys.argv[1:]))
