-- Full-screen messages: the two shapes of "the screen has something to say"
-- that are not a list.
--
--     local dialog = sys.import("/lib/dialog.lua")
--     dialog.message("wifi", { "stored network forgotten." })
--     dialog.busy("wifi setup", "scanning...")
--
-- message() waits for the user; busy() does not, because the point of it is to
-- be on the panel before something slow starts.

local ui = sys.import("/lib/ui.lua")
local screen = sys.import("/lib/screen.lua")

local dialog = {}

local W, H = display.size()

-- A full-screen message with a single way out. Used for results and for the
-- things that take long enough to need saying.
function dialog.message(title, lines, footer)
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
    input.take(screen.IDLE_MS)
end

-- Drawn before a blocking call, so the device does not look wedged during the
-- two to four seconds a scan takes.
function dialog.busy(title, text)
    display.begin("partial")
    ui.header(title, W)
    display.text(ui.MARGIN, ui.LIST_TOP + 10, text, 2)
    display.show()
end

return dialog
