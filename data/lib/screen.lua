-- Running a screen: the input vocabulary, and the loop that turns state into
-- frames.
--
--     local screen = sys.import("/lib/screen.lua")
--
-- Split from /lib/ui.lua, which draws. Every list screen in the launcher runs
-- on screen.run, and any app is welcome to; a screen with its own loop -- the
-- keyboard, because its regions are per-cell rather than per-screen -- still
-- takes its vocabulary from here so that "back" means one thing on the device.

local ui = sys.import("/lib/ui.lua")

local screen = {}

--------------------------------------------------------------------- input --

-- One vocabulary for both controllers, so no screen has to know which is
-- attached. Event names carry their source; these tables map them to intent.
--
-- CONFIRM and BACK are what input.take() reports as an action. The four
-- direction tables are for apps still reading the raw event stream: take() has
-- already turned those events into dx, dy and wheel.
screen.DOWN = { rotary_cw = true, rotary_down = true, gamepad_down = true }
screen.UP = { rotary_ccw = true, rotary_up = true, gamepad_up = true }
screen.LEFT = { rotary_left = true, gamepad_left = true }
screen.RIGHT = { rotary_right = true, gamepad_right = true }
screen.CONFIRM = { rotary_select = true, gamepad_a = true, gamepad_start = true }
screen.BACK = { rotary_select_long = true, gamepad_b = true }

-- How long a screen waits before deciding the user has walked away. Two
-- minutes untouched is not someone reading: e-paper holds its image with no
-- power, so a settings screen or a password field left standing is how a device
-- gets found in a state nobody meant to leave it in.
--
-- One number, because it is one policy. A screen that should stay up for its
-- own reasons passes its own idleMs.
screen.IDLE_MS = 120000

-- A digest read as lines of a list: the dial and the d-pad mean the same thing
-- here. A screen that walks a grid uses nav.dy and nav.wheel separately, which
-- is the only reason they arrive apart.
function screen.steps(nav)
    return nav.dy + nav.wheel
end

--------------------------------------------------------------------- loop --

-- Input -> state -> render, decoupled. Every list screen in the launcher runs
-- on this, and any app is welcome to.
--
-- A screen using it never draws in response to an event. It changes state, and
-- this redraws when the state stops matching what the panel is showing. That
-- indirection is the whole point: a whole-panel refresh measures 609ms on this
-- display, during which a user can turn the dial another eight detents, and
-- drawing per event would then spend five seconds walking to a cell they left
-- long ago. Drawing from state instead means those eight detents cost one frame.
--
-- The other half is how much panel each frame drives. Measured on the
-- GDEY042T81, a partial refresh costs
--
--     402ms fixed + 0.70ms per row
--
-- so two thirds of a whole-panel frame is overhead that a smaller window does
-- not avoid. That is why the signature comes apart in three: a cursor move
-- repaints two rows (~435ms), a change elsewhere in the body repaints the band
-- below the navbar, and only a change to the chrome drives the whole panel.
--
-- opts:
--   signature()  -> chrome, body, cursor. Three values that change exactly when
--                   the corresponding part of the screen would look different.
--                   `cursor` is the selected index and nothing else, so that
--                   moving it can be told apart from the list underneath it
--                   changing. Later values may be omitted.
--   draw(mode)   draws the WHOLE screen. A frame is open and show() is called
--                for you; on a region frame the drawing outside it is clipped
--                away, so draw() never has to know which mode it is in. Drawing
--                everything is not waste, it is what keeps a region correct:
--                the page buffer comes up white, so anything inside the window
--                that is not drawn is erased.
--   cursorRect(index) -> {x, y, w, h} for the row `index` sits on, or nil.
--                Without it a cursor move costs a whole body refresh.
--   body         {x, y, w, h} the region a body-only change is confined to. A
--                nil w or h means "out to the panel edge". Without it every
--                change costs a whole-panel refresh.
--   apply(nav)   nav is {dx, dy, wheel, action}. Change state and return nil,
--                "close" to leave the screen, or "repaint" when something else
--                has taken over the panel in the meantime.
--   idleMs       how long to wait for input before looking at the state again.
--                A screen showing anything that changes on its own -- a clock,
--                a wifi icon -- wants this short enough to keep it honest, and
--                pays nothing when nothing changed.
--   onIdle()     called when idleMs passed with no input at all. Return "close"
--                to leave; the default is to stay and look again.
function screen.run(opts)
    local W, H = display.size()
    local idleMs = opts.idleMs or 60000

    local shownChrome, shownBody, shownCursor
    local partials = 0
    local stale = true  -- nothing of this screen is on the panel yet

    -- Entering a screen is a context switch. What the user pressed at the last
    -- one was aimed at the last one.
    input.flush()

    while true do
        local chrome, body, cursor = opts.signature()

        if stale or chrome ~= shownChrome or body ~= shownBody
                or cursor ~= shownCursor then
            -- Ghosting builds up over partial refreshes, so one in every N is
            -- full. How many is a setting -- how fast it builds depends on the
            -- panel and the room it is in -- and it is read per frame because
            -- the screen changing it is one of these.
            local everyN = settings.get("refresh_every") or 8

            local mode, rect
            if stale or partials + 1 >= everyN then
                mode = "full"
            elseif chrome == shownChrome and body == shownBody
                    and opts.cursorRect then
                -- Only the cursor moved: two rows out of the whole panel. This
                -- is the common case on a list, and the cheapest frame there
                -- is.
                rect = ui.unionRect(opts.cursorRect(shownCursor),
                    opts.cursorRect(cursor))
                mode = rect and "cursor" or "partial"
            elseif chrome == shownChrome and opts.body then
                mode = "region"
                rect = opts.body
            else
                mode = "partial"
            end

            if rect then
                display.begin("partial", rect[1], rect[2],
                    rect[3] or W - rect[1], rect[4] or H - rect[2])
            else
                display.begin(mode)
            end

            opts.draw(mode)
            display.show()

            partials = mode == "full" and 0 or partials + 1
            shownChrome, shownBody, shownCursor = chrome, body, cursor
            stale = false
        end

        -- Everything that happened during that refresh arrives here as one
        -- digest, so the loop comes straight back round to draw the state the
        -- user has already reached rather than each state on the way to it.
        local nav = input.take(idleMs)
        if nav == nil then
            -- Nothing at all for idleMs. Most screens use this only to come
            -- round and look at their state again; one that should not be left
            -- standing open says so.
            if opts.onIdle and opts.onIdle() == "close" then
                return
            end
        else
            local outcome = opts.apply(nav)
            if outcome == "close" then
                return
            elseif outcome == "repaint" then
                -- A sub-screen owned the panel while apply() ran: the state may
                -- be untouched, but the glass no longer shows it.
                stale = true
                input.flush()
            end
        end
    end
end

return screen
