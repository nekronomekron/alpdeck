# The alpdeck Lua API

Everything runnable on an alpdeck is a Lua script — the launcher included.
This is the whole surface a script sees.

**API version 1.** Read it from `sys.info().api`. It is an integer, bumped on
every breaking change, and it exists for exactly one reason: `uploadfs` never
touches the SD card, so a card can hold apps built against an older firmware
and there is otherwise no way to tell. An app may declare `api = 1` in its
manifest; the launcher warns on a mismatch instead of letting it fail in
confusing ways.

## The environment

The standard library is opened with **base, `table`, `string` and `math` only**.
There is no `io`, no `os`, no `package` and therefore no `require`: one app is
one file, and `fs.*` is the only route to storage.

Five globals are provided: `display`, `input`, `fs`, `sys` and `settings`.

A script ends by returning. The host tears down the whole VM, so an app needs
no cleanup of its own.

---

## display

The panel is 400×300, one bit per pixel. There is no grey.

### Frames

```lua
display.begin([mode] [, x, y, w, h])   -- open a frame
display.show()                          -- push it to the panel
```

`begin()` opens a frame and fixes its refresh mode for the whole frame:

| mode | cost | effect |
| --- | --- | --- |
| `"partial"` *(default)* | ~400 ms | fast, leaves faint ghosting behind |
| `"full"` | ~1200 ms | slow, clears accumulated ghosting |

Give `x, y, w, h` to bind the frame to a **region** (always a partial refresh
— a full refresh drives the whole panel by nature). Drawing is clipped to it
and `show()` pushes only that rectangle, which is markedly faster than a whole
panel. Everything outside keeps whatever the panel was already displaying.

Two things follow from how e-paper works here, and both surprise people:

- **The buffer comes up white on every `begin()`.** You cannot draw on top of
  the previous frame — a frame redraws its region from scratch, every time.
- `show()` **blocks** until the panel has finished. There is no asynchronous
  refresh to poll, which is why there is no `busy()`.

`begin()` also resets the ink to black and the font to `"default"`.

A drawing call with no frame open opens a default partial one, so short scripts
can just draw.

```lua
display.size()        --> width, height
display.timing()      --> milliseconds the last show() took
display.clear()       -- fill the drawable area with the current ink
```

### Ink

```lua
display.color("white")   -- or "black", the default
```

Every subsequent drawing call — shapes, text, the set bits of a bitmap — uses
this ink until it changes or the next `begin()`. White is how you erase:

```lua
display.color("white")
display.rect(10, 10, 40, 20, true)   -- rub out a region
display.color("black")
```

There is no `display.invert()`. GxEPD2's page buffer cannot be read back, so
"flip every pixel" is not expressible; a selection highlight is a filled rect
plus white text, which the ink model already covers.

### Text

```lua
display.font("default")            -- 6x8 built-in, exact character cells
display.font("sans")               -- FreeSans 9pt, proportional
display.font("bold")               -- FreeSans Bold 9pt
display.font("pixel")              -- Org_01, blocky, good for games
```

The launcher and the bootscreen deliberately stay on `"default"`: its blocky
cells are the alpdeck look and they land exactly on the pixel grid. The other
faces are there for apps.

```lua

display.text(x, y, s [, size])     -- size 1..8, default 1
display.measure(s [, size])        --> width, height
```

**`x, y` is the top-left corner of the text, for every font.** Raw Adafruit_GFX
positions custom fonts by their baseline; the binding corrects for that so a
layout does not shift when you change fonts. `measure()` reports the same box
`text()` will fill, so centring is `(W - display.measure(s)) / 2` regardless of
which font is active.

### Shapes

Coordinates are pixels; `fill` is a boolean, default false.

```lua
display.pixel(x, y)
display.line(x0, y0, x1, y1)
display.rect(x, y, w, h [, fill])
display.circle(cx, cy, r [, fill])          -- cx,cy is the centre
display.roundrect(x, y, w, h, r [, fill])
display.triangle(x0, y0, x1, y1, x2, y2 [, fill])
```

### Bitmaps

```lua
display.bitmap(x, y, w, h, data [, background])
```

`data` is a **1-bit-per-pixel string**: rows top to bottom, each row padded to a
whole number of bytes, most significant bit leftmost. Exactly
`ceil(w / 8) * h` bytes — a mismatch is an error, not a buffer overrun.

Set bits draw in the current ink. Omit `background` and clear bits are left
alone, which is what a sprite over existing artwork needs; pass `"white"` or
`"black"` to paint them too.

Convert a PNG with the script that ships alongside:

```
python scripts/png2bin.py sprite.png sprite.bin
```

It prints the width and height to pass to `display.bitmap`.

```lua
local sprite = fs.read("player.bin")     -- relative to the app's own folder
display.bitmap(64, 40, 16, 16, sprite)   -- transparent background
```

---

## input

Two controllers, both optional, at least one always present. Event names carry
their source so a two-player app can tell them apart.

```lua
input.read([timeoutMs])   --> event name, or nil on timeout
input.state()             --> what is held right now
input.controllers()       --> { rotary = bool, gamepad = bool }
```

`read()` is **edge-triggered**: it reports presses and never releases, so it
cannot answer "is this button still down". `state()` is the level-triggered
mirror for that.

Events:

```
rotary_cw  rotary_ccw  rotary_up  rotary_down  rotary_left  rotary_right
rotary_select  rotary_select_long
gamepad_up  gamepad_down  gamepad_left  gamepad_right
gamepad_a  gamepad_b  gamepad_x  gamepad_y  gamepad_start  gamepad_select
```

`state()` returns:

```lua
{
  rotary  = { present, select, up, left, down, right, encoder },
  gamepad = { present, a, b, x, y, start, select,
              left, right, up, down,      -- stick digitised
              dx, dy,                     -- signed travel from centre
              stick_x, stick_y },         -- raw ADC, 0..1023
}
```

