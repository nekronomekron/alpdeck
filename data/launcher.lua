-- alpdeck launcher
--
-- Just another Lua app -- the one the host runs when nothing else is. It lives
-- on LittleFS rather than the SD card so the device still boots to something
-- usable with no card inserted.
--
-- Apps are discovered at /sd/apps/<name>/main.lua. An optional app.lua beside
-- it returns a table of metadata; it is parsed with load() rather than a JSON
-- reader, because the device already has an interpreter.
--
-- Sections, in order: layout, model, discovery, drawing, input. Nothing draws
-- outside the drawing section, and nothing mutates the model outside the model
-- section.
--
-- What the launcher is NOT: the modal list screen behind every sub-screen is
-- /lib/menu.lua, the message and busy screens are /lib/dialog.lua, and the
-- options menu with the screens behind its rows is /options.lua. This file is
-- the app list and nothing else.

local ui = sys.import("/lib/ui.lua")
local screen = sys.import("/lib/screen.lua")

local W, H = display.size()
local INFO = sys.info()
local API = INFO.api
local VERSION = "v" .. (INFO.version or "?")

local APPS_DIR = "/sd/apps"
local ENTRY = "main.lua"
local MANIFEST = "app.lua"

local VISIBLE = ui.visibleRows(H)

-- Look at the state again on this cadence even with no input, so the wifi
-- indicator does not go stale on a device sitting idle. Looking is free: the
-- screen only redraws if the icon would actually come out different.
local IDLE_LOOK_MS = 30000

--------------------------------------------------------------------- model --

-- Focus is one index over [menu, app1 .. appN]: MENU_FOCUS is the hamburger,
-- 1 upwards are apps. One rule, no special cases, and it is why moving up from
-- the first app reaches the menu instead of stopping.
local MENU_FOCUS = 0

local state = {
    apps = {},
    focus = 1,
    top = 1,
}

