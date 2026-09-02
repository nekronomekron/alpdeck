# alpdeck — project context

As of 2026-09-02. Handoff document: what this is, how it is put together, and
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
  framebuffer, 15000 bytes, a single page. **Measured** refresh, not the
  driver's datasheet fallbacks: whole-panel partial **609 ms**, full
  **1989 ms**, hibernate **102 ms**, and a flat **41 ms** more on the first
  frame after the panel has hibernated. A partial refresh scales as
  `402 ms + 0.70 ms/row` warm and `443 ms + 0.70 ms/row` cold — parallel
  curves, so waking is a constant offset and not something a smaller window
  avoids. Two thirds of any frame is fixed cost.
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
  core/         the kernel: BootSequence, Settings, Vfs, and lua/
    lua/        LuaHost, LuaWrapper glue, LuaContext, and one file per API
                table: DisplayApi, InputApi, FsApi, SysApi, SettingsApi,
                WifiApi, FtpApi
  peripherals/  Display + PanelPower, Input + InputDigest, Controller and its
                two implementations (RotaryController, GamepadController),
                SeesawButtons, PowerButton
  net/          Network, CaptivePortal, FtpService -- one subsystem, one
                lifecycle
  ui/           Logo, Bootscreen
  utils/        Logger, JsonUtil
  main.cpp      twelve lines; the Arduino framework wants a setup() and a loop()

data/           flashed to LittleFS with `uploadfs`
  boot.lua      user hook, runs before the launcher
  launcher.lua  the launcher: find apps, list them, launch one
  options.lua   the options menu and the screens behind its rows. Imported
                only when the menu is opened
  lib/          shared Lua modules, loaded with sys.import
    ui.lua        navbar, header, footer, list, scrollbar, icons, geometry
    screen.lua    the controller vocabulary and screen.run, the input ->
                  state -> render loop that owns the refresh strategy
    menu.lua      the modal list screen every sub-screen is built on
    dialog.lua    message and busy screens
    keyboard.lua  on-screen keyboard
