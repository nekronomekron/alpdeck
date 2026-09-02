"""The device's Lua sandbox and bindings, reproduced off device.

The environment is built to match LuaWrapper::init exactly: base, table,
string and math via luaL_requiref, and nothing else. lupa opens the full
standard library into its own runtime, so scripts are loaded with an explicit
_ENV containing only what the firmware installs. Testing against lupa's default
globals is how the "math is nil" bug reached hardware in the first place.

What this can and cannot catch:
  CAN  -- launcher/app logic, sandbox escapes, rendering, API misuse.
  CANNOT -- bugs on the C side of a binding. This is real Lua talking to a
            Python reimplementation of the bindings, so a mistake in
            LuaBindings.cpp itself is invisible here.
"""

import os
import re

from lupa import LuaRuntime

from epaper import Panel
from gfx import BLACK, WHITE, load_gfx_font

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def api_version():
    """The API version the firmware would report, read from LuaApi.h.

    Parsed rather than copied. It was a literal here, and bumping it in the
    header without bumping it here made every app on the card look stale to the
    harness and nowhere else -- a drift between the mock and the firmware is
    exactly the failure this harness exists to catch, not to have.
    """
    header = os.path.join(PROJECT_ROOT, "src", "core", "lua", "LuaApi.h")
    with open(header, encoding="utf-8") as handle:
        match = re.search(r"kApiVersion\s*=\s*(\d+)", handle.read())
    if match is None:
        raise AssertionError("kApiVersion not found in %s" % header)
    return int(match.group(1))


class HarnessStop(Exception):
    """Raised to break out of a script's event loop once the fed events run out.

    The launcher loops forever by design -- it only returns when it launches an
    app. Rendering a state therefore means driving it to that state and then
    cutting the loop from underneath, rather than asking the script to end.
    """


# The exact set the firmware installs. Anything outside this is a sandbox hole.
ALLOWED_LIBRARIES = ("table", "string", "math")

# Names luaopen_base installs, minus dofile and loadfile: those reach C stdio
# rather than fs.*, and LuaWrapper nils them out after opening the library.
BASE_NAMES = (
    "assert", "collectgarbage", "error", "getmetatable", "ipairs",
    "load", "next", "pairs", "pcall", "print", "rawequal",
    "rawget", "rawlen", "rawset", "select", "setmetatable", "tonumber",
    "tostring", "type", "xpcall", "_VERSION",
)

