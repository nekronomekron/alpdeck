-- Display timing bench.
--
-- Answers the only question that matters when a screen feels slow: where do
-- the milliseconds actually go on THIS panel, in THIS room. GxEPD2 quotes 400ms
-- partial and 1200ms full, but those are the fallbacks it uses when there is no
-- BUSY line to wait on. Measured here: 609 warm and 1989 for a full panel, and
-- the fit across window sizes came out at 402ms fixed plus 0.70ms a row warm,
-- 443ms plus the same 0.70 cold. Parallel curves -- waking the panel is a flat
-- 41ms and not something a smaller window avoids.
--
-- Re-run it after any change to how frames are drawn or powered.
--
-- Every mode is timed twice, because since the firmware stopped hibernating
-- after every frame there are two different costs and both are real:
--
--   warm  the panel was still powered from the previous frame. What a user gets
--         while they are actually working a screen.
--   cold  the panel was hibernated first, so the frame pays a hardware reset, a
--         re-init and the charge pump coming up. What the first frame after a
--         pause costs.
--   wake  cold minus warm. The price of one hibernate round trip, and the whole
--         justification for deferring the power-down to the main loop.
--
-- The other two columns:
--
--   draw   Lua and Adafruit_GFX filling the page buffer. No panel involved.
--   power  the last hibernate, 102ms when it happens. On the warm pass it should
--          read 0 -- the main loop pays it later, off the critical path.
--          Reading 102 there means the deferral has stopped working.
--
-- The serial log has more: GxEPD2 is built with diagnostics on, so it prints
-- its own microsecond timings per phase (_PowerOn, _Update_Part, _Update_Full,
-- _PowerOff) as it goes. Those are the ground truth this app adds up.

-- Imported by absolute path: the libraries are on flash, this app is on the
-- card, and a relative path would resolve inside the app's own folder.
--
-- Only the chrome comes from them. The measured frames are opened and shown by
-- hand, deliberately -- a timing run that went through the shared refresh loop
-- would be measuring the library.
local ui = sys.import("/lib/ui.lua")
local screen = sys.import("/lib/screen.lua")

local W, H = display.size()

local TITLE = "display timing"

-- Three timed passes per mode and regime, plus one thrown away. The first frame
-- after a reset is forced full by the controller no matter what was asked for,
-- and the first frame in a new mode pays a ram write the steady state does not.
local WARMUP = 1
local REPEATS = 3

local POLL_MS = 40

-- What gets drawn into every timed frame. Ink matters: an e-paper refresh
-- moves particles, and a blank frame is not what a real screen costs.
local function pattern(y, h)
    display.rect(4, y + 4, W - 8, h - 8)
    for i = 0, 5 do
        local ry = y + 8 + i * 6
        if ry + 4 < y + h then
            display.rect(10, ry, W - 20, 3, i % 2 == 0)
        end
    end
    display.text(14, y + 10, "alpdeck timing bench 0123456789", 1)
end

-- One regime of one mode. `cold` hibernates the panel before each frame so the
-- frame pays for waking it; that power-down sits inside the timed span, because
-- in this regime it is part of the cycle rather than something the main loop
-- absorbs afterwards.
local function timeFrames(mode, x, y, w, h, cold)
    local draw, refresh, power, total = 0, 0, 0, 0

    for pass = 1, WARMUP + REPEATS do
        local started = sys.millis()

        if cold then
            display.power_down()
        end

        local frameStart = sys.millis()
        if mode == "region" then
            display.begin("partial", x, y, w, h)
        else
            display.begin(mode)
        end

        pattern(y, h)
        local drawn = sys.millis()

        display.show()
        local shownRefresh, shownPower = display.timing()

        if pass > WARMUP then
            draw = draw + (drawn - frameStart)
            refresh = refresh + shownRefresh
            power = power + (shownPower or 0)
            total = total + (sys.millis() - started)
        end
    end

    return {
        draw = draw // REPEATS,
        refresh = refresh // REPEATS,
        power = power // REPEATS,
        total = total // REPEATS,
    }
end

local function measure(label, mode, x, y, w, h)
    local warm = timeFrames(mode, x, y, w, h, false)
    local cold = timeFrames(mode, x, y, w, h, true)

    local row = {
        label = label,
        draw = warm.draw,
        warm = warm.refresh,
        cold = cold.refresh,
        wake = cold.refresh - warm.refresh,
        power = warm.power,
        total = warm.total,
    }

    sys.log(string.format(
        "bench  %-14s draw %4d  warm %4d  cold %4d  wake %4d  power %4d  total %4d",
        row.label, row.draw, row.warm, row.cold, row.wake, row.power,
        row.total))
    return row
end

------------------------------------------------------------------- measure --

sys.log("bench: measuring, " .. REPEATS .. " passes per mode and regime")

-- Ordered cheapest last, so the screen ends on the fast case and the eye has
-- something to compare the slow one against.
local rows = {
    measure("full panel", "full", 0, 0, W, H),
    measure("partial full", "partial", 0, 0, W, H),
    measure("region 240px", "region", 0, 42, W, 240),
    measure("region 120px", "region", 0, 90, W, 120),
    measure("region 60px", "region", 0, 120, W, 60),
    measure("region 40x28", "region", 180, 130, 40, 28),
}

------------------------------------------------------------------- results --

-- One full refresh for the results, and it is not counted: it clears the
-- ghosting the bench just built up as well as showing the numbers.
local COLUMNS = { 12, 148, 202, 256, 310, 356 }

local function line(y, cells, size)
    for index, cell in ipairs(cells) do
        display.text(COLUMNS[index], y, cell, size or 1)
    end
end

local radio = wifi.status()
local shownBars = ui.wifiBars(radio)

display.begin("full")

ui.header(TITLE, W, radio)
display.text(ui.MARGIN, ui.HEADER_H + 6,
    "milliseconds, mean of " .. REPEATS .. " passes", 1)

local TABLE_TOP = 66

line(TABLE_TOP, { "mode", "draw", "warm", "cold", "wake", "power" })
display.rect(ui.MARGIN, TABLE_TOP + 12, W - 2 * ui.MARGIN, 1, true)

for index, row in ipairs(rows) do
    local y = TABLE_TOP + 20 + (index - 1) * 16
    line(y, {
        row.label,
        tostring(row.draw),
        tostring(row.warm),
        tostring(row.cold),
        tostring(row.wake),
        tostring(row.power),
    })
end

local footer = TABLE_TOP + 20 + #rows * 16 + 8
display.rect(ui.MARGIN, footer, W - 2 * ui.MARGIN, 1, true)
display.text(ui.MARGIN, footer + 8,
    "warm: panel still powered. cold: hibernated first.", 1)
display.text(ui.MARGIN, footer + 20,
    "wake is what one hibernate round trip costs the next frame.", 1)
display.text(ui.MARGIN, footer + 32,
    "power 0 on warm means the hibernate is off the critical path.", 1)

ui.footer("press anything to go back", W, H)
display.show()

while true do
    local event = input.read(0)
    if event then
        return
    end

    -- The results are a report and never change, so the icon is the only thing
    -- on this panel that can go stale. A header-only frame is what that is
    -- worth; redrawing the table to move four bars would not be.
    radio = wifi.status()
    if ui.wifiBars(radio) ~= shownBars then
        shownBars = ui.wifiBars(radio)
        screen.refreshHeader(TITLE, W, radio)
    end

    sys.delay(POLL_MS)
end