```

The line between `ui.lua` and `screen.lua` is the line you change along:
widgets change for how something should look, the runtime for what a 609 ms
refresh costs.

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
- **PanelPower** — the panel's lock and its power deadline, split out of
  Display because it is not a drawing question: the panel is a shared device
  with a rail that must not be left up, and the main loop switches it off while
  a Lua app may be mid-refresh. It never touches the panel — Display hands it a
  function that hibernates. Every path to the hardware takes a scoped
  `PanelPower::Lock`; the main loop's is `Wait::Never`, because a busy panel is
  one mid-refresh and blocking there would stall input polling for the length
  of a frame.
- **Settings** — NVS-backed device settings, each key declared in one table
  with its type, default and range. The kernel reads several before any Lua
  runs (whether to bring the radio up at all), which is what rules out a Lua
  file: there is no interpreter yet at that point. Lua writes a value to state
  an intent; a change hook lets BootSequence decide what that means for the
  hardware, so radio management stays in the kernel. Credentials are
  deliberately not in here — a store any app can read is not a place for a
  password.
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

  **The server object belongs to `loop()`, on the main loop, and to nothing
  else.** Everything else states an intent — `setEnabled()`, `requestRebuild()`
  — and `loop()` reconciles. The callers are the settings hook, the connect
  callback and two Lua bindings, and those run on the Lua task while the main
  loop is inside `server->handle()`; deleting it there is a dangling pointer,
  and it was reachable from the options menu.
- **Input** — facade over the controllers. Each implements **Controller**
  (`begin`, `available`, `poll`, `fill`) and the facade holds them in one
  array, so a third device is a class plus one entry rather than a branch in
  `init()` and another in `poll()`. What it still costs: new event names in
  `Event` and `eventName()`, a case in the digest's classification, and
  `Snapshot` fields for any level-triggered state — that is the event
  vocabulary, and no abstraction invents it. Today the array holds
  **RotaryController** (5740) and **GamepadController** (5743);
  **SeesawButtons** is the shared debounce and long-press helper.
  `init()` probes both, true on ≥ 1. `poll()` owns I2C exclusively on the main
  loop; the Lua task consumes what it publishes. The bus is never touched from
  two tasks. Buttons without a long-press fire on **press**; only
  `rotary_select` fires on release, to disambiguate the long press.

  It publishes **two views of the same input**, and which one an app takes
  decides how it behaves when the panel cannot keep up:

  - `read()` — the FreeRTOS queue, every edge in order. For games.
  - `take()` — a **coalesced digest** since the last call, kept by
    **InputDigest**, which is its own translation unit: the facade is a driver
    problem, the digest is a concurrency one. For screens. Relative
    navigation adds up (`dx`, `dy`, `wheel`); a discrete `action` is a **queue
    of one**, so a second press during a refresh is dropped rather than acted
    on later. Navigation arriving *after* an action is held for the next
    digest, which is what keeps a press attached to the row the user was
    actually looking at. The wheel is folded in from the encoder's **absolute
    position**, so coalescing it costs nothing however long a refresh blocked.

  `flush()` drops all of it and rebaselines the encoder. `BootSequence` calls
  it on every app start and launcher return; `screen.run` calls it entering and
  leaving a screen. Without it, detents turned at the old screen land on the
  new one.
- **PowerButton** — press/hold detection, wake confirmation, deep-sleep entry.
  It takes an `onBeforeSleep` callback rather than calling the display itself,
  which is also what stops an early re-sleep drawing to a panel that has not
  been initialised yet.
- **LuaHost** — task supervisor. Exactly **one** `lua_State` and task at a time.
  An app requests the next one with `sys.launch(path)` and *returns*; the host
  tears the state down and only then starts the next. A crash lands back at the
  launcher.
- **Vfs** — the `/sd` vs LittleFS path vocabulary, and the SD **mount** that
  makes it true. `mountSd()` releases an existing mount first, so it doubles as
  the re-read behind `sys.sd_remount()`; `FtpService::start()` asks it whether
  there is a card rather than being told.
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
region refresh), `controllers` (a live schematic of both controllers, drawing
only the ones that are attached) and `bench` (**Timing** — measures what a
frame actually costs on this panel: draw, refresh, power-down and wall clock,
per refresh mode).

All three take their chrome from `/lib/ui.lua` and `/lib/screen.lua`, imported
by **absolute** path because the libraries are on flash and the apps are on the
card. None of them takes `screen.run`, and that is not an oversight: `hello`
reads raw events because it echoes every one of them, `controllers` polls
`input.state()` because a released button produces no event to wake on, and
`bench` opens its own frames because a timing run routed through the shared
loop would be measuring the loop.

## Drawing: input, state, render

Screens do not draw in response to an event. `screen.run` in `lib/screen.lua`
is the loop every list screen uses, and it works one way round only:

1. Ask the screen for a **signature** — three comparable values: the chrome,
   the body, and the cursor — describing what the panel *should* show.
2. Redraw only if that differs from what is on the panel. Body-only change →
   the refresh is confined to the body region. Chrome change → whole-panel
   partial. First frame of a screen, or one in every `refresh_every` → full,
   against ghosting.
3. `input.take()` for a digest, apply it to the state, round again.

The consequence is that nothing is ever redrawn twice on the way to where the
user already is: eight detents turned during a refresh cost one more frame, not
eight. It also removes the idle repaint — the launcher signs its navbar with
the wifi **bar count** rather than the rssi, so looking every 30 s is free and
only an icon that would actually differ buys a refresh.

The keyboard keeps its own loop because its regions are per-cell rather than
per-screen, but it follows the same rule: move the cursor the whole way the
digest says, then draw once.

## Four load-bearing invariants

1. **Exactly one `lua_State` at a time.** Never start one nested.
2. **Cancellation is cooperative** (`lua_sethook`). Never `vTaskDelete` a Lua
   task: it may hold the shared SPI mutex and would block display and SD for
   the whole device.
3. **No `io`, `os`, `package`, `dofile` or `loadfile`.** `fs.*` is the only
   storage access and every path is checked. `sys.import` is the module system
   that replaces `require`: same path rule, cached per launch, same restricted
   environment. The last two matter because both
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
python tools/verify/run.py             # the gate
python tools/verify/run.py --bless
python tools/verify/render_ui.py       # previews
```

