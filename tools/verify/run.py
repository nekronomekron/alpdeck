"""Entry point for the off-device verification harness.

    python tools/verify/run.py            # check everything against the goldens
    python tools/verify/run.py --bless    # accept current output as the goldens
    python tools/verify/run.py --strict   # also fail on known sandbox issues

Exit code is non-zero when anything fails, so this can gate a commit.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_sandbox  # noqa: E402
import png  # noqa: E402
import render_states  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
GOLDEN_DIR = os.path.join(HERE, "golden")
OUTPUT_DIR = os.path.join(HERE, "out")

# A screen that is entirely white or entirely black is the blank-screen and the
# black-on-black bug respectively. Neither has ever been a legitimate state.
MIN_INK_RATIO = 0.001
MAX_INK_RATIO = 0.95


def _ink_ratio(pixels):
    dark = sum(1 for value in pixels if value < 128)
    return dark / float(len(pixels))


def _compare(name, width, height, pixels, bless):
    golden_path = os.path.join(GOLDEN_DIR, name + ".png")

    if bless or not os.path.exists(golden_path):
        png.write_gray_png(golden_path, width, height, pixels)
        return "blessed" if bless else "created"

    g_width, g_height, g_pixels = png.read_gray_png(golden_path)
    if (g_width, g_height) != (width, height):
        return "size changed: golden %dx%d, now %dx%d" % (g_width, g_height, width, height)

    differing = sum(1 for a, b in zip(g_pixels, pixels) if a != b)
    if differing:
        return "%d pixels differ (%.2f%%)" % (differing, 100.0 * differing / len(pixels))
    return None


def main(argv):
    bless = "--bless" in argv
    strict = "--strict" in argv

    os.makedirs(GOLDEN_DIR, exist_ok=True)
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    print("sandbox conformance")
    _, sandbox_failures, sandbox_issues = check_sandbox.run(strict=strict)

    print("\nrendered states")
    failures = 0
    created = 0
    for name, ok, error, host in render_states.scenarios():
        if not ok:
            print("  FAIL  %-22s script error: %s" % (name, error))
            failures += 1
            continue

        panel = host.panel
        pixels = panel.displayed.to_gray()
        png.write_gray_png(os.path.join(OUTPUT_DIR, name + ".png"),
                           panel.width, panel.height, pixels)

        ratio = _ink_ratio(pixels)
        if panel.frames_shown == 0:
            print("  FAIL  %-22s never called display.show()" % name)
            failures += 1
            continue
        if ratio < MIN_INK_RATIO:
            print("  FAIL  %-22s blank screen (%.3f%% ink)" % (name, ratio * 100))
            failures += 1
            continue
        if ratio > MAX_INK_RATIO:
            print("  FAIL  %-22s screen is almost solid black (%.1f%% ink)" % (name, ratio * 100))
            failures += 1
            continue

        verdict = _compare(name, panel.width, panel.height, pixels, bless)
        if verdict in ("blessed", "created"):
            created += 1
            print("  %-5s %-22s %.1f%% ink, %d frame(s)"
                  % (verdict.upper()[:5], name, ratio * 100, panel.frames_shown))
        elif verdict:
            failures += 1
            print("  FAIL  %-22s %s" % (name, verdict))
        else:
            print("  ok    %-22s %.1f%% ink, %d frame(s)"
                  % (name, ratio * 100, panel.frames_shown))

    total_failures = failures + sandbox_failures
    print("\n%s -- %d failure(s), %d golden(s) written, %d known issue(s)"
          % ("FAILED" if total_failures else "PASSED", total_failures, created, sandbox_issues))
    return 1 if total_failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