_BINDINGS_LUA = """
-- `env` is the sandbox table the script will run in. sys.import needs it: a
-- module must be loaded into the SAME restricted environment as its caller,
-- not into lupa's full-stdlib globals.
local host, env = ...

local display = {}
function display.begin(mode, x, y, w, h)
    if x then
        host.display_begin_region(x, y, w, h)
    else
        host.display_begin(mode or "partial")
    end
end
function display.show() host.display_show() end
function display.size() return host.display_width(), host.display_height() end
function display.timing() return host.display_timing(), host.display_last_power_down() end
function display.power_down() host.display_power_down() end
function display.color(c) host.display_color(c) end
function display.font(name) host.display_font(name or "default") end
function display.clear() host.display_clear() end
function display.text(x, y, s, size) host.display_text(x, y, tostring(s), size or 1) end
function display.measure(s, size) return host.display_measure_w(tostring(s), size or 1), host.display_measure_h(tostring(s), size or 1) end
function display.pixel(x, y) host.display_pixel(x, y) end
function display.line(x0, y0, x1, y1) host.display_line(x0, y0, x1, y1) end
function display.rect(x, y, w, h, fill) host.display_rect(x, y, w, h, fill and true or false) end
function display.circle(x, y, r, fill) host.display_circle(x, y, r, fill and true or false) end
function display.roundrect(x, y, w, h, r, fill) host.display_roundrect(x, y, w, h, r, fill and true or false) end
function display.triangle(x0, y0, x1, y1, x2, y2, fill) host.display_triangle(x0, y0, x1, y1, x2, y2, fill and true or false) end
function display.bitmap(x, y, w, h, data, bg) host.display_bitmap(x, y, w, h, data, bg) end

local input = {}
function input.read(timeout) return host.input_read(timeout or 0) end
function input.take(timeout) return host.input_take(timeout or 0) end
function input.flush() host.input_flush() end
function input.state() return host.input_state() end
function input.controllers() return host.input_controllers() end

local fs = {}
function fs.list(path) return host.fs_list(path) end
function fs.read(path) return host.fs_read(path) end
function fs.exists(path) return host.fs_exists(path) end
function fs.write(path, data) return host.fs_write(path, data) end

local sys = {}
function sys.millis() return host.sys_millis() end
function sys.delay(ms) host.sys_delay(ms) end
function sys.log(msg) host.sys_log(tostring(msg)) end
function sys.launch(path) host.sys_launch(path) end
function sys.exit() host.sys_exit() end
function sys.restart() host.sys_restart() end
function sys.sd_remount() return host.sys_sd_remount() end
function sys.memory() return host.sys_lua_bytes(), host.sys_free_heap() end
function sys.temperature() return host.sys_temperature() end
function sys.info() return host.sys_info() end
function sys.appdir() return host.sys_appdir() end

local wifi = {}
function wifi.status() return host.wifi_status() end
function wifi.scan() return host.wifi_scan() end
function wifi.configure(ssid, pass) host.wifi_configure(ssid, pass or "") end
function wifi.forget() host.wifi_forget() end
function wifi.portal() host.wifi_portal() end

local ftp = {}
function ftp.configure(user, pass) host.ftp_configure(user, pass) end

local modules = {}
function sys.import(path)
    local resolved = host.sys_resolve(path)
    if modules[resolved] ~= nil then
        return modules[resolved]
    end
    local source = host.fs_read(resolved)
    if not source then
        error("sys.import('" .. path .. "'): not found", 2)
    end
    local chunk, err = load(source, "=" .. resolved, "t", env)
    if not chunk then
        error("sys.import('" .. path .. "'): " .. tostring(err), 2)
    end
    local value = chunk()
    modules[resolved] = value
    return value
end

local settings = {}
function settings.get(name) return host.settings_get(name) end
function settings.set(name, value) return host.settings_set(name, value) end
function settings.keys() return host.settings_keys() end

return display, input, fs, sys, settings, wifi, ftp
"""


# Loaded once: the same faces DisplayApi.cpp embeds, parsed from the same
# headers PlatformIO fetched.
_FONTS = None


def fonts():
    global _FONTS
    if _FONTS is None:
        _FONTS = {
            "default": None,
            "sans": load_gfx_font("FreeSans9pt7b"),
            "bold": load_gfx_font("FreeSansBold9pt7b"),
            "pixel": load_gfx_font("Org_01"),
        }
    return _FONTS


# Mirrors the schema declared in src/core/Settings.cpp. Kept in step by hand,
# like everything else in the mock -- a divergence here would show up as a
# launcher that renders a value the device would not.
SETTINGS_SCHEMA = {
    "wifi_enabled": {"type": "bool", "default": True},
    "ftp_enabled": {"type": "bool", "default": True},
    "standby_screen": {"type": "bool", "default": False},
    "sleep_after_min": {"type": "int", "default": 0, "min": 0, "max": 240},
    "refresh_every": {"type": "int", "default": 8, "min": 1, "max": 64},
}

DEFAULT_SCAN = [
    {"ssid": "alpdeck-test", "rssi": -48, "open": False},
    {"ssid": "Gaeststube", "rssi": -61, "open": False},
    {"ssid": "FRITZ!Box 7590", "rssi": -70, "open": False},
    {"ssid": "Freifunk", "rssi": -78, "open": True},
    {"ssid": "hotel-guest", "rssi": -85, "open": True},
]


