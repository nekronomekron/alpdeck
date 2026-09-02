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
-- Scrolls the window so the focused row is inside it. The menu icon is not a
-- row, so focusing it leaves the window alone.
local function scrollIntoView()
    if state.focus < 1 then
        return
    end
    if state.focus < state.top then
        state.top = state.focus
    elseif state.focus >= state.top + VISIBLE then
        state.top = state.focus - VISIBLE + 1
    end
end

local function moveBy(delta)
    local target = state.focus + delta
    if target < MENU_FOCUS then
        target = MENU_FOCUS
    elseif target > #state.apps then
        target = math.max(MENU_FOCUS, #state.apps)
    end
    state.focus = target

    scrollIntoView()
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

    scrollIntoView()
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
-- decides a refresh mode: ui.run owns both, because only the loop knows whether
-- this frame is a cursor move inside the list or a new screen.
local function draw()
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
end

-- What the panel is showing, in three parts.
--
-- The split is what buys the cheap refresh. Moving the cursor changes only the
-- third value, and ui.run then repaints the two rows involved -- 435ms against
-- the 609ms a whole panel costs. A scroll or a rescan changes the body, and
-- only the navbar changing drives the whole panel.
--
-- The navbar signs itself with the wifi BAR COUNT rather than the rssi: the raw
-- value drifts by a dBm between reads and would buy a refresh every half minute
-- to redraw an identical icon.
local function signature()
    local wifi = sys.wifi()
    local chrome = table.concat({
        state.focus == MENU_FOCUS and "menu" or "list",
        wifi.enabled and "on" or "off",
        ui.wifiBars(wifi),
    }, "|")

    return chrome, table.concat({ state.top, #state.apps }, "|"), state.focus
end

-- Only ever called for a move that left state.top alone, so both the old and
-- the new row are measured against the same window.
local function cursorRect(index)
    return ui.rowRect(index, state.top, W)
end

------------------------------------------------------------------- screens --

-- A modal list screen: draws itself, runs its own loop, returns when the user
-- backs out. Every sub-screen in the launcher is one of these, so they all
-- behave the same way.
--
-- opts: title, items, footer. An item is
--   {label=, value=, action=, disabled=, header=, screen=}
-- where `screen` marks a row whose action takes the panel over -- a keyboard,
-- a message, another menu. Those need a repaint afterwards; a row that only
-- changes a setting does not, because the value is part of the signature and
-- redraws like any other change.
local function runMenu(opts)
    local listTop = ui.LIST_TOP
    local visible = ui.visibleRows(H - 24, listTop)

    -- Start on the first row that is not a group label. ui.list never marks a
    -- header active because `selected` never points at one, and nil is a real
    -- answer: a menu with nothing selectable still draws, and back still leaves.
    local selected = ui.firstSelectable(opts.items)
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

    local function draw()
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
    end

    -- The title never changes, so nothing here ever drives the whole panel. The
    -- visible rows' values are part of the body: a toggle moves no cursor, and
    -- the screen still has to show what it did.
    local function signature()
        local parts = { top }
        for index = top, math.min(top + visible - 1, #opts.items) do
            local item = opts.items[index]
            parts[#parts + 1] = item.value and tostring(item.value()) or ""
            parts[#parts + 1] = (item.disabled and item.disabled()) and "-" or ""
        end
        return opts.title, table.concat(parts, "|"), selected
    end

    -- One step at a time even for a digest of eight, because the rows that can
    -- be selected are not evenly spaced -- nextSelectable is what skips the
    -- group labels, and it only knows how to take one step.
    local function moveBy(steps)
        if selected == nil then
            return  -- nothing to move between
        end

        local direction = steps > 0 and 1 or -1
        for _ = 1, math.abs(steps) do
            local target = ui.nextSelectable(opts.items, selected, direction)
            if not target then
                break  -- an end of the list; stop there rather than wrapping
            end
            selected = target
        end

        if selected < top then
            top = selected
        elseif selected >= top + visible then
            top = selected - visible + 1
        end
        -- Scrolling up onto the first row of a group brings its label along,
        -- otherwise the section arrives unlabelled. Only when the selection is
        -- at the top of the window: shifting it in any other case would push
        -- the selection off the bottom.
        if selected == top and top > 1 and opts.items[top - 1].header then
            top = top - 1
        end
    end

    local function apply(nav)
        local steps = ui.steps(nav)
        if steps ~= 0 then
            moveBy(steps)
        end

        if nav.action and ui.BACK[nav.action] then
            return "close"
        end

        -- Select and right both mean "forward", left means "back one value".
        -- The dial's travel goes through as a count, so a setting steps by as
        -- far as the user actually turned rather than by one per refresh.
        local direction = (nav.action and ui.CONFIRM[nav.action]) and 1 or nav.dx
        if direction == 0 then
            return
        end

        local item = opts.items[selected]
        if not item or not item.action or (item.disabled and item.disabled()) then
            return
        end

        if item.action(direction) == "close" then
            return "close"
        end
        if item.screen then
            return "repaint"
        end
    end

    ui.run{
        idleMs = 120000,
        body = ui.bodyRegion(H, 24),
        cursorRect = function(index)
            return ui.rowRect(index, top, W, listTop)
        end,
        signature = signature,
        draw = draw,
        apply = apply,
        -- Two minutes untouched is someone who walked away, not someone
        -- reading. Leaving a settings screen standing open is how a device
        -- gets found in a state nobody meant to leave it in.
        onIdle = function() return "close" end,
    }
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

    -- Flush AFTER the refresh, not before it: the long-press that opened this
    -- screen is still an action, and so is anything pressed during the second
    -- the panel spent drawing a message nobody could read yet. Either would
    -- dismiss it on the spot.
    input.flush()
    input.take(120000)
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
            screen = true,  -- raises the keyboard
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

local function refreshSdCard()
    -- The remount releases the card and takes it again, so anything reading it
    -- has to be finished first. Drawing this before the call is not only
    -- politeness: it is the last chance to touch the panel and the card in a
    -- known order.
    showBusy("sd card", "re-reading...")

    if sys.sd_remount() then
        showMessage("sd card", {
            "card re-read.",
            "",
            "the app list has been rescanned.",
        })
    else
        showMessage("sd card", {
            "no card found.",
            "",
            "check that it is seated, then try",
            "again.",
        })
    end
end

local function restart()
    -- E-paper holds its image with no power, so whatever is on the panel when
    -- the reset hits stays there for the whole boot. Leaving a menu up would
    -- look like a device that had hung at the moment it was told to restart.
    display.begin("full")
    ui.header("restart", W)
    display.text(ui.MARGIN, ui.LIST_TOP + 10, "restarting...", 2)
    display.show()

    sys.restart()  -- does not return
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

    -- Wraps for any step size, not just one: a digest hands over a whole turn
    -- of the dial at once, so index + direction can land well outside the list.
    index = (index - 1 + (direction or 1)) % #choices + 1

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
                screen = true,
                action = function() wifiSetup() end,
                disabled = needsWifi,
            },
            {
                label = "setup portal",
                screen = true,
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
                screen = true,
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
                screen = true,
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

            { header = true, label = "display" },
            {
                label = "full refresh",
                value = function()
                    return "every " .. settings.get("refresh_every")
                end,
                action = function(direction)
                    cycle("refresh_every", REFRESH_CHOICES, direction)
                end,
            },

            { header = true, label = "device" },
            {
                label = "info",
                screen = true,
                action = function() deviceInfo() end,
            },
            {
                label = "refresh sd card",
                screen = true,
                action = function() refreshSdCard() end,
            },
            {
                label = "restart",
                screen = true,
                action = function() restart() end,
            },
        },
    }
end

--------------------------------------------------------------------- input --

-- Nothing here draws. It changes the model and says what should happen to the
-- screen; ui.run decides whether that is worth a refresh and what kind.
local function apply(nav)
    -- Navigation first, always. A digest can carry both a turn and a press, and
    -- the press was made after the turn -- applying them the other way round
    -- would select the row the user was leaving.
    moveBy(ui.steps(nav))

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

    if ui.BACK[action] then
        discover()
    elseif ui.CONFIRM[action] then
        if state.focus == MENU_FOCUS then
            optionsMenu()
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

ui.run{
    idleMs = IDLE_LOOK_MS,
    body = ui.bodyRegion(H),
    cursorRect = cursorRect,
    signature = signature,
    draw = draw,
    apply = apply,
}
