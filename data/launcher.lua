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
local LIST_TOP = 74
local MARGIN = 12

local W, H = display.size()
local VISIBLE = math.floor((H - LIST_TOP - 18) / ROW_HEIGHT)

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

local function drawHeader()
    display.text(MARGIN, 18, "alpdeck", 3)
    display.text(MARGIN, 46, "apps | games | tools", 1)
    display.rect(MARGIN, 60, W - 2 * MARGIN, 2, true)
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
    drawHeader()

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

        if #apps > VISIBLE then
            display.text(W - MARGIN - 40, H - 14,
                string.format("%d/%d", selected, #apps), 1)
        end
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

discover()
draw()

while true do
    local event = input.read(30000)

    if event == "cw" or event == "down" then
        if moveBy(1) then draw() end
    elseif event == "ccw" or event == "up" then
        if moveBy(-1) then draw() end
    elseif event == "select" then
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
    elseif event == "select_long" or event == "left" then
        discover()
        draw()
    end
end
