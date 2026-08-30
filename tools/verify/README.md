# Off-device verification harness

Runs the launcher and the sample apps through the real Lua interpreter under the
same restricted environment the firmware builds, renders what the panel would
show, and compares it against committed reference images.

```bash
~/.platformio/penv/Scripts/python.exe tools/verify/run.py
```

There is no system Python on this machine; PlatformIO's `penv` is the
interpreter, and it already has `lupa` 2.8. Nothing else is needed — the PNG
writer is standard library only, deliberately, so the harness cannot rot because
Pillow is missing somewhere.

| flag | effect |
| --- | --- |
| *(none)* | check everything; non-zero exit on any failure |
| `--bless` | accept the current output as the new goldens |
| `--strict` | also fail on the known sandbox issues listed below |

`golden/` is committed and is the reference. `out/` is this run's output and is
git-ignored — diff the two by eye when a check fails.

## Why this exists

The two worst bugs this project has had were invisible to the compiler and to a
syntax check: the launcher rendering a blank screen (`show(full)` pushing an
empty frame), and the selected row drawn black on black. Both are obvious in a
picture and in nothing else. A third — `math` arriving as `nil` because
`luaopen_math` was called directly — got through because it was tested against a
Lua with the full standard library open.

So the harness asserts three things: the sandbox contains exactly what the
firmware installs, every state actually draws something, and the pixels have not
moved.

## What it does and does not cover

**Covers.** Launcher and app logic, sandbox escapes, layout and rendering,
API misuse, and the `firstPage()`-whitens-the-buffer behaviour that caused the
blank-screen bug.

**Does not cover.** Anything on the C side of a binding. This is a real Lua VM
talking to a Python reimplementation of the bindings, so a mistake in
`LuaBindings.cpp` itself is invisible here — the Lua sees a correct API either
way. Hardware timing, refresh duration, I2C, and the boot path are likewise out
of reach.

**Fidelity notes.**

- `gfx.py` reimplements Adafruit_GFX's rasterisation line for line (Bresenham,
  midpoint circle, scanline triangle fill, the classic-font glyph loop). It is
  a reimplementation, not the library: if Adafruit_GFX changes how it
  rasterises, goldens can shift with no change to alpdeck.
- Glyphs are parsed out of the real `glcdfont.c` and `Fonts/*.h` that
  PlatformIO fetched into `.pio/libdeps/`, so text cannot drift from the
  device -- including the three GFXfonts `display.font()` exposes. Run
  `pio run -e Alpdeck` once before the harness on a fresh checkout, otherwise
  those files do not exist yet and the harness says so.
- `fs.read` returns bytes decoded latin-1 so binary assets survive the trip
  through Lua. lupa re-encodes as UTF-8 on the way in, so a Lua-side `#data`
  over binary content reads longer than it would on the device. The length
  check that matters -- the one `display.bitmap` makes -- happens on the
  Python side and is exact.
- The vendored interpreter and lupa are both Lua 5.5, so language semantics
  match.

## Known issues it reports

None at present. `KNOWN_ISSUES` in `check_sandbox.py` is the list of policy
violations that exist but are not yet fixed: entries there are reported on
every run rather than hidden, and move into `REQUIRED` once closed. `loadfile`
and `dofile` were the first two, and are now removed from the sandbox.
