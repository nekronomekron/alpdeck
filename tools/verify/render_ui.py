"""Preview the C++-drawn screens: the mark, the bootscreen, the error screen.

    python tools/verify/render_ui.py [out_dir]

These are PREVIEWS, not goldens. run.py does not compare them and they are not
committed. Layout constants and the logo geometry are parsed straight out of
Logo.cpp, Bootscreen.cpp and AppConfig.h, so changing a number shows up here --
but the drawing procedure is a port, and changing HOW those files draw will not
appear until this file is updated to match. The device is the authority for
anything drawn in C++.
"""

import os
import re
import sys

from gfx import BLACK, WHITE, Canvas, load_gfx_font
from png import write_gray_png

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(os.path.dirname(HERE))
LOGO_SOURCE = os.path.join(PROJECT_ROOT, "src", "ui", "Logo.cpp")
BOOTSCREEN_SOURCE = os.path.join(PROJECT_ROOT, "src", "ui", "Bootscreen.cpp")
CONFIG_SOURCE = os.path.join(PROJECT_ROOT, "src", "config", "AppConfig.h")

SIMPLIFY_BELOW_WIDTH = 90


# ------------------------------------------------------------------ parsing

def geometry():
    """The constants between the LOGO_GEOMETRY markers in Logo.cpp."""
    with open(LOGO_SOURCE, "r", encoding="utf-8") as handle:
        source = handle.read()

    block = source[source.index("LOGO_GEOMETRY_BEGIN"):
                   source.index("LOGO_GEOMETRY_END")]
    values = dict(re.findall(r"(k[A-Za-z]+)\s*=\s*(-?[\d.]+)f", block))
    if not values:
        raise RuntimeError("no geometry found in %s" % LOGO_SOURCE)
    return {name: float(value) for name, value in values.items()}


def bootscreen_layout():
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


# -------------------------------------------------------------- rasteriser
#
# A port of the one in Logo.cpp. Adafruit_GFX cannot skip pixels, so the lines
# and the cap fill are rasterised by hand where the occluder can be consulted.

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


def _cross(ax, ay, bx, by, x, y):
    return (bx - ax) * (y - ay) - (by - ay) * (x - ax)


def _hidden(occluder, x, y):
    """Point in triangle, boundary included."""
    if occluder is None:
        return False
    (ax, ay), (bx, by), (cx, cy) = occluder
    d1 = _cross(ax, ay, bx, by, x, y)
    d2 = _cross(bx, by, cx, cy, x, y)
    d3 = _cross(cx, cy, ax, ay, x, y)
    return not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0))


def _plot(canvas, x, y, color, occluder):
    if not _hidden(occluder, x, y):
        canvas.draw_pixel(x, y, color)


def _line(canvas, x0, y0, x1, y1, color, weight, occluder):
    mostly_horizontal = abs(x1 - x0) >= abs(y1 - y0)
    steep = abs(y1 - y0) > abs(x1 - x0)
    if steep:
        x0, y0 = y0, x0
        x1, y1 = y1, x1
    if x0 > x1:
        x0, x1 = x1, x0
        y0, y1 = y1, y0

    dx = x1 - x0
    dy = abs(y1 - y0)
    error = dx // 2
    step = 1 if y0 < y1 else -1

    y = y0
    for x in range(x0, x1 + 1):
        for offset in range(weight):
            ox = 0 if mostly_horizontal else offset
            oy = offset if mostly_horizontal else 0
            if steep:
                _plot(canvas, y + ox, x + oy, color, occluder)
            else:
                _plot(canvas, x + ox, y + oy, color, occluder)
        error -= dy
        if error < 0:
            y += step
            error += dx


