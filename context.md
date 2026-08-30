# alpdeck — project context

As of 2026-08-30. Handoff document: what this is, how it is put together, and
which of it has actually been seen working.

## Overview

alpdeck is an e-paper handheld on an ESP32-S3 that runs applications — apps,
games, tools. **Everything runnable is Lua in a sandbox**, the launcher
included. C++ is the kernel: peripherals, filesystems, network, and the Lua
lifecycle. Lua is the payload.

The Lua API is documented in [docs/LUA_API.md](docs/LUA_API.md), which is the
contract and the file to keep current. Nothing below duplicates it.

## Hardware

- **Board:** LOLIN S3 PRO (ESP32-S3, 16 MB flash, PSRAM).
- **Display:** GDEY042T81, 4.2" e-paper, 400×300, 1 bit (GxEPD2). One
  framebuffer, 15000 bytes, a single page. Partial refresh ~400 ms, full
  ~1200 ms.
- **Input:** two controllers on an I2C STEMMA QT daisy chain, both optional,
  **at least one required** (otherwise the boot stops on the error screen):
  - Adafruit ANO Rotary Navigation Encoder (seesaw, product **5740**, I2C
    **0x49**): 5-way switch plus wheel. Events `rotary_*`.
  - Adafruit Mini I2C Gamepad with seesaw (product **5743**, I2C **0x50**):
    6 buttons (A/B/X/Y/Start/Select) and an analog stick, digitised to
    directions with hysteresis. Events `gamepad_*`.
- **SD card:** shares the SPI bus with the display.
- **Power button** on GPIO 5: hold to sleep, hold to wake. Waking is an EXT0
  wakeup, which is a reset — there is no resume.

Pin source is the board variant's `pins_arduino.h`, not guesswork:

- Display: CS=7, DC=8, RST=2, BUSY=1, SCK=12, MISO=13, MOSI=11.
- SD: CS=46 (the variant's `TF_CS`, confirmed).
- I2C: SDA=9, SCL=10 (variant default, free and broken out on the header).
- ANO switches are **seesaw-side** pins, not ESP32 GPIOs: SELECT=1, UP=2,
  LEFT=3, DOWN=4, RIGHT=5 (active low, pull-up).
- Gamepad pins (seesaw-side, from Adafruit's gamepad_qt example): SELECT=0,
  B=1, Y=2, A=5, X=6, START=16; stick analog X=14, Y=15 (0–1023, centre ~512).

All pins live in `src/config/AppConfig.h`.

**The gamepad is mounted rotated 90° clockwise.** The drivers and the
`gamepad_*` events stay in the board's coordinate frame; un-rotating is the
app's job. Board `(bx, by)` → world `(-by, bx)`, so `gamepad_left` is
physically up and `gamepad_up` is physically right. The silkscreen X/Y/A/B
(top/left/right/bottom) becomes Y top, X right, A bottom, B left.

## Source layout

```
src/
  config/       AppConfig.h -- every pin, timeout and path
  core/         the kernel: BootSequence, Vfs, and lua/
    lua/        LuaHost, LuaWrapper glue, LuaContext, and one file per API
                table: DisplayApi, InputApi, FsApi, SysApi
  peripherals/  Display, Input, RotaryController, GamepadController,
                SeesawButtons, PowerButton
  net/          Network, CaptivePortal, FtpService -- one subsystem, one
                lifecycle
  ui/           Logo, Bootscreen
  utils/        Logger, JsonUtil
  main.cpp      twelve lines; the Arduino framework wants a setup() and a loop()
```

Modules that are genuinely singletons are namespaces with their state in an
anonymous namespace in the .cpp. Things with real per-instance state stay
classes: the two seesaw controllers, and the shared button helper.

## Boot sequence

`BootSequence::run()`, in this order and for these reasons:

1. Serial and the logger.
2. `PowerButton::begin()` — if this boot came from a press too brief to count,
   go straight back to sleep, **before touching any peripheral**.
3. `Display::init()` and the bootscreen. It stays up until the launcher draws
   its first frame; there is deliberately no `Display::shutdown()` before the
   scripts.
4. LittleFS (`formatOnFail=true`, **fatal** on failure), then SD — after the
   display, because that is what called `SPI.begin()` for the shared bus. SD is
   optional; a missing card is a warning.
5. `Input::init()` — **fatal** when no controller answers. The device cannot be
   operated without one, and a launcher nobody can drive is worse than a
   readable error. The Wokwi build continues, since the simulator has no seesaw
   hardware.
6. Network, non-blocking. FTP starts from the connect callback.
7. `LuaHost::init()` (**fatal** on allocation failure).
8. `boot.lua` runs as the user hook, then the launcher, then the chosen app.

**Fatal path:** `fail()` in BootSequence logs, redraws the bootscreen with
`Bootscreen::drawError()` — a warning triangle beside the message in a thin
frame — and halts. Fatal: the LittleFS mount, no input controller, the LuaHost
allocation, and the launcher failing to start (a fresh flash where `uploadfs`
was never run lands here). Deliberately **not** fatal: SD and network, which
leave a usable device, and a launcher that crashes at runtime, where FTP stays
up as the repair route.

`BootSequence::loop()` pumps network, FTP, input, the Lua host and the power
button.

## Module notes

- **Display** — GxEPD2 wrapper. Immediate-mode frames (`beginFrame`/`canvas`/
  `endFrame`) for the Lua bindings, because a script erroring inside a paged
  loop could longjmp straight through C++ destructors. Only safe because the
  panel fits one page. `beginFrame(x, y, w, h)` binds a frame to a rectangle,
  which is what `display.begin`'s region form uses.
- **Vfs** — one path vocabulary: `/sd/...` is the card, everything else
  LittleFS. The only place that mapping exists.
- **Logger** (+ `utils/JsonUtil.h`) — thread-safe: lines are composed into a
  buffer and written as one under a mutex, because the main loop and the Lua
  task log concurrently.
- **Network** — non-blocking WiFi state machine. Credentials in `Preferences`
  (NVS). With none stored it raises the captive portal. `WiFi.begin()` returns
  immediately and `loop()` polls, so nothing blocks the boot.
- **CaptivePortal** — own AP setup portal, no WiFiManager: soft AP, wildcard
  DNS, web server, black-and-white UI. Deduplicates SSIDs server-side
  (strongest BSSID per name).
- **FtpService** — LittleFS as `/flash`, SD as `/sd`. Starts only on a WiFi
  connection. Heap lifecycle per connection cycle: the library has no `stop()`,
  but `~FTPServer` → `~WiFiServer` → `end()` closes the socket cleanly.
- **Input** — facade over **RotaryController** (5740) and **GamepadController**
  (5743); **SeesawButtons** is the shared debounce and long-press helper.
  `init()` probes both, true on ≥ 1. `poll()` owns I2C exclusively on the main
  loop and publishes into a FreeRTOS queue; the Lua task consumes it. The bus is
  never touched from two tasks. Buttons without a long-press fire on **press**;
  only `rotary_select` fires on release, to disambiguate the long press.
- **PowerButton** — press/hold detection, wake confirmation, deep-sleep entry.
  It takes an `onBeforeSleep` callback rather than calling the display itself,
  which is also what stops an early re-sleep drawing to a panel that has not
  been initialised yet.
- **LuaHost** — task supervisor. Exactly **one** `lua_State` and task at a time.
  An app requests the next one with `sys.launch(path)` and *returns*; the host
  tears the state down and only then starts the next. A crash lands back at the
  launcher.
- **LuaContext** — the per-launch state that is not any one table's business:
  the sandbox root and the pending launch request.
- **LuaWrapper** (lib/luawrapper) — instantiated per launch. PSRAM allocator,
  traceback handler, cooperative cancel hook (`lua_sethook`, **never**
  `vTaskDelete`, which would strand the SPI mutex), and `lua_atpanic`, without
  which an API error reboots the device. Opens base, table, string and math
  only, then removes `dofile` and `loadfile` from the base library.

## App format

`/sd/apps/<name>/main.lua` is required, `app.lua` beside it optional — see
[docs/LUA_API.md](docs/LUA_API.md).

**Note:** `/sd` is a virtual mount; on the card the app lives physically at
`/apps/<name>/`. The *contents* of `sdcard/` belong in the card's root. The
launcher recognises an app by a readable `main.lua`, not by the directory flag.

Shipped apps: `hello` (the worked example — relative asset load, a 1bpp sprite,
region refresh) and `controllers` (a live schematic of both controllers, drawing
only the ones that are attached).

## Four load-bearing invariants

1. **Exactly one `lua_State` at a time.** Never start one nested.
2. **Cancellation is cooperative** (`lua_sethook`). Never `vTaskDelete` a Lua
   task: it may hold the shared SPI mutex and would block display and SD for
   the whole device.
3. **No `io`, `os`, `package`, `dofile` or `loadfile`.** `fs.*` is the only
   storage access and every path is checked. The last two matter because both
   mounts are visible to the ESP VFS, so C stdio would walk straight around the
   path sandbox.
4. **The panel fits one page.** `MAX_HEIGHT` resolves to all 300 rows, so a
   single `nextPage()` flushes a frame. A smaller `MAX_DISPLAY_BUFFER_SIZE`
   would silently render only the top slice.

## Non-obvious traps

- **seesaw is vendored** (`lib/seesaw`), not from the registry. The registry
  package pulls ST7735 → `arduino-libraries/SD`, which shadows the ESP32 core's
  SD library and breaks the build. `lib_ignore = SD` does not help — both are
  called `SD`. Do not "fix" this by re-adding it.
- **Lua integer width is a global build flag** (`-DLUA_32BITS`), never a header
  define. A `#define` in a header sets `LUA_INTEGER` to 32 bit only for files
  that include it, while the Lua `.c` files stay at 64: an ABI mismatch where
  every integer crossing the C boundary becomes garbage. That was the "no apps
  found" bug. A `static_assert(sizeof(lua_Integer) == 4)` now guards it.
- **The SD card is not touched by `uploadfs`.** The launcher lives on LittleFS
  and the apps on the card, so they drift. After any change to the API or the
  event names, re-copy `sdcard/` by hand or over FTP. `sys.info().api` and the
  launcher's `api!` marker exist to make that visible instead of mysterious.
- **Off-device testing must use the restricted `_ENV`.** lupa opens the full
  standard library; the device opens four. Testing against lupa's defaults is
  how the `math`-is-nil bug reached hardware.

## Build and flash

```
pio run -e Alpdeck                 # build firmware
pio run -e Alpdeck -t upload       # flash firmware (C++ changes)
pio run -e Alpdeck -t uploadfs     # flash LittleFS (boot.lua, launcher.lua)
pio run -e Alpdeck -t buildfs      # build the FS image only
pio run -e Alpdeck -t factory      # single factory.bin (bootloader + firmware
                                   # + LittleFS) for end users, flashable at
                                   # 0x0. Script: scripts/factory_image.py
```

Two environments: `Alpdeck` (hardware) and `Alpdeck-Wokwi` (simulator; the
portal is compiled out and it joins `Wokwi-GUEST` directly). pio lives at
`~/.platformio/penv/Scripts/pio.exe`.

**Remember:** `upload` never flashes the filesystem. `boot.lua` and
`launcher.lua` always need a separate `uploadfs`. After an erase, LittleFS is
empty.

## Verification

```
~/.platformio/penv/Scripts/python.exe tools/verify/run.py       # the gate
~/.platformio/penv/Scripts/python.exe tools/verify/run.py --bless
~/.platformio/penv/Scripts/python.exe tools/verify/render_ui.py # previews
```

`tools/verify/` runs the launcher and both apps through a real Lua 5.5 VM under
the same restricted `_ENV` the firmware builds, renders what the panel would
show through a faithful Adafruit_GFX port, and compares against committed
golden images. See `tools/verify/README.md` for what it does and does not
cover — in particular, it cannot see bugs on the C side of a binding.

`test/native/` is reserved for `pio test`. It does not run here: only the ESP32
cross-toolchains are installed, and PlatformIO's native platform needs a host
compiler. Installing MSYS2/MinGW-w64 is all that is missing.

There is no CI. The gate is: both environments build, `pio check` is clean over
`src/`, and the harness passes.

## Current state

**Verified on hardware:**

- Launcher runs on the device and lists apps from the card: display, LittleFS,
  SD mount, Lua host, app discovery and launcher rendering are confirmed
  end-to-end.
- **Both controllers are detected** (rotary at 0x49, gamepad at 0x50), as of
  2026-08-30.
- **The refactored firmware runs on hardware, and the fatal halt works.**

**Built and verified off-device only:**

- Everything from the 2026-08-30 refactor: the directory restructure, the
  namespace conversion, the `main.cpp` split into PowerButton and
  BootSequence, the reworked Lua API (fonts, ink, sprites, region refresh,
  `sys.appdir`, `sys.info().api`), the reworked launcher, and the redrawn logo
  and bootscreen.
- Network, captive portal and FTP with `/flash` + `/sd`.

## Open points

1. **Not everything from the refactor has been exercised on hardware.** The
   firmware runs and the fatal halt works. Still unconfirmed on the device:
   `display.begin` with a region, `display.bitmap` in the hello app, and the
   redrawn logo with its hidden-line removal and filled snow caps.
2. **Region refresh timing is a guess.** The ~400 ms figure in the docs is for
   a whole-panel partial refresh. Measure a small region on the device before
   quoting a number to app authors.
3. **32-bit integers and floats in Lua** (from `LUA_32BITS`) — sufficient for
   the launcher and apps, but a deliberate trade against 64-bit and double.
4. **Old committed WiFi credentials** (`IoT`/`05021904`) are still in the git
   history (commit fc1434d). Rotate them if the repository is ever public.
5. **No `require`,** so an app is one file. Relevant once apps get large; the
   embedded-Lua-prelude idea was considered and set aside.
6. **No native toolchain,** so `test/native/` sits unrun and the C++-drawn
   screens can only be previewed through a ported renderer rather than the real
   code.