Gamepad directions are in the **board's own frame**. The gamepad is mounted
rotated 90° clockwise on the device, and un-rotating it is the app's job:
board `(bx, by)` → world `(-by, bx)`.

---

## fs

```lua
fs.list(dir)      --> { {name=, dir=, size=}, ... } or nil
fs.read(path)     --> string or nil
fs.exists(path)   --> boolean
fs.write(path, s) --> boolean
```

Paths starting with `/sd/` are the SD card; everything else is internal flash.

**A path that does not start with `/` is relative to the app's own folder**, so
an app reads its assets with `fs.read("sprite.bin")` and never needs to know
where it was installed. `..` is rejected everywhere.

Writes are confined to the app's own folder. Reads are not, so an app can read
shared data — but it cannot climb out with `..`.

---

## sys

```lua
sys.millis()        --> milliseconds since boot
sys.delay(ms)       -- yields the task; does not spin
sys.log(msg)        -- to the serial log
sys.appdir()        --> the app's folder, e.g. "/sd/apps/snake"
sys.launch(path)    -- ask for the next app, then return
sys.exit()          -- stop this script
sys.memory()        --> luaBytes, freeHeapBytes
sys.temperature()   --> die temperature in °C
sys.info()          --> table, below
sys.wifi()          --> { enabled, connected, portal [, ssid, ip, rssi] }
sys.import(path)    --> a module (see below)
```

Network control, all write-only. Nothing reads a credential back into Lua:

```lua
sys.wifi_scan()                       --> { {ssid=, rssi=, open=}, ... }
sys.wifi_configure(ssid [, password]) -- store and connect
sys.wifi_forget()                     -- forget the stored network
sys.wifi_portal()                     -- raise the setup portal on demand
sys.ftp_configure(user, password)     -- change the FTP login
```

`wifi_scan()` blocks for two to four seconds. That is fine here and nowhere
else: apps run on their own task, so the main loop, FTP and input polling keep
running. Results are deduplicated to the strongest access point per name and
sorted strongest first.

Any app can *set* credentials — worth being plain about. It cannot read them:
they live in their own store and no binding returns them.

`sys.launch()` only records the request — **return from the script afterwards**.
The host tears the VM down before starting the next app, which is what keeps
exactly one interpreter alive at a time. Launching from inside a loop does
nothing.

### Modules

There is no `require` — `package` is not opened. `sys.import` is the whole
module system:

```lua
local ui = sys.import("/lib/ui.lua")
local keyboard = sys.import("/lib/keyboard.lua")
```

A module is a file that returns a value. It is resolved through the same path
rule as `fs.*` (absolute, or relative to the app's folder), runs in the same
restricted environment as its caller, and is cached per launch — two imports of
one path return the same table, so a module may hold state.

Libraries ship in `/lib` on internal flash, so they are present even with no SD
card. `/lib/keyboard.lua` gives you a text prompt:

```lua
local password = keyboard.prompt{ title = "password", mask = true, max = 63 }
if password then ... end        -- nil means the user backed out
```

It takes over the screen and input until Done or Back. `/lib/ui.lua` has the
navbar, list and icons the launcher draws with, plus `ui.UP` / `ui.DOWN` /
`ui.CONFIRM` / `ui.BACK` — event tables that map both controllers onto one
vocabulary, so a screen never has to know which one is attached.

---

## settings

```lua
settings.get(name)         --> boolean or number
settings.set(name, value)  --> boolean, false if out of range
settings.keys()            --> { {name=, type=, min=, max=}, ... }
```

| key | type | default | |
| --- | --- | --- | --- |
| `wifi_enabled` | bool | true | radio off entirely when false |
| `ftp_enabled` | bool | true | needs `wifi_enabled` to do anything |
| `standby_screen` | bool | false | show a screen when sleeping, or blank |
| `sleep_after_min` | int | 0 | sleep after N idle minutes; 0 never |
| `refresh_every` | int | 8 | full refresh every Nth launcher frame |

`get()` takes no default argument: the kernel declares them, so a script and
the firmware cannot disagree about what an unset key means. An unknown name is
an error, not a nil that would read as "off".

**Setting a value states an intent; it does not perform an action.** The kernel
watches this store and decides what a change means for the hardware — writing
`wifi_enabled = false` is what tears down FTP and powers the radio off. That is
why there is no `sys.wifi_enable()`: connection management stays with the
kernel, which is also why `sys.wifi()` only reports.

Apps store their own state with `fs.write` in their own folder. This table is
for device settings, and its keys are fixed.

---

`sys.info()`:

```lua
{
  api = 1,                    -- this document's version
  version = "0.1",            -- firmware version
  chip, revision, cores, cpu_mhz,
  flash_bytes, psram_bytes, psram_free_bytes,
  heap_bytes, heap_free_bytes, heap_min_free_bytes,
  uptime_ms,
  reset_reason,               -- "brownout" means the supply sagged
}
```

---

## An app

`/sd/apps/<name>/main.lua` is required. `app.lua` beside it is an optional
manifest — plain Lua, because the device already has an interpreter:

```lua
return {
    name = "Snake",
    version = "1.0",
    author = "you",
    api = 1,
}
```

The launcher finds an app by its readable `main.lua`, not by the directory
flag. On the card the app lives at `/apps/<name>/`; `/sd` is a virtual mount
the bindings strip.

```lua
-- main.lua
local W, H = display.size()

display.begin()
display.font("sans")
display.text(20, 20, "hello")
display.show()

while true do
    local event = input.read(30000)
    if event == "gamepad_b" or event == "rotary_select_long" then
        return          -- back to the launcher
    end
end
```
