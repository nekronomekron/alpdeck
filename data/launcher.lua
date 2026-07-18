-- alpdeck launcher
--
-- Just another Lua app -- the one the host runs when nothing else is. It lives
-- on LittleFS rather than the SD card so the device still boots to something
-- usable with no card inserted.
--
-- Apps are discovered at /sd/apps/<name>/main.lua. An optional app.lua beside
-- it returns a table of metadata, e.g.  return { name = "Snake", version = "1.0" }
-- It is parsed with load() rather than a JSON reader: we already have an
-- interpreter, so a manifest is just Lua.

local APPS_DIR = "/sd/apps"
local ENTRY = "main.lua"
local MANIFEST = "app.lua"

local ROW_HEIGHT = 22
local NAV_HEIGHT = 40  -- navbar, closed by a full-width 2px rule
local LIST_TOP = NAV_HEIGHT + 14
local MARGIN = 12

local W, H = display.size()
local VISIBLE = math.floor((H - LIST_TOP - 6) / ROW_HEIGHT)

local VERSION = "v" .. (sys.info().version or "?")

-- Ghosting builds up over partial refreshes; clear it periodically.
local FULL_REFRESH_EVERY = 8

local apps = {}
local selected = 1
local top = 1
local refreshes = 0

local function manifestFor(dir)
    local path = dir .. "/" .. MANIFEST
    if not fs.exists(path) then
        return nil
    end

    local source = fs.read(path)
    if not source then
        return nil
    end

    -- A broken manifest must not take the launcher down, so compile and call it
    -- in protected mode and fall back to the folder name.
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
    apps = {}

    local entries = fs.list(APPS_DIR)
    if not entries then
        sys.log("discover: fs.list(" .. APPS_DIR .. ") returned nil")
        return
    end

    for _, entry in ipairs(entries) do
        -- What makes something an app is a readable main.lua, not the directory
        -- flag. Probing for the file directly is both simpler and robust to a
        -- missing or unreliable `dir` field from the fs binding.
        local dir = APPS_DIR .. "/" .. entry.name
        local entryPath = dir .. "/" .. ENTRY

        if fs.exists(entryPath) then
            local meta = manifestFor(dir) or {}
            apps[#apps + 1] = {
                name = meta.name or entry.name,
                version = meta.version,
                path = entryPath,
                dir = dir,
            }
        end
    end

    sys.log("discover: " .. #apps .. " app(s) found")

    table.sort(apps, function(a, b)
        return a.name:lower() < b.name:lower()
    end)
end

-- Four ascending signal bars, 18px wide in total. Filled count follows the
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

-- Hamburger placeholder for the options menu (menu itself comes later).
local function drawMenuIcon(x, y)
    for i = 0, 2 do
        display.rect(x, y + i * 5, 16, 2, true)
    end
end

local function drawNavbar()
    display.text(MARGIN, 10, "alpdeck", 3)
    display.text(MARGIN + 7 * 18 + 6, 24, VERSION, 1)

    local menuX = W - MARGIN - 16
    drawMenuIcon(menuX, 14)
    drawWifiIcon(menuX - 10 - 18, 28)

    display.rect(0, NAV_HEIGHT, W, 2, true)
end

-- Right-edge scrollbar, only when the list does not fit the screen. The thumb
-- tracks the scroll window, not the selection.
local function drawScrollbar()
    if #apps <= VISIBLE then
        return
    end

    local x = W - 8
    local trackY = LIST_TOP
    local trackH = H - 8 - trackY
    display.rect(x, trackY, 4, trackH)

    local thumbH = math.max(10, math.floor(trackH * VISIBLE / #apps))
    local maxTop = #apps - VISIBLE
    local thumbY = trackY
        + math.floor((trackH - thumbH) * (top - 1) / maxTop)
    display.rect(x, thumbY, 4, thumbH, true)
end

local function drawEmpty()
    display.text(MARGIN, LIST_TOP + 10, "no apps found", 2)
    display.text(MARGIN, LIST_TOP + 36, APPS_DIR .. "/<name>/" .. ENTRY, 1)
    display.text(MARGIN, LIST_TOP + 50, "add apps to the sd card, then press", 1)
    display.text(MARGIN, LIST_TOP + 62, "select to rescan.", 1)
end

local function drawRow(app, index, y)
    local active = index == selected

    -- The selected row is a filled black bar, so its text must draw white
    -- (invert). A non-selected row is black text on white. Getting this wrong is
    -- how the list first rendered black-on-black and looked empty.
    if active then
        display.rect(MARGIN, y - 4, W - 2 * MARGIN, ROW_HEIGHT, true)
    end

    display.text(MARGIN + 6, y, active and ">" or " ", 2, active)
    display.text(MARGIN + 24, y, app.name, 2, active)

    if app.version then
        display.text(W - MARGIN - 6 * #app.version - 6, y + 4, app.version, 1,
            active)
    end
end

local function draw()
    -- Ghosting builds up over partial refreshes, so every FULL_REFRESH_EVERY
    -- frames open the frame in full mode instead. The mode is chosen here, at
    -- clear(), because it is fixed for the life of the frame.
    refreshes = refreshes + 1
    local full = refreshes % FULL_REFRESH_EVERY == 1

    display.clear(full)
    drawNavbar()

    if #apps == 0 then
        drawEmpty()
    else
        for offset = 0, VISIBLE - 1 do
            local index = top + offset
            local app = apps[index]
            if app then
                drawRow(app, index, LIST_TOP + offset * ROW_HEIGHT)
            end
        end

        drawScrollbar()
    end

    display.show()
end

local function moveBy(delta)
    if #apps == 0 then
        return false
    end

    local target = selected + delta
    if target < 1 then
        target = #apps
    elseif target > #apps then
        target = 1
    end

    if target == selected then
        return false
    end
    selected = target

    -- Keep the selection inside the window.
    if selected < top then
        top = selected
    elseif selected >= top + VISIBLE then
        top = selected - VISIBLE + 1
    end
    return true
end

-- Event names carry their source controller (rotary_* / gamepad_*) so apps can
-- tell the two apart. The launcher itself accepts both, so either controller
-- alone can drive it.
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
    local event = input.read(30000)

    if event == nil then
        -- Timeout: redraw so the wifi indicator tracks reality.
        draw()
    elseif MOVE_DOWN[event] then
        if moveBy(1) then draw() end
    elseif MOVE_UP[event] then
        if moveBy(-1) then draw() end
    elseif LAUNCH[event] then
        if #apps == 0 then
            discover()
            draw()
        else
            local app = apps[selected]
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