class VirtualFs:
    """Mirrors Vfs::resolve -- /sd/... is the card, everything else LittleFS.

    Backed by the repository: data/ is the flash image, sdcard/ is the card, so
    the harness runs against the same launcher and apps that get flashed.
    """

    def __init__(self, flash_dir=None, sd_dir=None):
        self.flash_dir = flash_dir or os.path.join(PROJECT_ROOT, "data")
        self.sd_dir = sd_dir or os.path.join(PROJECT_ROOT, "sdcard")
        self.writes = {}
        self.sandbox_root = ""

    def resolve(self, path):
        """Mirrors FsApi::resolve: relative paths hang off the app folder."""
        if ".." in path:
            raise ValueError("'..' is not allowed")
        if path.startswith("/"):
            return path
        if not self.sandbox_root:
            raise ValueError("relative path, but this script has no app directory")
        return self.sandbox_root + "/" + path

    def _local(self, path):
        if path == "/sd":
            return self.sd_dir
        if path.startswith("/sd/"):
            return os.path.join(self.sd_dir, path[4:].replace("/", os.sep))
        return os.path.join(self.flash_dir, path.lstrip("/").replace("/", os.sep))

    def list(self, path):
        target = self._local(self.resolve(path))
        if not os.path.isdir(target):
            return None
        entries = []
        for name in sorted(os.listdir(target)):
            full = os.path.join(target, name)
            entries.append(
                {"name": name, "dir": os.path.isdir(full),
                 "size": os.path.getsize(full) if os.path.isfile(full) else 0}
            )
        return entries

    def read(self, path):
        # Byte-preserving: fs.read on the device hands back raw bytes, and
        # sprite data is not text. latin-1 maps every byte to one code point,
        # so the value survives the trip through Lua and back.
        #
        # LIMIT: lupa re-encodes as UTF-8 on the way into Lua, so a Lua-side
        # `#data` on binary content reports more than the device would. Nothing
        # in the launcher or the sample apps does that; the length check that
        # matters happens on the Python side, where it is exact.
        target = self._local(self.resolve(path))
        if not os.path.isfile(target):
            return None
        with open(target, "rb") as handle:
            return handle.read().decode("latin-1")

    def exists(self, path):
        return os.path.exists(self._local(path))


