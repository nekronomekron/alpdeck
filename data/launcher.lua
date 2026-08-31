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
-- Sections, in order: layout, model, discovery, drawing, screens, input.
-- Nothing draws outside the drawing section, and nothing mutates the model
-- outside the model section.

local ui = sys.import("/lib/ui.lua")

local W, H = display.size()
local INFO = sys.info()
local API = INFO.api
local VERSION = "v" .. (INFO.version or "?")

local APPS_DIR = "/sd/apps"
local ENTRY = "main.lua"
local MANIFEST = "app.lua"

local VISIBLE = ui.visibleRows(H)

-- Redraw on this cadence even with no input, so the wifi indicator does not go
-- stale on a device sitting idle.
local IDLE_REDRAW_MS = 30000

--------------------------------------------------------------------- model --

-- Focus is one index over [menu, app1 .. appN]: MENU_FOCUS is the hamburger,
-- 1 upwards are apps. One rule, no special cases, and it is why moving up from
-- the first app reaches the menu instead of stopping.
local MENU_FOCUS = 0

local state = {
    apps = {},
    focus = 1,
    top = 1,
    refreshes = 0,
}

-- Moves focus and scrolls the window to keep it visible. Returns true only when
-- something changed, so the caller does not pay 400ms for a refresh with
-- nothing new to show.
--
-- Clamped, not wrapped, at both ends: reaching an end stops there. Wrapping
-- made a long list feel like it had lost your place.
local function moveBy(delta)
    local target = state.focus + delta
    if target < MENU_FOCUS then
        target = MENU_FOCUS
    elseif target > #state.apps then
        target = math.max(MENU_FOCUS, #state.apps)
    end

    if target == state.focus then
        return false
    end
    state.focus = target

    if state.focus >= 1 then
        if state.focus < state.top then
            state.top = state.focus
        elseif state.focus >= state.top + VISIBLE then
            state.top = state.focus - VISIBLE + 1
        end
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

local function draw()
    -- The refresh mode is fixed for the life of the frame, so it is chosen here
    -- at begin() and nowhere else. How often a full refresh happens is a
    -- setting, because how quickly ghosting builds up depends on the panel.
    state.refreshes = state.refreshes + 1
    local every = settings.get("refresh_every")
    local full = state.refreshes % every == 1

    display.begin(full and "full" or "partial")

    ui.navbar{
        width = W,
        title = "alpdeck",
        version = VERSION,
        wifi = sys.wifi(),
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

    display.show()
end

------------------------------------------------------------------- screens --

-- A modal list screen: draws itself, runs its own loop, returns when the user
-- backs out. Every sub-screen in the launcher is one of these, so they all
-- behave the same way.
--
-- opts: title, items (each {label=, value=, action=, disabled=}), footer
local function runMenu(opts)
    local listTop = ui.LIST_TOP
    local visible = ui.visibleRows(H - 24, listTop)

    -- Start on the first row that is not a group label. ui.list never marks a
    -- header active because `selected` never points at one.
    local selected = opts.items[1].header
        and ui.nextSelectable(opts.items, 1, 1) or 1
    local top = 1

    local function render(item, y, active)
        if item.header then
            ui.groupHeader(item.label, y, W)
            return
        end

        display.text(ui.MARGIN + 24, y, item.label, 2)
        local value = item.value and item.value() or nil
        if item.disabled and item.disabled() then
            value = value and (value .. "  --") or "--"
        end
        ui.rowValue(value, y, W)
    end

    local function paint(full)
        display.begin(full and "full" or "partial")
        ui.header(opts.title, W)
        ui.list{
            items = opts.items,
            selected = selected,
            top = top,
            visible = visible,
            width = W,
            height = H - 24,
            render = render,
        }
        ui.footer(opts.footer or "select to change   long-press / B to go back",
            W, H)
        display.show()
    end

    paint(true)

    while true do
        local event = input.read(120000)

        if event == nil or ui.BACK[event] then
            return
        elseif ui.DOWN[event] or ui.UP[event] then
            local target = ui.nextSelectable(opts.items, selected,
                ui.DOWN[event] and 1 or -1)
            if target then
                selected = target
                if selected < top then
                    top = selected
                elseif selected >= top + visible then
                    top = selected - visible + 1
                end
                -- Scrolling up onto the first row of a group brings its label
                -- along, otherwise the section arrives unlabelled. Only when
                -- the selection is at the top of the window: shifting it in
                -- any other case would push the selection off the bottom.
                if selected == top and top > 1 and opts.items[top - 1].header then
                    top = top - 1
                end
                paint(false)
            end
        elseif ui.CONFIRM[event] or ui.LEFT[event] or ui.RIGHT[event] then
            local item = opts.items[selected]
            if item and item.action and not (item.disabled and item.disabled()) then
                -- The action owns the screen while it runs, so repaint fully
                -- afterwards rather than assuming anything survived.
                if item.action(ui.LEFT[event] and -1 or 1) == "close" then
                    return
                end
                paint(true)
            end
        end
    end
end

-- A full-screen message with a single way out. Used for results and for the
-- things that take long enough to need saying.
local function showMessage(title, lines, footer)
    display.begin("full")
    ui.header(title, W)
    for index, line in ipairs(lines) do
        display.text(ui.MARGIN, ui.LIST_TOP + (index - 1) * 14, line, 1)
    end
    ui.footer(footer or "any key to go back", W, H)
    display.show()

    input.read(120000)
end

-- Drawn before a blocking call, so the device does not look wedged during the
-- two to four seconds a scan takes.
local function showBusy(title, text)
    display.begin("partial")
    ui.header(title, W)
    display.text(ui.MARGIN, ui.LIST_TOP + 10, text, 2)
    display.show()
end

local function wifiSetup()
    if not settings.get("wifi_enabled") then
        showMessage("wifi setup", { "wifi is switched off.",
            "turn it on first." })
        return
    end

    showBusy("wifi setup", "scanning...")

    local ok, networks = pcall(sys.wifi_scan)
    if not ok or not networks or #networks == 0 then
        showMessage("wifi setup", { "no networks found.",
            "move closer and try again." })
        return
    end

    local items = {}
    for _, network in ipairs(networks) do
        items[#items + 1] = {
            label = network.ssid,
            value = function()
                return (network.open and "open  " or "") .. network.rssi .. "dBm"
            end,
            action = function()
                local password = ""
                if not network.open then
                    local keyboard = sys.import("/lib/keyboard.lua")
                    password = keyboard.prompt{
                        title = network.ssid,
                        mask = true,
                        max = 63,
                    }
                    if password == nil then
                        return  -- cancelled: back to the network list
                    end
                end

                sys.wifi_configure(network.ssid, password)
                showMessage("wifi setup", {
                    "connecting to " .. network.ssid .. "...",
                    "",
                    "this takes a few seconds. the",
                    "signal icon shows the result.",
                })
                return "close"
            end,
        }
    end

    runMenu{
        title = "choose a network",
        items = items,
        footer = "select to join   long-press / B to go back",
    }
end

local function ftpLogin()
    local keyboard = sys.import("/lib/keyboard.lua")

    local user = keyboard.prompt{ title = "ftp user", value = "alpdeck", max = 31 }
    if user == nil or user == "" then
        return
    end

    local password = keyboard.prompt{ title = "ftp password", mask = true, max = 31 }
    if password == nil or password == "" then
        return
    end

    sys.ftp_configure(user, password)
    showMessage("ftp login", { "saved. the server restarts with",
        "the new login." })
end

local function deviceInfo()
    local info = sys.info()
    local wifi = sys.wifi()
    local luaBytes, freeHeap = sys.memory()

    local lines = {
        string.format("%s rev %d, %d cores @ %d MHz", info.chip, info.revision,
            info.cores, info.cpu_mhz),
        string.format("firmware %s   api %d", info.version, info.api),
        "",
        string.format("heap    %d free of %d, low %d", info.heap_free_bytes,
            info.heap_bytes, info.heap_min_free_bytes),
        string.format("psram   %d free of %d", info.psram_free_bytes,
            info.psram_bytes),
        string.format("flash   %d bytes", info.flash_bytes),
        string.format("lua     %d bytes in this script", luaBytes),
        "",
        string.format("uptime  %d s", info.uptime_ms // 1000),
        string.format("temp    %.1f C (die, not room)", sys.temperature()),
        string.format("boot    %s", info.reset_reason),
        "",
    }

    if not wifi.enabled then
        lines[#lines + 1] = "wifi    switched off"
    elseif wifi.connected then
        lines[#lines + 1] = string.format("wifi    %s", wifi.ssid or "?")
        lines[#lines + 1] = string.format("ip      %s   %d dBm", wifi.ip or "?",
            wifi.rssi or 0)
    elseif wifi.portal then
        lines[#lines + 1] = "wifi    setup portal is up"
    else
        lines[#lines + 1] = "wifi    not connected"
    end

    showMessage("device", lines)
end

-- Steps a numeric setting through a list of sensible values rather than one
-- unit at a time: nobody wants to press right thirty times.
local function cycle(key, choices, direction)
    local current = settings.get(key)
    local index = 1
    for position, value in ipairs(choices) do
        if value == current then
            index = position
            break
        end
    end

    index = index + (direction or 1)
    if index < 1 then
        index = #choices
    elseif index > #choices then
        index = 1
    end

    settings.set(key, choices[index])
end

local function onOff(key)
    return function()
        return settings.get(key) and "on" or "off"
    end
end

local function toggle(key)
    return function()
        settings.set(key, not settings.get(key))
    end
end

local SLEEP_CHOICES = { 0, 5, 15, 30 }
local REFRESH_CHOICES = { 1, 4, 8, 16, 32 }

local function needsWifi()
    return not settings.get("wifi_enabled")
end

-- Grouped, because ten flat rows of unrelated switches read as a wall. The
-- group label carries the subject, so the rows under it can drop the prefix
-- and say what they actually do: "enabled" under WIFI beats "wifi" twice.
local function optionsMenu()
    runMenu{
        title = "options",
        items = {
            { header = true, label = "wifi" },
            {
                label = "enabled",
                value = onOff("wifi_enabled"),
                action = toggle("wifi_enabled"),
            },
            {
                label = "setup",
                action = function() wifiSetup() end,
                disabled = needsWifi,
            },
            {
                label = "setup portal",
                action = function()
                    sys.wifi_portal()
                    showMessage("setup portal", {
                        "the alpdeck access point is up.",
                        "join it from a phone or laptop",
                        "to choose a network.",
                    })
                end,
            },
            {
                label = "forget network",
                action = function()
                    sys.wifi_forget()
                    showMessage("wifi", { "stored network forgotten." })
                end,
            },

            { header = true, label = "ftp" },
            {
                label = "enabled",
                value = onOff("ftp_enabled"),
                action = toggle("ftp_enabled"),
                -- Shown but inert while the radio is off: hiding it would just
                -- raise the question of whether the firmware still has FTP.
                disabled = needsWifi,
            },
            {
                label = "login",
                action = function() ftpLogin() end,
                disabled = needsWifi,
            },

            { header = true, label = "power" },
            {
                label = "sleep after",
                value = function()
                    local minutes = settings.get("sleep_after_min")
                    return minutes == 0 and "never" or (minutes .. " min")
                end,
                action = function(direction)
                    cycle("sleep_after_min", SLEEP_CHOICES, direction)
                end,
            },
            {
                label = "standby screen",
                value = onOff("standby_screen"),
                action = toggle("standby_screen"),
            },

            { header = true, label = "general" },
            {
                label = "full refresh",
                value = function()
                    return "every " .. settings.get("refresh_every")
                end,
                action = function(direction)
                    cycle("refresh_every", REFRESH_CHOICES, direction)
                end,
            },
            {
                label = "device info",
                action = function() deviceInfo() end,
            },
        },
    }
end

--------------------------------------------------------------------- input --

discover()
draw()

while true do
    local event = input.read(IDLE_REDRAW_MS)

    if event == nil then
        draw()  -- timeout: keep the wifi indicator honest
    elseif ui.DOWN[event] then
        if moveBy(1) then draw() end
    elseif ui.UP[event] then
        if moveBy(-1) then draw() end
    elseif ui.CONFIRM[event] then
        if state.focus == MENU_FOCUS then
            optionsMenu()
            draw()
        elseif #state.apps == 0 then
            discover()
            draw()
        else
            local app = state.apps[state.focus]
            sys.log("launching " .. app.path)
            sys.launch(app.path)
            -- Returning hands control back to the host, which tears this state
            -- down before starting the app. Never launch from inside the loop.
            return
        end
    elseif ui.BACK[event] or ui.LEFT[event] then
        discover()
        draw()
    end
end
