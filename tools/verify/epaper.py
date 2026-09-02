"""GxEPD2 + Display.cpp frame semantics, off device.

Two behaviours matter and both have already caused bugs on hardware, so they
are modelled explicitly rather than approximated:

1. ``firstPage()`` whitens the page buffer. Opening a second frame and calling
   show() therefore pushes a blank screen -- the "launcher renders nothing"
   bug. There is no incremental drawing on top of what is already displayed.
2. A partial window only transfers its own rectangle to the panel. Everything
   outside keeps the previous contents, which is why the panel is a persistent
   image here and not a fresh canvas per frame.

The panel is the thing a photograph of the device would show; the canvas is
what the firmware is currently drawing into.
"""

from gfx import BLACK, WHITE, Canvas, load_font

PANEL_WIDTH = 400
PANEL_HEIGHT = 300

# What the first frame after a hibernate costs extra: a hardware reset and a
# re-init. Measured at a flat 41ms across every window size, which is why it is
# added rather than scaled.
PANEL_WAKE_MS = 41


class Panel:
    def __init__(self, width=PANEL_WIDTH, height=PANEL_HEIGHT, font=None):
        self.width = width
        self.height = height
        self._font = font if font is not None else load_font()

        # What the panel is showing. A powered e-paper panel holds its image,
        # so this survives between frames.
        self.displayed = Canvas(width, height, self._font)

        self.canvas = Canvas(width, height, self._font)
        self.frame_open = False
        self.window = (0, 0, width, height)

        # Diagnostics the harness asserts on.
        self.full_refreshes = 0
        self.partial_refreshes = 0
        self.frames_shown = 0

        # What the last frame would have cost on the device. Measured on the
        # GDEY042T81: a warm partial refresh is 402ms fixed plus 0.70ms a row, a
        # warm full one drives the whole panel at 1989ms, and a panel that had
        # hibernated costs a flat 41ms more for the reset and re-init. Modelled
        # rather than guessed so a script that paces itself off display.timing()
        # is exercised against the shape of the real thing.
        self.last_refresh_ms = 0
        self.hibernated = False

    # Mirrors Display::beginFrame(bool partial). Note the default is *full*,
    # which is what an implicit open from a bare draw call gets.
    def begin_frame(self, partial=False):
        if self.frame_open:
            return  # already drawing; the caller's frame is kept

        if partial:
            self.window = (0, 0, self.width, self.height)
        else:
            self.window = (0, 0, self.width, self.height)
        self._partial = partial

        x, y, w, h = self.window
        self.canvas.set_clip(x, y, w, h)
        # firstPage(): the page buffer comes up white.
        self.canvas.fill_rect(x, y, w, h, WHITE)
        self.frame_open = True

    def begin_region(self, x, y, w, h, partial=True):
        """Frame bound to a sub-rectangle -- the display.region() design."""
        if self.frame_open:
            return
        self.window = (x, y, w, h)
        self._partial = partial
        self.canvas.set_clip(x, y, w, h)
        self.canvas.fill_rect(x, y, w, h, WHITE)
        self.frame_open = True

    def end_frame(self):
        if not self.frame_open:
            return
        self.frame_open = False

        x, y, w, h = self.window
        for row in range(y, min(y + h, self.height)):
            start = row * self.width
            self.displayed.buffer[start + x : start + min(x + w, self.width)] = (
                self.canvas.buffer[start + x : start + min(x + w, self.width)]
            )

        self.frames_shown += 1
        wake = PANEL_WAKE_MS if self.hibernated else 0
        self.hibernated = False

        if getattr(self, "_partial", False):
            self.partial_refreshes += 1
            self.last_refresh_ms = int(round(402 + 0.70 * h)) + wake
        else:
            self.full_refreshes += 1
            self.last_refresh_ms = 1989 + wake

        self.canvas.set_clip(0, 0, self.width, self.height)

    def ensure_frame(self):
        """Mirrors the bindings' canvas(): a bare draw call opens a full frame."""
        if not self.frame_open:
            self.begin_frame()
        return self.canvas