class Host:
    """Python side of the bindings, mirroring LuaBindings.cpp semantics."""

    def __init__(self, panel=None, vfs=None, events=None, wifi=None,
                 stop_when_drained=False, settings=None, scan_results=None):
        self.panel = panel or Panel()
        self.vfs = vfs or VirtualFs()
        self.events = list(events or [])
        self.stop_when_drained = stop_when_drained
        self.event_log = []
        self.log_lines = []
        self.launch_request = None
        self.exited = False
        self.restarted = False
        self.sd_remounts = 0
        self.power_downs = 0
        self.clock_ms = 0
        self.ink = BLACK
        self.settings = dict(settings or {})
        self.scan_results = list(scan_results if scan_results is not None
                                 else DEFAULT_SCAN)
        self.configured_wifi = None
        self.configured_ftp = None
        self.portal_active = False
        self.lua = None  # set by run_script, needed to build Lua tables
        self.wifi_state = wifi if wifi is not None else {"connected": False}
        self.snapshot = {
            "rotary": {"present": True, "select": False, "up": False, "left": False,
                       "down": False, "right": False, "encoder": 0},
            "gamepad": {"present": True, "a": False, "b": False, "x": False, "y": False,
                        "start": False, "select": False, "left": False, "right": False,
                        "up": False, "down": False, "dx": 0, "dy": 0,
                        "stick_x": 512, "stick_y": 512},
        }

    # ------------------------------------------------------------------ display
    #
    # Mirrors DisplayApi.cpp, including the two things apps actually notice:
    # the ink is stateful, and text() positions by the top-left corner for
    # every font rather than by the baseline.

    def _canvas(self):
        return self.panel.ensure_frame()

    def display_begin(self, mode):
        if not self.panel.frame_open:
            self.panel.begin_frame(partial=(mode != "full"))
        self.ink = BLACK
        self.panel.canvas.set_font(None)

    def display_begin_region(self, x, y, w, h):
        if not self.panel.frame_open:
            self.panel.begin_region(int(x), int(y), int(w), int(h))
        self.ink = BLACK
        self.panel.canvas.set_font(None)

    def display_show(self):
        self.panel.end_frame()

    def display_width(self):
        return self.panel.width

    def display_height(self):
        return self.panel.height

    def display_timing(self):
        # The mock has no panel to wait for, so this is the measured cost model
        # applied to the window that was actually refreshed. See Panel.
        return self.panel.last_refresh_ms

    def display_last_power_down(self):
        # Zero, and correct rather than a stand-in: the device defers the
        # hibernate to the main loop, so a run of frames reports no power-down
        # there either.
        return 0

    def display_power_down(self):
        # The panel model adds the measured wake cost to the next frame, so a
        # cold frame reads higher here the way it does on the device. The
        # power-down itself is not modelled: 102ms of main-loop time is real on
        # hardware and means nothing in a harness with no clock to spend it on.
        self.power_downs += 1
        self.panel.hibernated = True

    def display_color(self, name):
        self.ink = WHITE if name == "white" else BLACK

    def display_font(self, name):
        table = fonts()
        if name not in table:
            raise ValueError("unknown font '%s'" % name)
        self._canvas().set_font(table[name])

    def display_clear(self):
        self._canvas().fill_screen(self.ink)

    def _text_metrics(self, text, size):
        canvas = self._canvas()
        canvas.set_text_size(size)
        _x1, y1, width, height = canvas.get_text_bounds(text, 0, 0)
        return width, height, y1

    def display_text(self, x, y, text, size):
        size = max(1, min(8, int(size)))
        _w, _h, top_offset = self._text_metrics(text, size)
        canvas = self._canvas()
        canvas.set_text_color(self.ink)
        canvas.set_cursor(int(x), int(y) - top_offset)
        canvas.print(text)

    def display_measure_w(self, text, size):
        return self._text_metrics(text, max(1, min(8, int(size))))[0]

    def display_measure_h(self, text, size):
        return self._text_metrics(text, max(1, min(8, int(size))))[1]

    def display_pixel(self, x, y):
        self._canvas().draw_pixel(int(x), int(y), self.ink)

    def display_line(self, x0, y0, x1, y1):
        self._canvas().draw_line(int(x0), int(y0), int(x1), int(y1), self.ink)

    def display_rect(self, x, y, w, h, fill):
        canvas = self._canvas()
        args = (int(x), int(y), int(w), int(h), self.ink)
        canvas.fill_rect(*args) if fill else canvas.draw_rect(*args)

    def display_circle(self, x, y, r, fill):
        canvas = self._canvas()
        args = (int(x), int(y), int(r), self.ink)
        canvas.fill_circle(*args) if fill else canvas.draw_circle(*args)

    def display_roundrect(self, x, y, w, h, r, fill):
        canvas = self._canvas()
        args = (int(x), int(y), int(w), int(h), int(r), self.ink)
        canvas.fill_round_rect(*args) if fill else canvas.draw_round_rect(*args)

    def display_triangle(self, x0, y0, x1, y1, x2, y2, fill):
        canvas = self._canvas()
        args = (int(x0), int(y0), int(x1), int(y1), int(x2), int(y2), self.ink)
        canvas.fill_triangle(*args) if fill else canvas.draw_triangle(*args)

    def display_bitmap(self, x, y, w, h, data, background):
        w, h = int(w), int(h)
        if w <= 0 or h <= 0:
            raise ValueError("bitmap size must be positive, got %dx%d" % (w, h))

        payload = data.encode("latin-1") if isinstance(data, str) else bytes(data)
        expected = ((w + 7) // 8) * h
        if len(payload) != expected:
            raise ValueError("bitmap data is %d bytes, %dx%d needs %d"
                             % (len(payload), w, h, expected))

        bg = None
        if background is not None:
            bg = WHITE if background == "white" else BLACK
        self._canvas().draw_bitmap(int(x), int(y), payload, w, h, self.ink, bg)

    # -------------------------------------------------------------------- input

    def input_read(self, timeout_ms):
        timeout_ms = max(0, int(timeout_ms))
        if self.events:
            event = self.events.pop(0)
            self.event_log.append(event)
            return event
        if self.stop_when_drained:
            raise HarnessStop()
        # Empty queue with a timeout is the launcher's periodic redraw path.
        self.clock_ms += timeout_ms
        return None

    # Mirrors Input::take, including the rule that an action closes the digest:
    # navigation queued behind one is left for the next call. That is what keeps
    # "turn, press, turn, press" two keystrokes here exactly as on the device --
    # draining the whole scripted list into one digest would silently collapse
    # every scenario that types more than one character.
    NAV_STEPS = {
        "rotary_up": (0, -1, 0), "gamepad_up": (0, -1, 0),
        "rotary_down": (0, 1, 0), "gamepad_down": (0, 1, 0),
        "rotary_left": (-1, 0, 0), "gamepad_left": (-1, 0, 0),
        "rotary_right": (1, 0, 0), "gamepad_right": (1, 0, 0),
        "rotary_cw": (0, 0, 1), "rotary_ccw": (0, 0, -1),
    }

    def input_take(self, timeout_ms):
        dx = dy = wheel = 0
        action = None

        while self.events and action is None:
            event = self.events.pop(0)
            self.event_log.append(event)
            step = self.NAV_STEPS.get(event)
            if step is None:
                action = event
            else:
                dx, dy, wheel = dx + step[0], dy + step[1], wheel + step[2]

        if action is None and (dx, dy, wheel) == (0, 0, 0):
            if self.stop_when_drained:
                raise HarnessStop()
            # An empty digest with a timeout is a screen coming round to look at
            # its own state again -- the launcher's wifi indicator path.
            self.clock_ms += max(0, int(timeout_ms))
            return None

        digest = {"dx": dx, "dy": dy, "wheel": wheel}
        if action is not None:
            digest["action"] = action
        return self.lua.table_from(digest, recursive=True)

    def input_flush(self):
        # Deliberately does nothing. The harness feeds a script of what the user
        # meant to do, not a hardware buffer, and dropping the rest of it here
        # would stop every scenario at its first context switch.
        #
        # LIMIT: a flush this code forgot to make is therefore invisible here.
        # Stale input arriving on a fresh screen is a device-only bug.
        pass

    def sys_restart(self):
        # ESP.restart() never returns, so neither may this: letting the script
        # carry on past a reboot would exercise a path the device cannot reach.
        self.restarted = True
        raise HarnessStop()

    def sys_sd_remount(self):
        # The virtual card is a directory that is always there. This can
        # therefore only ever confirm that the call is wired up and that the
        # screen around it renders -- an absent or unreadable card is a
        # device-only state.
        self.sd_remounts += 1
        return True

    def input_state(self):
        return self.lua.table_from(self.snapshot, recursive=True)

    def input_controllers(self):
        return self.lua.table_from(
            {"rotary": self.snapshot["rotary"]["present"],
             "gamepad": self.snapshot["gamepad"]["present"]}
        )

    # ----------------------------------------------------------------------- fs

    def fs_list(self, path):
        entries = self.vfs.list(path)
        if entries is None:
            return None
        return self.lua.table_from([self.lua.table_from(e) for e in entries])

    def fs_read(self, path):
        return self.vfs.read(path)

    def fs_exists(self, path):
        return self.vfs.exists(path)

    def fs_write(self, path, data):
        self.vfs.writes[path] = data
        return True

    # ---------------------------------------------------------------------- sys

    def sys_millis(self):
        self.clock_ms += 1
        return self.clock_ms

    def sys_delay(self, ms):
        self.clock_ms += max(0, int(ms))

    def sys_log(self, message):
        self.log_lines.append(message)

    def sys_launch(self, path):
        self.launch_request = path

    def sys_exit(self):
        self.exited = True

    def sys_lua_bytes(self):
        return 24000

    def sys_free_heap(self):
        return 180000

    def sys_temperature(self):
        return 41.5

    def sys_info(self):
        return self.lua.table_from({
            "chip": "ESP32-S3", "revision": 0, "cores": 2, "cpu_mhz": 240,
            "flash_bytes": 16777216, "psram_bytes": 8388608,
            "psram_free_bytes": 8000000, "heap_bytes": 327680,
            "heap_free_bytes": 180000, "heap_min_free_bytes": 150000,
            "uptime_ms": self.clock_ms, "reset_reason": "poweron",
            "version": "0.1", "api": api_version(),
        })

    def wifi_status(self):
        status = dict(self.wifi_state)
        status["enabled"] = self.settings_get("wifi_enabled")
        status["portal"] = self.portal_active
        return self.lua.table_from(status)

    def sys_appdir(self):
        return self.vfs.sandbox_root

    def sys_resolve(self, path):
        """Mirrors FsApi::resolvePath, which sys.import shares with fs.*."""
        return self.vfs.resolve(path)

    def wifi_scan(self):
        return self.lua.table_from(
            [self.lua.table_from(network) for network in self.scan_results])

    def wifi_configure(self, ssid, password):
        # Write-only on the device; here it is recorded so a test can assert
        # what the flow submitted without the value ever being readable in Lua.
        self.configured_wifi = (ssid, password)
        self.wifi_state = {"connected": True, "ssid": ssid,
                            "ip": "192.168.1.42", "rssi": -55}

    def wifi_forget(self):
        self.configured_wifi = None
        self.wifi_state = {"connected": False}

    def wifi_portal(self):
        self.portal_active = True

    def ftp_configure(self, user, password):
        self.configured_ftp = (user, password)

    # ----------------------------------------------------------------- settings

    def settings_get(self, name):
        key = SETTINGS_SCHEMA.get(name)
        if key is None:
            raise ValueError("unknown setting '%s'" % name)
        value = self.settings.get(name, key["default"])
        return bool(value) if key["type"] == "bool" else int(value)

    def settings_set(self, name, value):
        key = SETTINGS_SCHEMA.get(name)
        if key is None:
            raise ValueError("unknown setting '%s'" % name)
        if key["type"] == "bool":
            self.settings[name] = bool(value)
            return True
        value = int(value)
        if value < key["min"] or value > key["max"]:
            return False
        self.settings[name] = value
        return True

    def settings_keys(self):
        rows = []
        for name, key in SETTINGS_SCHEMA.items():
            row = {"name": name, "type": key["type"]}
            if key["type"] == "int":
                row["min"] = key["min"]
                row["max"] = key["max"]
            rows.append(self.lua.table_from(row))
        return self.lua.table_from(rows)


def build_environment(lua, host):
    """Create the restricted _ENV the firmware gives a script."""
    globals_table = lua.globals()

    env = lua.table()
    for name in BASE_NAMES:
        env[name] = globals_table[name]
    for name in ALLOWED_LIBRARIES:
        env[name] = globals_table[name]
    env["_G"] = env

    build = lua.eval("function(src) return assert(load(src, '=bindings', 't')) end")
    tables = build(_BINDINGS_LUA)(host, env)
    (display, input_table, fs_table, sys_table, settings_table, wifi_table,
     ftp_table) = tables
    env["display"] = display
    env["input"] = input_table
    env["fs"] = fs_table
    env["sys"] = sys_table
    env["settings"] = settings_table
    env["wifi"] = wifi_table
    env["ftp"] = ftp_table
    return env


def run_script(source, name, host):
    """Load and run a script under the restricted environment.

    Returns (ok, error_message). A HarnessStop counts as success: it means the
    script was still running when the harness cut off its event supply.
    """
    lua = LuaRuntime(unpack_returned_tuples=True)
    host.lua = lua
    env = build_environment(lua, host)

    # tostring(err) keeps the return arity at two: lupa cannot unpack a single
    # value, and a successful load returns only the function.
    loader = lua.eval(
        "function(src, name, env) local f, e = load(src, name, 't', env)"
        " return f, tostring(e) end"
    )
    chunk, error = loader(source, "@" + name, env)
    if chunk is None:
        return False, str(error)

    # Deliberately no Lua-side pcall: HarnessStop has to reach Python, and a
    # real failure is more useful with its traceback intact.
    try:
        chunk()
    except HarnessStop:
        return True, None
    except Exception as failure:
        return False, str(failure)
    return True, None


def run_file(path, host):
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return run_script(handle.read(), os.path.basename(path), host)
