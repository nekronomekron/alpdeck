-- The launcher's options menu: what the device lets you change, and the
-- screens behind the rows that need one.
--
--     local options = sys.import("/options.lua")
--     options.run()
--
-- Its own file, and imported only when the menu is actually opened -- the same
-- treatment the keyboard gets. Someone who boots the device and launches an app
-- never pays for either.
--
-- The menu itself is a table of rows, so adding a setting is a declaration
-- here plus a key in Settings.h. Nothing in the launcher has to learn about it.

local ui = sys.import("/lib/ui.lua")
local menu = sys.import("/lib/menu.lua")
local dialog = sys.import("/lib/dialog.lua")

local options = {}

local W = display.size()


local function wifiSetup()
    if not settings.get("wifi_enabled") then
        dialog.message("wifi setup", { "wifi is switched off.",
            "turn it on first." })
        return
    end

    dialog.busy("wifi setup", "scanning...")

    local ok, networks = pcall(wifi.scan)
    if not ok or not networks or #networks == 0 then
        dialog.message("wifi setup", { "no networks found.",
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

                wifi.configure(network.ssid, password)
                dialog.message("wifi setup", {
                    "connecting to " .. network.ssid .. "...",
                    "",
                    "this takes a few seconds. the",
                    "signal icon shows the result.",
                })
                return "close"
            end,
        }
    end

    menu.run{
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

    ftp.configure(user, password)
    dialog.message("ftp login", { "saved. the server restarts with",
        "the new login." })
end

local function refreshSdCard()
    -- The remount releases the card and takes it again, so anything reading it
    -- has to be finished first. Drawing this before the call is not only
    -- politeness: it is the last chance to touch the panel and the card in a
    -- known order.
    dialog.busy("sd card", "re-reading...")

    if sys.sd_remount() then
        dialog.message("sd card", {
            "card re-read.",
            "",
            "the app list has been rescanned.",
        })
    else
        dialog.message("sd card", {
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
    local radio = wifi.status()
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

    if not radio.enabled then
        lines[#lines + 1] = "wifi    switched off"
    elseif radio.connected then
        lines[#lines + 1] = string.format("wifi    %s", radio.ssid or "?")
        lines[#lines + 1] = string.format("ip      %s   %d dBm", radio.ip or "?",
            radio.rssi or 0)
    elseif radio.portal then
        lines[#lines + 1] = "wifi    setup portal is up"
    else
        lines[#lines + 1] = "wifi    not connected"
    end

    dialog.message("device", lines)
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
function options.run()
    menu.run{
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
                    wifi.portal()
                    dialog.message("setup portal", {
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
                    wifi.forget()
                    dialog.message("wifi", { "stored network forgotten." })
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


return options