def _fill_polygon(canvas, points, color, occluder):
    """Even-odd scanline fill: the cap's lower edge is a zigzag, so a scanline
    can cross it more than twice."""
    top = min(point[1] for point in points)
    bottom = max(point[1] for point in points)

    for y in range(top, bottom + 1):
        crossings = []
        for i in range(len(points)):
            fx, fy = points[i]
            tx, ty = points[(i + 1) % len(points)]
            if fy == ty:
                continue
            if not (min(fy, ty) <= y < max(fy, ty)):
                continue
            crossings.append(fx + (tx - fx) * (y - fy) // (ty - fy))
        crossings.sort()
        for i in range(0, len(crossings) - 1, 2):
            for x in range(crossings[i], crossings[i + 1] + 1):
                _plot(canvas, x, y, color, occluder)


def draw_logo(canvas, x, y, width, geo, color=BLACK):
    height = int(width * geo["kAspect"] + 0.5)
    simplified = width < SIMPLIFY_BELOW_WIDTH
    weight = 2 if width >= 150 else 1

    def px(value):
        return x + int(value * width + 0.5)

    def py(value):
        return y + int(value * height + 0.5)

    foot_y = geo["kFootY"]
    back = Peak(geo["kRightApexX"], geo["kRightApexY"], geo["kRightLeftFootX"],
                geo["kRightRightFootX"], foot_y)
    front = Peak(geo["kLeftApexX"], geo["kLeftApexY"], geo["kLeftLeftFootX"],
                 geo["kLeftRightFootX"], foot_y)

    occluder = ((px(front.apex_x), py(front.apex_y)),
                (px(front.left_foot_x), py(foot_y)),
                (px(front.right_foot_x), py(foot_y)))

    # Back peak first, with everything behind the front one omitted rather than
    # overpainted, so the mark stays transparent.
    for peak, clip in ((back, occluder), (front, None)):
        apex = (px(peak.apex_x), py(peak.apex_y))
        left = (px(peak.left_foot_x), py(foot_y))
        right = (px(peak.right_foot_x), py(foot_y))

        _line(canvas, left[0], left[1], apex[0], apex[1], color, weight, clip)
        _line(canvas, apex[0], apex[1], right[0], right[1], color, weight, clip)
        _line(canvas, left[0], left[1], right[0], right[1], color, weight, clip)

        cap_left = peak.flank_x(peak.left_foot_x, geo["kCapFlank"])
        cap_right = peak.flank_x(peak.right_foot_x, geo["kCapFlank"])
        flank_y = peak.depth_y(geo["kCapFlank"])
        notch_y = peak.depth_y(geo["kCapNotch"])
        rise_y = peak.depth_y(geo["kCapRise"])
        span = cap_right - cap_left

        cap = [apex, (px(cap_left), py(flank_y))]
        if simplified:
            cap.append((px(cap_left + span * 0.5), py(notch_y)))
        else:
            cap.append((px(cap_left + span * 0.25), py(notch_y)))
            cap.append((px(cap_left + span * 0.5), py(rise_y)))
            cap.append((px(cap_left + span * 0.75), py(notch_y)))
        cap.append((px(cap_right), py(flank_y)))

        _fill_polygon(canvas, cap, color, clip)

    return height


# ------------------------------------------------------------- bootscreen

def _centered(canvas, text, y, size, font):
    canvas.set_font(font)
    canvas.set_text_size(size)
    _x1, y1, width, height = canvas.get_text_bounds(text, 0, 0)
    canvas.set_text_color(BLACK)
    canvas.set_cursor(canvas.width // 2 - width // 2, y - y1)
    canvas.print(text)
    return height


def draw_bootscreen(canvas, geo, layout, error=None, standby=False):
    canvas.fill_screen(WHITE)
    name, subtitle, version = app_strings()
    if standby:
        subtitle, version = "standby", "hold the power button to wake"

    logo_width = layout["kLogoWidth"]
    logo_top = layout["kLogoTop"]
    logo_height = draw_logo(canvas, canvas.width // 2 - logo_width // 2,
                            logo_top, logo_width, geo)

    title_top = logo_top + logo_height + layout["kTitleGap"]
    title_height = _centered(canvas, name, title_top, layout["kTitleSize"], None)
    _centered(canvas, subtitle,
              title_top + title_height + layout["kSubtitleGap"],
              2 if standby else 1, None)

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
    sheet = Canvas(400, 300)
    sheet.fill_screen(WHITE)

    cursor_y = 12
    for width in widths:
        height = draw_logo(sheet, 12, cursor_y, width, geo)
        sheet.set_font(None)
        sheet.set_text_size(1)
        sheet.set_text_color(BLACK)
        sheet.set_cursor(240, cursor_y + max(0, height // 2 - 4))
        sheet.print("%d x %d px" % (width, height))
        cursor_y += height + 10

    path = os.path.join(out_dir, "logo-sizes.png")
    write_gray_png(path, sheet.width, sheet.height, sheet.to_gray())
    print("wrote", path)

    for filename, error, standby in (
            ("bootscreen.png", None, False),
            ("bootscreen-error.png",
             "no input controller found" + chr(10) +
             "connect a rotary or gamepad", False),
            ("bootscreen-standby.png", None, True)):
        screen = Canvas(400, 300)
        draw_bootscreen(screen, geo, layout, error, standby)
        path = os.path.join(out_dir, filename)
        write_gray_png(path, screen.width, screen.height, screen.to_gray())
        print("wrote", path)

    return 0


if __name__ == "__main__":
    sys.path.insert(0, HERE)
    raise SystemExit(main(sys.argv[1:]))