`tools/verify/` runs the launcher and the apps through a real Lua 5.5 VM under
the same restricted `_ENV` the firmware builds, renders what the panel would
show through a faithful Adafruit_GFX port, and compares against committed
golden images. See `tools/verify/README.md` for what it does and does not
cover — in particular, it cannot see bugs on the C side of a binding.

`fixtures/` holds scripts that are not shipped, for states the launcher guards
against reaching before it ever gets there — an empty menu, and one holding
nothing but group labels. The empty state of a shared widget is the one nobody
looks at, and both used to be a crash.

**The mock reads what it can out of the firmware rather than restating it.**
The API version comes from `LuaApi.h` and the fonts from the same headers
PlatformIO fetched. Anything hard-coded here is a drift waiting to happen: an
`api = 1` literal in the harness survived a bump in the header and quietly
marked every app on the card stale.

`test/native/` is reserved for `pio test`. It does not run here: only the ESP32
cross-toolchains are installed, and PlatformIO's native platform needs a host
compiler. Installing MSYS2/MinGW-w64 is all that is missing.

There is no CI. The gate is: both environments build, `pio check` is clean over
`src/`, and the harness passes. Run it before every commit.

**A structural change must not move a pixel.** The goldens are what says a
refactor was a refactor; when one does change, bless it on its own and look at
the difference first.

## Current state

**Verified on hardware:**

- Launcher runs on the device and lists apps from the card: display, LittleFS,
  SD mount, Lua host, app discovery and launcher rendering are confirmed
  end-to-end.
- **Both controllers are detected** (rotary at 0x49, gamepad at 0x50), as of
  2026-08-30.
- **The refactored firmware runs on hardware, and the fatal halt works.**
- **The 2026-09-02 restructure runs on hardware**, exercised in three passes:
  boot, launcher, launching an app and coming back, and navigating with both
  the dial and the d-pad; the options menu clicked through end to end (the WiFi
  and FTP toggles, a scan, the keyboard, the SD refresh, restart and device
  info); and FTP served live against `/flash` and `/sd` with the timing bench
  run against it.

  That is the C side of every binding this restructure touched: the FTP
  reconcile on the main loop, `PanelPower::Lock` under real frames, the digest
  after its move into `InputDigest`, and the `wifi.*` / `ftp.*` tables. It also
  answers the stall question — nothing hangs when an app draws while FTP is
  serving.
- **The standby screen and the idle sleep work**, which are the two states that
  can only be reached by leaving the device alone. They are also the only thing
  that exercises `Display::powerDown()` and the deep-sleep entry at all, so this
  is what confirms `PanelPower`'s lock on that path and the `main.cpp` split into
  PowerButton and BootSequence.

**Built and verified off-device only:**

- The captive portal.
- `display.bitmap` in the hello app, and the redrawn logo with its hidden-line
  removal — both are drawn every time the thing they belong to is on screen, so
  they may well be fine; nobody has said so.

## Open points

1. **The deferred power-down is confirmed, its current draw is not.** `power`
   reads 0 across a run of frames and a warm frame is 143 ms cheaper than the
   old regime — 753 ms down to 609 ms for a whole panel (cold 651 plus a 102 ms
   power-down, against a warm frame that pays neither). Standby and idle sleep
   both behave. What none of that measures is the rail actually being down
   afterwards: `hibernate()` returning is not an ammeter. Only worth chasing if
   this ever runs on a battery.
2. **Still unconfirmed from the earlier refactor:** `display.bitmap` in the
   hello app, and the redrawn logo with its hidden-line removal.
3. **32-bit integers and floats in Lua** (from `LUA_32BITS`) — sufficient for
   the launcher and apps, but a deliberate trade against 64-bit and double.
4. **Old committed WiFi credentials** (`IoT`/`05021904`) are still in the git
   history (commit fc1434d). Rotate them if the repository is ever public.
5. **FTP credentials default to alpdeck/alpdeck** until changed in the menu.
   Anyone on the same network can write to flash with the default login.
6. **No native toolchain,** so `test/native/` sits unrun and the C++-drawn
   screens can only be previewed through a ported renderer rather than the real
   code.
