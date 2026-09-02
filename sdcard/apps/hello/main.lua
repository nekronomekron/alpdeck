-- Example app, and the reference for what the API can do.
--
-- Copy the whole sdcard/ tree onto the card. Returning from this script hands
-- control back to the launcher: the host closes the VM and frees everything,
-- so an app needs no teardown of its own.
--
-- What it demonstrates, in the order it comes up:
--   * loading an asset from the app's own folder with a relative path
--   * drawing that asset as a 1bpp sprite with a transparent background
--   * region refresh -- redrawing only the play area when the sprite moves,
--     instead of paying for the whole 400x300 panel every frame
--   * the font and ink settings
--   * the shared UI libraries, so an app looks like the rest of the device
--     without redrawing the launcher's chrome by hand
--
-- The libraries live on flash and the app on the card, which is why they are
-- imported by absolute path: a relative one would resolve inside this app's
-- own folder.

local ui = sys.import("/lib/ui.lua")
local screen = sys.import("/lib/screen.lua")

local W, H = display.size()

local TITLE = "hello"

local SPRITE_W, SPRITE_H = 16, 16
-- Relative, so the app never needs to know where it was installed. Generated
-- from sprite.png with scripts/png2bin.py.
local SPRITE = fs.read("sprite.bin")

-- The area the sprite moves in, and the only part that gets refreshed while it
-- does. Everything outside keeps whatever the last full frame left there.
local PLAY = { x = 10, y = 64, w = W - 20, h = 128 }

local sprite = {
    x = PLAY.x + PLAY.w // 2 - SPRITE_W // 2,
    y = PLAY.y + PLAY.h // 2 - SPRITE_H // 2,
}
local steps = 0
local lastEvent = "(none yet)"

-- The bar count last drawn in the header, so the icon can be kept honest
-- without redrawing anything for an rssi that wandered by a dBm.
local shownBars = nil

local STEP = 8

-- Everything that changes as the sprite moves lives inside the region --
-- including the readout. Anything drawn outside it would keep the value it had
-- when the last whole-panel frame ran, and quietly lie.
local function drawPlayArea()
    display.rect(PLAY.x, PLAY.y, PLAY.w, PLAY.h)

    if SPRITE then
        -- No background argument: clear bits are left alone, which is what a
        -- sprite over existing artwork wants.
        display.bitmap(sprite.x, sprite.y, SPRITE_W, SPRITE_H, SPRITE)
    else
        display.font("default")
        display.text(PLAY.x + 8, PLAY.y + 8, "sprite.bin missing", 1)
    end

    display.font("default")
    display.text(PLAY.x + 8, PLAY.y + PLAY.h - 16,
        string.format("steps %d   at %d,%d   last %s",
            steps, sprite.x, sprite.y, lastEvent), 1)
end

-- Whole panel: chrome plus the play area. Slow (609ms partial, 1989ms full),
-- so it runs on entry and when something outside the play area changes.
local function drawAll(full)
    display.begin(full and "full" or "partial")

    -- ui.header draws in whatever face is current, and this app changes it, so
    -- put it back before every frame rather than hoping.
    display.font("default")
    local radio = wifi.status()
    shownBars = ui.wifiBars(radio)
    ui.header(TITLE, W, radio)

    display.text(ui.MARGIN, ui.HEADER_H + 10, "move with the stick or the dial", 1)

    drawPlayArea()

    -- Static chrome only: this is outside the region the movement refreshes,
    -- so nothing here may change between whole-panel frames.
    display.font("default")
    local luaBytes, freeHeap = sys.memory()

    -- Bound to a local rather than called inline: timing() returns the refresh
    -- and the power-down, and a two-value call in the last argument slot would
    -- quietly hand format() an argument it does not have a slot for.
    local refreshMs = display.timing()
    display.text(ui.MARGIN, H - 34,
        string.format("lua %d B   heap %d B   last refresh %d ms",
            luaBytes, freeHeap, refreshMs), 1)

    ui.footer("long-press select / B to exit", W, H)

    display.show()
end

-- Only the play area. The panel keeps everything outside the region, so the
-- chrome drawn by drawAll() stays on screen without being redrawn.
local function drawMove()
    display.begin("partial", PLAY.x, PLAY.y, PLAY.w, PLAY.h)
    drawPlayArea()
    display.show()
end

local function move(dx, dy)
    local x = sprite.x + dx * STEP
    local y = sprite.y + dy * STEP

    -- Clamp inside the play area, one pixel in from its outline.
    if x < PLAY.x + 2 then x = PLAY.x + 2 end
    if y < PLAY.y + 2 then y = PLAY.y + 2 end
    if x > PLAY.x + PLAY.w - SPRITE_W - 2 then x = PLAY.x + PLAY.w - SPRITE_W - 2 end
    if y > PLAY.y + PLAY.h - SPRITE_H - 2 then y = PLAY.y + PLAY.h - SPRITE_H - 2 end

    if x == sprite.x and y == sprite.y then
        return false
    end

    sprite.x, sprite.y = x, y
    steps = steps + 1
    return true
end

-- Event names carry their source (rotary_* / gamepad_*); this listens to both.
local MOVES = {
    rotary_up = { 0, -1 }, rotary_down = { 0, 1 },
    rotary_left = { -1, 0 }, rotary_right = { 1, 0 },
    rotary_cw = { 1, 0 }, rotary_ccw = { -1, 0 },
    gamepad_up = { 0, -1 }, gamepad_down = { 0, 1 },
    gamepad_left = { -1, 0 }, gamepad_right = { 1, 0 },
}
drawAll(true)

while true do
    local event = input.read(30000)

    if event then
        lastEvent = event
    else
        -- Nothing for thirty seconds. Look at the radio rather than the panel:
        -- the icon is the one thing up there that changes on its own, and a
        -- header-only frame is the cheapest way to say so.
        local radio = wifi.status()
        if ui.wifiBars(radio) ~= shownBars then
            shownBars = ui.wifiBars(radio)
            screen.refreshHeader(TITLE, W, radio)
        end
    end

    if screen.BACK[event] then
        return  -- the host restarts the launcher for us
    elseif MOVES[event] then
        local delta = MOVES[event]
        if move(delta[1], delta[2]) then
            drawMove()
        end
    elseif event then
        -- Every event is echoed, not just the ones this app acts on. A
        -- controller that works but sends names the app does not know then
        -- looks different from one that sends nothing at all -- which is the
        -- difference between a stale copy on the card and a wiring fault.
        drawAll(false)
    end
end
