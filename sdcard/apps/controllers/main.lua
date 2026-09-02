-- Controller test: a live schematic of both input controllers.
--
-- Gamepad on the left, rotary on the right; a controller that did not answer
-- on the bus is left out and the other one takes the whole width. Every button
-- fills in black while it is held, so the whole surface of both controllers can
-- be walked through without a serial monitor.
--
-- Why this app polls instead of blocking on input.read(): the event queue is
-- edge-triggered. It reports a press and never a release, so it cannot say what
-- is held *right now* -- input.state() is the level-triggered mirror for that.
-- Events are still drained each pass, both to keep the queue from filling and
-- to catch the rotary long-press.

local W, H = display.size()

-- The bindings this app needs postdate the first firmware that shipped apps.
-- Saying so beats drawing a screen where nothing ever lights up.
if type(input.state) ~= "function" or type(display.circle) ~= "function" then
    display.begin("full")
    display.text(12, 30, "firmware too old", 2)
    display.text(12, 66, "this app needs input.state() and", 1)
    display.text(12, 80, "display.circle(), added with the", 1)
    display.text(12, 94, "controller schematic.", 1)
    display.text(12, 122, "flash the current firmware, then", 1)
    display.text(12, 136, "copy sdcard/ to the card again.", 1)
    display.text(12, 164, "press anything to go back", 1)
    display.show()
    input.read(30000)
    return
end

-- ----------------------------------------------------------------- geometry

local HEADER_RULE_Y = 30
local PANEL_Y, PANEL_H = 34, 244
local BOARD_Y, BOARD_H = 52, 200
local READOUT_Y = 256
local FOOTER_RULE_Y = 282

local FULL_REFRESH_EVERY = 12  -- partial refreshes leave ghosting behind
local POLL_MS = 40
local EXIT_HOLD_MS = 1500

-- ------------------------------------------------------------ mounting angle

-- The gamepad is mounted turned 90 degrees clockwise. Rotating a board vector
-- (bx, by) by +90 cw gives world (-by, bx): the board right edge points down
-- and its top edge points right. So the driver gamepad_left is physically UP
-- and gamepad_up is physically RIGHT -- the events stay in the board frame
-- (that is the driver contract) and only this app turns them.
--
-- Same rotation for the silkscreen: X top, Y left, A right, B bottom becomes
-- Y top, X right, A bottom, B left.

-- World position -> the state field that lights it up.
local STICK_ARROWS = {
    { dx =  0, dy = -1, dir = "up",    field = "left" },
    { dx =  1, dy =  0, dir = "right", field = "up" },
    { dx =  0, dy =  1, dir = "down",  field = "right" },
    { dx = -1, dy =  0, dir = "left",  field = "down" },
}

local FACE_BUTTONS = {
    { dx =  0, dy = -1, label = "Y", field = "y" },
    { dx =  1, dy =  0, label = "X", field = "x" },
    { dx =  0, dy =  1, label = "A", field = "a" },
    { dx = -1, dy =  0, label = "B", field = "b" },
}

-- The rotary sits upright, so its 5-way needs no turning.
local NAV_SWITCHES = {
    { dx =  0, dy = -1, dir = "up",    field = "up" },
    { dx =  1, dy =  0, dir = "right", field = "right" },
    { dx =  0, dy =  1, dir = "down",  field = "down" },
    { dx = -1, dy =  0, dir = "left",  field = "left" },
}

-- ------------------------------------------------------------------ drawing

