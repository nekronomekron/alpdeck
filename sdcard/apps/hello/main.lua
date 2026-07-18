-- Example app. Copy the whole sdcard/apps/ tree onto the SD card.
--
-- Returning from this script hands control back to the launcher, so an app
-- needs no teardown of its own: the host closes the VM and frees everything.

local W, H = display.size()
local count = 0

local function draw()
    display.clear()

    display.text(12, 18, "Hello", 3)
    display.rect(12, 50, W - 24, 2, true)

    display.text(12, 70, "turn the dial or push the stick", 1)
    display.text(12, 100, tostring(count), 4)

    local luaBytes, freeHeap = sys.memory()
    display.text(12, H - 30, string.format("lua %d B   heap %d B", luaBytes, freeHeap), 1)
    display.text(12, H - 16, "long-press select / B to exit", 1)

    display.show()
end

-- A partial-refresh clear() by default; the periodic full clear lives in the
-- launcher, and a counter demo does not accumulate enough ghosting to need it.

draw()

-- Event names carry their source (rotary_* / gamepad_*); this demo listens to
-- both controllers.
local UP = { rotary_cw = true, rotary_up = true, gamepad_up = true }
local DOWN = { rotary_ccw = true, rotary_down = true, gamepad_down = true }
local EXIT = { rotary_select_long = true, gamepad_b = true }

while true do
    local event = input.read(30000)

    if UP[event] then
        count = count + 1
        draw()
    elseif DOWN[event] then
        count = count - 1
        draw()
    elseif EXIT[event] then
        -- Simply return: the host restarts the launcher for us.
        return
    end
end
