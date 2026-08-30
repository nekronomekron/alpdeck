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

from lupa import LuaRuntime

from epaper import Panel
from gfx import BLACK, WHITE

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


class HarnessStop(Exception):
    """Raised to break out of a script's event loop once the fed events run out.

    The launcher loops forever by design -- it only returns when it launches an
    app. Rendering a state therefore means driving it to that state and then
    cutting the loop from underneath, rather than asking the script to end.
    """


# The exact set the firmware installs. Anything outside this is a sandbox hole.
ALLOWED_LIBRARIES = ("table", "string", "math")

# Names luaopen_base installs. dofile/loadfile reach the C stdio and are listed
# here so the conformance check can assert on them deliberately rather than by
# omission.
BASE_NAMES = (
    "assert", "collectgarbage", "dofile", "error", "getmetatable", "ipairs",
    "load", "loadfile", "next", "pairs", "pcall", "print", "rawequal",
    "rawget", "rawlen", "rawset", "select", "setmetatable", "tonumber",
    "tostring", "type", "xpcall", "_VERSION",
)

_BINDINGS_LUA = """
local host = ...

local display = {}
function display.clear(full) host.display_clear(full and true or false) end
function display.text(x, y, s, size, invert) host.display_text(x, y, tostring(s), size or 1, invert and true or false) end
function display.rect(x, y, w, h, fill) host.display_rect(x, y, w, h, fill and true or false) end
function display.circle(x, y, r, fill) host.display_circle(x, y, r, fill and true or false) end
function display.roundrect(x, y, w, h, r, fill) host.display_roundrect(x, y, w, h, r, fill and true or false) end
function display.triangle(x0, y0, x1, y1, x2, y2, fill) host.display_triangle(x0, y0, x1, y1, x2, y2, fill and true or false) end
function display.line(x0, y0, x1, y1) host.display_line(x0, y0, x1, y1) end
function display.pixel(x, y) host.display_pixel(x, y) end
function display.show() host.display_show() end
function display.size() return host.display_width(), host.display_height() end

local input = {}
function input.read(timeout) return host.input_read(timeout or 0) end
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
function sys.memory() return host.sys_lua_bytes(), host.sys_free_heap() end
function sys.temperature() return host.sys_temperature() end
function sys.info() return host.sys_info() end
function sys.wifi() return host.sys_wifi() end

return display, input, fs, sys
"""


class VirtualFs:
    """Mirrors Vfs::resolve -- /sd/... is the card, everything else LittleFS.

    Backed by the repository: data/ is the flash image, sdcard/ is the card, so
    the harness runs against the same launcher and apps that get flashed.
    """

    def __init__(self, flash_dir=None, sd_dir=None):
        self.flash_dir = flash_dir or os.path.join(PROJECT_ROOT, "data")
        self.sd_dir = sd_dir or os.path.join(PROJECT_ROOT, "sdcard")
        self.writes = {}

    def _local(self, path):
        if path == "/sd":
            return self.sd_dir
        if path.startswith("/sd/"):
            return os.path.join(self.sd_dir, path[4:].replace("/", os.sep))
        return os.path.join(self.flash_dir, path.lstrip("/").replace("/", os.sep))

    def list(self, path):
        target = self._local(path)
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
        target = self._local(path)
        if not os.path.isfile(target):
            return None
        with open(target, "r", encoding="utf-8", errors="replace") as handle:
            return handle.read()

    def exists(self, path):
        return os.path.exists(self._local(path))


class Host:
    """Python side of the bindings, mirroring LuaBindings.cpp semantics."""

    def __init__(self, panel=None, vfs=None, events=None, wifi=None,
                 stop_when_drained=False):
        self.panel = panel or Panel()
        self.vfs = vfs or VirtualFs()
        self.events = list(events or [])
        self.stop_when_drained = stop_when_drained
        self.event_log = []
        self.log_lines = []
        self.launch_request = None
        self.exited = False
        self.clock_ms = 0
        self.lua = None  # set by run_script, needed to build Lua tables
        self.wifi_status = wifi if wifi is not None else {"connected": False}
        self.snapshot = {
            "rotary": {"present": True, "select": False, "up": False, "left": False,
                       "down": False, "right": False, "encoder": 0},
            "gamepad": {"present": True, "a": False, "b": False, "x": False, "y": False,
                        "start": False, "select": False, "left": False, "right": False,
                        "up": False, "down": False, "dx": 0, "dy": 0,
                        "stick_x": 512, "stick_y": 512},
        }

    # ------------------------------------------------------------------ display

    def _canvas(self):
        return self.panel.ensure_frame()

    def display_clear(self, full):
        if not self.panel.frame_open:
            self.panel.begin_frame(partial=not full)
        self.panel.canvas.fill_screen(WHITE)

    def display_text(self, x, y, text, size, invert):
        size = max(1, min(8, int(size)))
        canvas = self._canvas()
        canvas.set_text_size(size)
        canvas.set_text_color(WHITE if invert else BLACK)
        canvas.set_cursor(int(x), int(y))
        canvas.print(text)

    def display_rect(self, x, y, w, h, fill):
        canvas = self._canvas()
        if fill:
            canvas.fill_rect(int(x), int(y), int(w), int(h), BLACK)
        else:
            canvas.draw_rect(int(x), int(y), int(w), int(h), BLACK)

    def display_circle(self, x, y, r, fill):
        canvas = self._canvas()
        if fill:
            canvas.fill_circle(int(x), int(y), int(r), BLACK)
        else:
            canvas.draw_circle(int(x), int(y), int(r), BLACK)

    def display_roundrect(self, x, y, w, h, r, fill):
        canvas = self._canvas()
        if fill:
            canvas.fill_round_rect(int(x), int(y), int(w), int(h), int(r), BLACK)
        else:
            canvas.draw_round_rect(int(x), int(y), int(w), int(h), int(r), BLACK)

    def display_triangle(self, x0, y0, x1, y1, x2, y2, fill):
        canvas = self._canvas()
        args = (int(x0), int(y0), int(x1), int(y1), int(x2), int(y2), BLACK)
        if fill:
            canvas.fill_triangle(*args)
        else:
            canvas.draw_triangle(*args)

    def display_line(self, x0, y0, x1, y1):
        self._canvas().draw_line(int(x0), int(y0), int(x1), int(y1), BLACK)

    def display_pixel(self, x, y):
        self._canvas().draw_pixel(int(x), int(y), BLACK)

    def display_show(self):
        self.panel.end_frame()

    def display_width(self):
        return self.panel.width

    def display_height(self):
        return self.panel.height

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
            "version": "0.1",
        })

    def sys_wifi(self):
        return self.lua.table_from(self.wifi_status)


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
    display, input_table, fs_table, sys_table = build(_BINDINGS_LUA)(host)
    env["display"] = display
    env["input"] = input_table
    env["fs"] = fs_table
    env["sys"] = sys_table
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