-- Moves focus and scrolls the window to keep it visible. Says nothing about
-- whether anything changed: the render loop compares the state to what is on
-- the panel, so a move that goes nowhere costs no refresh without this having
-- to report it.
--
-- Clamped, not wrapped, at both ends: reaching an end stops there. Wrapping
-- made a long list feel like it had lost your place. Clamping is also what
-- makes a whole turn of the dial safe to apply in one step.
local function moveBy(delta)
    local target = state.focus + delta
    if target < MENU_FOCUS then
        target = MENU_FOCUS
    elseif target > #state.apps then
        target = math.max(MENU_FOCUS, #state.apps)
    end
    state.focus = target
    state.top = ui.scrollTo(state.top, state.focus, VISIBLE)
end

----------------------------------------------------------------- discovery --

local function manifestFor(dir)
    local path = dir .. "/" .. MANIFEST
    if not fs.exists(path) then
        return nil
    end

    local source = fs.read(path)
    if not source then
        return nil
    end

    -- A broken manifest must not take the launcher down, so compile and call
    -- it in protected mode and fall back to the folder name.
    local chunk = load(source, "=" .. path, "t", {})
    if not chunk then
        return nil
    end

    local ok, result = pcall(chunk)
    if ok and type(result) == "table" then
        return result
    end
    return nil
end

local function discover()
    -- Remember where the cursor was, so a rescan does not move it out from
    -- under the user. That is what lets this run after anything -- the options
    -- menu, an SD remount -- instead of only on an explicit rescan.
    local focused = state.apps[state.focus] and state.apps[state.focus].path
    local onMenu = state.focus == MENU_FOCUS

    state.apps = {}
    state.top = 1

    local entries = fs.list(APPS_DIR)
    if not entries then
        sys.log("discover: fs.list(" .. APPS_DIR .. ") returned nil")
        state.focus = MENU_FOCUS
        return
    end

    for _, entry in ipairs(entries) do
        -- What makes something an app is a readable main.lua, not the
        -- directory flag: probing for the file is simpler and survives an
        -- unreliable dir field from the fs binding.
        local dir = APPS_DIR .. "/" .. entry.name
        local entryPath = dir .. "/" .. ENTRY

        if fs.exists(entryPath) then
            local meta = manifestFor(dir) or {}
            state.apps[#state.apps + 1] = {
                name = meta.name or entry.name,
                version = meta.version,
                path = entryPath,
                -- A declared API version that does not match the firmware
                -- means the card was not re-copied after a firmware change.
                -- Left launchable, but the reason it may misbehave is shown.
                stale = meta.api ~= nil and meta.api ~= API,
            }
        end
    end

    sys.log("discover: " .. #state.apps .. " app(s) found")

    table.sort(state.apps, function(a, b)
        return a.name:lower() < b.name:lower()
    end)

    -- With no apps there is nothing to focus but the menu, which is also the
    -- only thing that can fix the situation.
    state.focus = #state.apps > 0 and 1 or MENU_FOCUS

    if onMenu then
        state.focus = MENU_FOCUS
    elseif focused then
        -- By path, not by index: the point of remembering is to survive a list
        -- that gained or lost an app, and an index would not.
        for index, app in ipairs(state.apps) do
            if app.path == focused then
                state.focus = index
                break
            end
        end
    end

    state.top = ui.scrollTo(state.top, state.focus, VISIBLE)
end

------------------------------------------------------------------- drawing --

local function drawEmpty()
    display.text(ui.MARGIN, ui.LIST_TOP + 10, "no apps found", 2)
    display.text(ui.MARGIN, ui.LIST_TOP + 40, APPS_DIR .. "/<name>/" .. ENTRY, 1)
    display.text(ui.MARGIN, ui.LIST_TOP + 54, "add apps to the sd card, then press", 1)
    display.text(ui.MARGIN, ui.LIST_TOP + 66, "select to rescan.", 1)
end

local function drawAppRow(app, y, active)
    display.text(ui.MARGIN + 24, y, app.name, 2)

    local label = app.version or ""
    if app.stale then
        label = (label ~= "" and label .. "  " or "") .. "api!"
    end
    ui.rowValue(label, y, W)
end

-- Draws the whole screen. It does not open or show the frame, and it never
-- decides a refresh mode: screen.run owns both, because only the loop knows whether
-- this frame is a cursor move inside the list or a new screen.
local function draw()
    ui.navbar{
        width = W,
        title = "alpdeck",
        version = VERSION,
        wifi = wifi.status(),
        menuFocused = state.focus == MENU_FOCUS,
    }

    if #state.apps == 0 then
        drawEmpty()
    else
        ui.list{
            items = state.apps,
            selected = state.focus,
            top = state.top,
            visible = VISIBLE,
            width = W,
            height = H,
            render = drawAppRow,
        }
    end
end

-- What the panel is showing, in three parts.
--
-- The split is what buys the cheap refresh. Moving the cursor changes only the
-- third value, and screen.run then repaints the two rows involved -- 435ms against
-- the 609ms a whole panel costs. A scroll or a rescan changes the body, and
-- only the navbar changing drives the whole panel.
--
-- The navbar signs itself with the wifi BAR COUNT rather than the rssi: the raw
-- value drifts by a dBm between reads and would buy a refresh every half minute
-- to redraw an identical icon.
local function signature()
    local radio = wifi.status()
    local chrome = table.concat({
        state.focus == MENU_FOCUS and "menu" or "list",
        radio.enabled and "on" or "off",
        ui.wifiBars(radio),
    }, "|")

    return chrome, table.concat({ state.top, #state.apps }, "|"), state.focus
end

-- Only ever called for a move that left state.top alone, so both the old and
-- the new row are measured against the same window.
local function cursorRect(index)
    return ui.rowRect(index, state.top, W)
end

--------------------------------------------------------------------- input --

-- Nothing here draws. It changes the model and says what should happen to the
-- screen; screen.run decides whether that is worth a refresh and what kind.
local function apply(nav)
    -- Navigation first, always. A digest can carry both a turn and a press, and
    -- the press was made after the turn -- applying them the other way round
    -- would select the row the user was leaving.
    moveBy(screen.steps(nav))

    -- Left is back, and the launcher is where back stops: there is nothing
    -- above it, so it rescans the card instead.
    if nav.dx < 0 then
        discover()
        return
    end

    local action = nav.action
    if action == nil then
        return
    end

    if screen.BACK[action] then
        discover()
    elseif screen.CONFIRM[action] then
        if state.focus == MENU_FOCUS then
            -- Imported here rather than at the top, like the keyboard: someone
            -- who boots and launches an app never pays to parse a screen they
            -- did not open.
            sys.import("/options.lua").run()
            -- Cheap, and the menu can have re-read the card underneath us.
            -- discover() keeps the cursor where it was, so this is invisible
            -- when nothing changed.
            discover()
            return "repaint"
        elseif #state.apps == 0 then
            discover()
        else
            local app = state.apps[state.focus]
            sys.log("launching " .. app.path)
            sys.launch(app.path)
            -- Leaving the loop hands control back to the host, which tears this
            -- state down before starting the app. Never launch from inside it.
            return "close"
        end
    end
end

discover()

screen.run{
    idleMs = IDLE_LOOK_MS,
    body = ui.bodyRegion(H),
    cursorRect = cursorRect,
    signature = signature,
    draw = draw,
    apply = apply,
}
