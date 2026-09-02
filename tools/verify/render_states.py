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
BENCH_APP = os.path.join(PROJECT_ROOT, "sdcard", "apps", "bench", "main.lua")

# Not a shipped script: a fixture that reaches a state the launcher guards
# against before it ever gets there. See the file for why it is worth a golden.
FIXTURES = os.path.join(PROJECT_ROOT, "tools", "verify", "fixtures")

WIFI_OFFLINE = {"connected": False}
WIFI_ONLINE = {"connected": True, "ssid": "alpdeck-test", "ip": "192.168.1.42", "rssi": -55}


def _synthetic_card(app_count, stale_every=0):
    """Build a temporary SD tree with N apps, for list and scroll states.

    stale_every marks every Nth app with a mismatched api version, which is
    what a card that was not re-copied after a firmware change looks like.
    """
    root = tempfile.mkdtemp(prefix="alpdeck-card-")
    for index in range(app_count):
        app_dir = os.path.join(root, "apps", "app%02d" % (index + 1))
        os.makedirs(app_dir)
        with open(os.path.join(app_dir, "main.lua"), "w", encoding="utf-8") as handle:
            handle.write("return\n")
        api = 99 if stale_every and (index % stale_every == 0) else 1
        with open(os.path.join(app_dir, "app.lua"), "w", encoding="utf-8") as handle:
            manifest = ('return { name = "Sample App %d", version = "1.%d",'
                        ' api = %d }' % (index + 1, index, api))
            handle.write(manifest + "\n")
    return root


def _empty_card():
    root = tempfile.mkdtemp(prefix="alpdeck-card-")
    os.makedirs(os.path.join(root, "apps"))
    return root


def _run(script_path, events, wifi=WIFI_OFFLINE, sd_dir=None, app_root="",
         settings=None):
    """Run one script the way the host would.

    app_root mirrors what BootSequence sets before a launch: an app is rooted
    at its own directory, the launcher at nothing. Getting this wrong is the
    difference between fs.read("sprite.bin") working and not.
    """
    panel = Panel()
    vfs = luaenv.VirtualFs(sd_dir=sd_dir)
    vfs.sandbox_root = app_root
    host = luaenv.Host(panel=panel, vfs=vfs, events=list(events), wifi=wifi,
                       stop_when_drained=True, settings=settings)
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
        # Up from the first app reaches the menu icon rather than stopping or
        # wrapping to the end -- the focus model, in one picture.
        yield ("launcher-menu-focus",) + _run(
            LAUNCHER, ["rotary_ccw"], wifi=WIFI_ONLINE, sd_dir=many
        )

        # --- the options menu and everything reachable from it ---
        to_menu = ["rotary_ccw", "rotary_select"]

        yield ("menu-options",) + _run(LAUNCHER, to_menu, wifi=WIFI_ONLINE)

        # With the radio off, the rows that depend on it are shown but inert.
        yield ("menu-options-wifi-off",) + _run(
            LAUNCHER, to_menu, settings={"wifi_enabled": False})

        # Scrolled to the bottom group: the check that a header travels with
        # the first row under it instead of the section arriving unlabelled.
        yield ("menu-options-scrolled",) + _run(
            LAUNCHER, to_menu + ["rotary_cw"] * 8, wifi=WIFI_ONLINE)

        # The last selectable row of the options list.
        yield ("menu-device-info",) + _run(
            LAUNCHER, to_menu + ["rotary_cw"] * 9 + ["rotary_select"],
            wifi=WIFI_ONLINE)

        # A move that stays inside the scroll window: the cursor path, where
        # only the two rows involved are refreshed. If ui.rowRect were off by a
        # pixel the row left behind would still be highlighted here.
        yield ("menu-options-cursor",) + _run(
            LAUNCHER, to_menu + ["rotary_cw"] * 3, wifi=WIFI_ONLINE)

        # The device group at the end of the list: the two rows that act on
        # the device rather than on a setting.
        yield ("menu-device-group",) + _run(
            LAUNCHER, to_menu + ["rotary_cw"] * 11, wifi=WIFI_ONLINE)

        # Re-reading the card. The mock's card is always there, so this is the
        # success screen; the failure branch is device-only.
        yield ("menu-sd-refresh",) + _run(
            LAUNCHER, to_menu + ["rotary_cw"] * 10 + ["rotary_select"],
            wifi=WIFI_ONLINE)

        # Row 2: wifi setup, which scans and lists what it found.
        yield ("menu-wifi-scan",) + _run(
            LAUNCHER, to_menu + ["rotary_cw", "rotary_select"], wifi=WIFI_ONLINE)

        # Row 6: ftp login, which is the first screen to raise the keyboard.
        to_keyboard = to_menu + ["rotary_cw"] * 5 + ["rotary_select"]
        yield ("keyboard-letters",) + _run(LAUNCHER, to_keyboard, wifi=WIFI_ONLINE)

        # Down to the action row, right to the layer key, and switch layers.
        yield ("keyboard-symbols",) + _run(
            LAUNCHER,
            to_keyboard + ["gamepad_down"] * 3 + ["gamepad_right", "gamepad_a"],
            wifi=WIFI_ONLINE)

        # Typing: the dial walks the grid in reading order, then select types.
        yield ("keyboard-typed",) + _run(
            LAUNCHER,
            to_keyboard + ["rotary_cw", "rotary_cw", "rotary_select",
                           "rotary_cw", "rotary_select"],
            wifi=WIFI_ONLINE)

        stale = _synthetic_card(4, stale_every=2)
        temporaries.append(stale)
        yield ("launcher-stale-api",) + _run(LAUNCHER, [], sd_dir=stale)

        yield ("app-hello",) + _run(
            HELLO_APP, ["gamepad_right", "gamepad_right", "gamepad_down"],
            app_root="/sd/apps/hello")
        yield ("app-controllers",) + _run(CONTROLLERS_APP, [],
                                          app_root="/sd/apps/controllers")

        # The timing bench. Its numbers are the mock's fiction -- there is no
        # panel to wait for here -- but the results table has to lay out, and a
        # column that has run off the panel edge is exactly the kind of thing
        # only a picture shows.
        yield ("app-bench",) + _run(BENCH_APP, [], app_root="/sd/apps/bench")

        # Menus with nothing selectable in them. Reachable only by asking for
        # them directly, and worth a golden anyway: both used to be a crash, and
        # the empty state of a shared widget is the one nobody looks at.
        yield ("menu-empty",) + _run(os.path.join(FIXTURES, "menu-empty.lua"), [])
        yield ("menu-headers-only",) + _run(
            os.path.join(FIXTURES, "menu-headers-only.lua"), [])
    finally:
        for path in temporaries:
            shutil.rmtree(path, ignore_errors=True)