-- Centres a label on a point. measure() rather than a hardcoded cell width, so
-- this keeps working if the font ever changes.
local function centered(cx, cy, text, size, invert)
    size = size or 1
    local w, h = display.measure(text, size)
    if invert then
        display.color("white")
    end
    display.text(cx - w // 2, cy - h // 2, text, size)
    if invert then
        display.color("black")
    end
end

local function arrow(cx, cy, dir, size, filled)
    local s = size
    if dir == "up" then
        display.triangle(cx, cy - s, cx - s, cy + s, cx + s, cy + s, filled)
    elseif dir == "down" then
        display.triangle(cx, cy + s, cx - s, cy - s, cx + s, cy - s, filled)
    elseif dir == "left" then
        display.triangle(cx - s, cy, cx + s, cy - s, cx + s, cy + s, filled)
    else
        display.triangle(cx + s, cy, cx - s, cy - s, cx - s, cy + s, filled)
    end
end

-- A held control is filled black, so its label has to invert to stay readable.
-- The outline is drawn either way: a filled circle alone loses its edge against
-- a neighbour, and the idle state needs it.
local function keycap(cx, cy, r, label, held)
    if held then
        display.circle(cx, cy, r, true)
    end
    display.circle(cx, cy, r)
    centered(cx, cy, label, 1, held)
end

local function pill(x, y, w, h, label, held)
    if held then
        display.roundrect(x, y, w, h, 4, true)
    end
    display.roundrect(x, y, w, h, 4)
    centered(x + w // 2, y + h // 2, label, 1, held)
end

local function panelFrame(px, pw, title)
    display.rect(px, PANEL_Y, pw, PANEL_H)
    display.text(px + 8, PANEL_Y + 5, title, 1)
end

-- ------------------------------------------------------------------ gamepad

local function drawGamepad(px, pw, gamepad)
    panelFrame(px, pw, "gamepad  90 cw")

    local cx = px + pw // 2
    display.roundrect(cx - 59, BOARD_Y, 118, BOARD_H, 10)

    -- Stick: travel ring, the four digitised directions around it, and a dot at
    -- the live position, so a miscalibrated centre or a swapped axis shows.
    local stickY, ring = 96, 26
    display.circle(cx, stickY, ring)

    for _, a in ipairs(STICK_ARROWS) do
        arrow(cx + a.dx * 32, stickY + a.dy * 32, a.dir, 7, gamepad[a.field])
    end

    local wx = -gamepad.dy  -- board (bx, by) turned 90 cw is world (-by, bx)
    local wy = gamepad.dx
    display.circle(cx + (wx * (ring - 6)) // 512,
        stickY + (wy * (ring - 6)) // 512, 5, true)

    -- Side by side on the board, so stacked once it is turned: select above
    -- start, matching what you see with the board mounted.
    pill(cx - 27, 142, 54, 15, "SELECT", gamepad.select)
    pill(cx - 27, 160, 54, 15, "START", gamepad.start)

    for _, b in ipairs(FACE_BUTTONS) do
        keycap(cx + b.dx * 24, 214 + b.dy * 24, 11, b.label, gamepad[b.field])
    end

    -- Raw ADC, for checking GAMEPAD_STICK_CENTER and the invert flags.
    display.text(px + 8, READOUT_Y,
        string.format("raw  x %4d  y %4d", gamepad.stick_x, gamepad.stick_y), 1)
    display.text(px + 8, READOUT_Y + 10,
        string.format("board dx %4d dy %4d", gamepad.dx, gamepad.dy), 1)
end

-- ------------------------------------------------------------------- rotary

local function drawRotary(px, pw, rotary, spin)
    panelFrame(px, pw, "rotary encoder")

    local cx = px + pw // 2
    display.roundrect(cx - 83, BOARD_Y, 166, BOARD_H, 10)

    -- The dial has no pressed state to show, so its feedback is a marker that
    -- steps around the rim: one detent, one tick.
    local wheelY, wheelR, TICKS = 152, 72, 16
    display.circle(cx, wheelY, wheelR)

    local marker = rotary.encoder % TICKS
    for i = 0, TICKS - 1 do
        local angle = i * 2 * math.pi / TICKS
        local sin, cos = math.sin(angle), math.cos(angle)
        local tx = math.floor(cx + wheelR * sin)
        local ty = math.floor(wheelY - wheelR * cos)
        if i == marker then
            display.circle(tx, ty, 5, true)
        else
            display.line(math.floor(cx + (wheelR - 6) * sin),
                math.floor(wheelY - (wheelR - 6) * cos), tx, ty)
        end
    end

    for _, s in ipairs(NAV_SWITCHES) do
        arrow(cx + s.dx * 46, wheelY + s.dy * 46, s.dir, 12, rotary[s.field])
    end

    if rotary.select then
        display.circle(cx, wheelY, 22, true)
    end
    display.circle(cx, wheelY, 22)
    centered(cx, wheelY, "SEL", 1, rotary.select)

    display.text(px + 8, READOUT_Y,
        string.format("encoder %5d   turn %s", rotary.encoder, spin), 1)
end

-- --------------------------------------------------------------------- page

local refreshes = 0

local function draw(state, spin)
    refreshes = refreshes + 1
    display.begin(refreshes % FULL_REFRESH_EVERY == 1 and "full" or "partial")

    display.text(12, 8, "controller test", 2)
    display.rect(0, HEADER_RULE_Y, W, 2, true)

    -- Only what actually answered on the bus gets drawn; a single controller
    -- gets a centred panel instead of an empty half screen.
    local panels = {}
    if state.gamepad.present then
        panels[#panels + 1] = function(px, pw)
            drawGamepad(px, pw, state.gamepad)
        end
    end
    if state.rotary.present then
        panels[#panels + 1] = function(px, pw)
            drawRotary(px, pw, state.rotary, spin)
        end
    end

    if #panels == 0 then
        display.text(12, 120, "no controller detected", 2)
        display.text(12, 150, "check the stemma qt chain: sda 9, scl 10", 1)
    else
        local pw = 190
        local left = (W - (#panels * pw + (#panels - 1) * 4)) // 2
        for index, panel in ipairs(panels) do
            panel(left + (index - 1) * (pw + 4), pw)
        end
    end

    display.rect(0, FOOTER_RULE_Y, W, 1, true)
    display.text(12, FOOTER_RULE_Y + 5,
        "hold START 1.5s or long-press SELECT to exit", 1)

    display.show()
end

-- Redraw only on a real change: an e-paper frame costs 609ms, and the stick
-- ADC jitters by a few counts at rest. Quantising it keeps that noise from
-- triggering a refresh of its own.
local function signature(state, spin)
    local g, r = state.gamepad, state.rotary
    return table.concat({
        tostring(r.present), tostring(r.select), tostring(r.up),
        tostring(r.left), tostring(r.down), tostring(r.right),
        tostring(r.encoder), spin,
        tostring(g.present), tostring(g.a), tostring(g.b), tostring(g.x),
        tostring(g.y), tostring(g.start), tostring(g.select),
        tostring(g.left), tostring(g.right), tostring(g.up), tostring(g.down),
        tostring(g.dx // 32), tostring(g.dy // 32),
    }, ",")
end

local state = input.state()
local spin = "--"
local encoder = state.rotary.encoder
local drawn = signature(state, spin)
local startHeldSince = nil

draw(state, spin)

while true do
    -- Drain the event queue: unread events pile up until it drops them, and the
    -- rotary long-press exists only as an event.
    local event = input.read(0)
    while event do
        if event == "rotary_select_long" then
            return
        end
        event = input.read(0)
    end

    state = input.state()

    if state.rotary.encoder ~= encoder then
        spin = state.rotary.encoder > encoder and "cw" or "ccw"
        encoder = state.rotary.encoder
    end

    -- Exit on a deliberate hold rather than a press, so START itself stays
    -- testable like every other button.
    if state.gamepad.start then
        startHeldSince = startHeldSince or sys.millis()
        if sys.millis() - startHeldSince >= EXIT_HOLD_MS then
            return
        end
    else
        startHeldSince = nil
    end

    local current = signature(state, spin)
    if current ~= drawn then
        drawn = current
        draw(state, spin)
    end

    sys.delay(POLL_MS)
end
