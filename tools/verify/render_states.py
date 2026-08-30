"""Renders the launcher and the sample apps to images, state by state.

These are the goldens. They exist because the two worst bugs this project has
had were both invisible to a compiler and to a syntax check: a launcher that
rendered a blank screen, and a selected row drawn black on black. Both show up
instantly in a picture and in nothing else.

Each scenario drives a script with a scripted event list and captures what the
panel would be showing when the events run out.
"""

import os
import shutil
import tempfile

import luaenv
from epaper import Panel

PROJECT_ROOT = luaenv.PROJECT_ROOT
LAUNCHER = os.path.join(PROJECT_ROOT, "data", "launcher.lua")
HELLO_APP = os.path.join(PROJECT_ROOT, "sdcard", "apps", "hello", "main.lua")
CONTROLLERS_APP = os.path.join(PROJECT_ROOT, "sdcard", "apps", "controllers", "main.lua")

WIFI_OFFLINE = {"connected": False}
WIFI_ONLINE = {"connected": True, "ssid": "alpdeck-test", "ip": "192.168.1.42", "rssi": -55}


def _synthetic_card(app_count):
    """Build a temporary SD tree with N apps, for list and scroll states."""
    root = tempfile.mkdtemp(prefix="alpdeck-card-")
    for index in range(app_count):
        app_dir = os.path.join(root, "apps", "app%02d" % (index + 1))
        os.makedirs(app_dir)
        with open(os.path.join(app_dir, "main.lua"), "w", encoding="utf-8") as handle:
            handle.write("return\n")
        with open(os.path.join(app_dir, "app.lua"), "w", encoding="utf-8") as handle:
            handle.write('return { name = "Sample App %d", version = "1.%d" }\n' % (index + 1, index))
    return root


def _empty_card():
    root = tempfile.mkdtemp(prefix="alpdeck-card-")
    os.makedirs(os.path.join(root, "apps"))
    return root


def _run(script_path, events, wifi=WIFI_OFFLINE, sd_dir=None):
    panel = Panel()
    vfs = luaenv.VirtualFs(sd_dir=sd_dir)
    host = luaenv.Host(panel=panel, vfs=vfs, events=list(events), wifi=wifi,
                       stop_when_drained=True)
    ok, error = luaenv.run_file(script_path, host)
    return ok, error, host


def scenarios():
    """Yield (name, ok, error, host) for every golden state."""
    temporaries = []

    try:
        # The real card contents shipped in the repo.
        yield ("launcher-offline",) + _run(LAUNCHER, [])
        yield ("launcher-online",) + _run(LAUNCHER, [], wifi=WIFI_ONLINE)
        yield ("launcher-second-row",) + _run(LAUNCHER, ["rotary_cw"])
        yield ("launcher-gamepad-nav",) + _run(LAUNCHER, ["gamepad_down", "gamepad_up"])

        empty = _empty_card()
        temporaries.append(empty)
        yield ("launcher-no-apps",) + _run(LAUNCHER, [], sd_dir=empty)

        many = _synthetic_card(12)
        temporaries.append(many)
        yield ("launcher-scrolled",) + _run(
            LAUNCHER, ["rotary_cw"] * 9, wifi=WIFI_ONLINE, sd_dir=many
        )
        yield ("launcher-wrap-to-last",) + _run(
            LAUNCHER, ["rotary_ccw"], wifi=WIFI_ONLINE, sd_dir=many
        )

        yield ("app-hello",) + _run(HELLO_APP, ["rotary_cw", "rotary_cw"])
        yield ("app-controllers",) + _run(CONTROLLERS_APP, [])
    finally:
        for path in temporaries:
            shutil.rmtree(path, ignore_errors=True)
