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
-- Sections below, in order: layout, model, discovery, drawing, input. Nothing
-- draws outside the drawing section, and nothing mutates the model outside the
-- model section.

local W, H = display.size()
local INFO = sys.info()
local API = INFO.api
local VERSION = "v" .. (INFO.version or "?")

local APPS_DIR = "/sd/apps"
local ENTRY = "main.lua"
local MANIFEST = "app.lua"

local MARGIN = 12
local NAV_HEIGHT = 40        -- navbar, closed by a full-width 2px rule
local ROW_HEIGHT = 24
local LIST_TOP = NAV_HEIGHT + 14
local VISIBLE = math.floor((H - LIST_TOP - 6) / ROW_HEIGHT)

-- Ghosting builds up over partial refreshes; clear it periodically.
local FULL_REFRESH_EVERY = 8

-- Redraw on this cadence even with no input, so the wifi indicator does not go
-- stale on a device sitting idle.
local IDLE_REDRAW_MS = 30000

--------------------------------------------------------------------- model --

local state = {
    apps = {},
    selected = 1,
    top = 1,
    refreshes = 0,
}

-- Moves the selection, wrapping at both ends, and scrolls the window to keep
-- it visible. Returns true only when something changed, so the caller does not
-- pay 400ms for a refresh with nothing new to show.
local function moveBy(delta)
    if #state.apps == 0 then
        return false
    end

    local target = state.selected + delta
    if target < 1 then
        target = #state.apps
    elseif target > #state.apps then
        target = 1
    end

    if target == state.selected then
        return false
    end
    state.selected = target

    if state.selected < state.top then
        state.top = state.selected
    elseif state.selected >= state.top + VISIBLE then
        state.top = state.selected - VISIBLE + 1
    end
    return true
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
    state.apps = {}
    state.selected = 1
    state.top = 1

    local entries = fs.list(APPS_DIR)
    if not entries then
        sys.log("discover: fs.list(" .. APPS_DIR .. ") returned nil")
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
end

------------------------------------------------------------------- drawing --

-- Four ascending signal bars, 18px wide in total. The filled count follows the
-- RSSI; offline draws all bars hollow with a strike-through.
local function drawWifiIcon(x, baseline)
    local wifi = sys.wifi()
    local bars = 0
    if wifi.connected then
        local rssi = wifi.rssi or -100
        bars = (rssi >= -55 and 4) or (rssi >= -65 and 3)
            or (rssi >= -75 and 2) or 1
    end

    for i = 1, 4 do
        local h = 4 + (i - 1) * 3
        display.rect(x + (i - 1) * 5, baseline - h, 3, h, i <= bars)
    end

    if bars == 0 then
        display.line(x - 1, baseline, x + 18, baseline - 14)
    end
end

-- Hamburger placeholder for the options menu (the menu itself comes later).
local function drawMenuIcon(x, y)
    for i = 0, 2 do
        display.rect(x, y + i * 5, 16, 2, true)
    end
end

local function drawNavbar()
    display.font("bold")
    local titleW = display.measure("alpdeck", 2)
    display.text(MARGIN, 6, "alpdeck", 2)

    display.font("default")
    display.text(MARGIN + titleW + 8, 22, VERSION, 1)

    local menuX = W - MARGIN - 16
    drawMenuIcon(menuX, 14)
    drawWifiIcon(menuX - 10 - 18, 28)

    display.rect(0, NAV_HEIGHT, W, 2, true)
end

local function drawEmpty()
    display.font("bold")
    display.text(MARGIN, LIST_TOP + 8, "no apps found", 2)

    display.font("default")
    display.text(MARGIN, LIST_TOP + 48, APPS_DIR .. "/<name>/" .. ENTRY, 1)
    display.text(MARGIN, LIST_TOP + 64, "add apps to the sd card, then press", 1)
    display.text(MARGIN, LIST_TOP + 76, "select to rescan.", 1)
end

-- The selected row is a filled black bar, so everything on it switches to
-- white ink. Getting this wrong is how the list once rendered black on black
-- and looked empty.
local function drawRow(app, index, y)
    local active = index == state.selected

    if active then
        display.rect(MARGIN, y - 4, W - 2 * MARGIN, ROW_HEIGHT, true)
        display.color("white")
        display.font("default")
        display.text(MARGIN + 6, y + 4, ">", 1)
    end

    display.font("sans")
    display.text(MARGIN + 24, y, app.name, 1)

    display.font("default")
    local label = app.version or ""
    if app.stale then
        label = (label ~= "" and label .. "  " or "") .. "api!"
    end
    if label ~= "" then
        local labelW = display.measure(label, 1)
        display.text(W - MARGIN - 10 - labelW, y + 6, label, 1)
    end

    display.color("black")
end

-- Right-edge scrollbar, only when the list does not fit. The thumb tracks the
-- scroll window, not the selection.
local function drawScrollbar()
    if #state.apps <= VISIBLE then
        return
    end

    local x = W - 8
    local trackY = LIST_TOP
    local trackH = H - 8 - trackY
    display.rect(x, trackY, 4, trackH)

    local thumbH = math.max(10, math.floor(trackH * VISIBLE / #state.apps))
    local maxTop = #state.apps - VISIBLE
    local thumbY = trackY + math.floor((trackH - thumbH) * (state.top - 1) / maxTop)
    display.rect(x, thumbY, 4, thumbH, true)
end

local function draw()
    -- The refresh mode is fixed for the life of the frame, so it is chosen
    -- here at begin() and nowhere else.
    state.refreshes = state.refreshes + 1
    local full = state.refreshes % FULL_REFRESH_EVERY == 1

    display.begin(full and "full" or "partial")
    drawNavbar()

    if #state.apps == 0 then
        drawEmpty()
    else
        for offset = 0, VISIBLE - 1 do
            local index = state.top + offset
            local app = state.apps[index]
            if app then
                drawRow(app, index, LIST_TOP + offset * ROW_HEIGHT)
            end
        end
        drawScrollbar()
    end

    display.show()
end

--------------------------------------------------------------------- input --

-- Event names carry their source controller (rotary_* / gamepad_*) so apps can
-- tell the two apart. The launcher accepts both, so either controller alone
-- can drive it.
local MOVE_DOWN = { rotary_cw = true, rotary_down = true, gamepad_down = true }
local MOVE_UP = { rotary_ccw = true, rotary_up = true, gamepad_up = true }
local LAUNCH = { rotary_select = true, gamepad_a = true, gamepad_start = true }
local RESCAN = {
    rotary_select_long = true,
    rotary_left = true,
    gamepad_select = true,
}

discover()
draw()

while true do
    local event = input.read(IDLE_REDRAW_MS)

    if event == nil then
        draw()  -- timeout: keep the wifi indicator honest
    elseif MOVE_DOWN[event] then
        if moveBy(1) then draw() end
    elseif MOVE_UP[event] then
        if moveBy(-1) then draw() end
    elseif LAUNCH[event] then
        if #state.apps == 0 then
            discover()
            draw()
        else
            local app = state.apps[state.selected]
            sys.log("launching " .. app.path)
            sys.launch(app.path)
            -- Returning hands control back to the host, which tears this state
            -- down before starting the app. Never launch from inside the loop.
            return
        end
    elseif RESCAN[event] then
        discover()
        draw()
    end
end
