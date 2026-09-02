-- Shared screen furniture for the launcher, and for any app that wants the
-- same look.
--
--     local ui = sys.import("/lib/ui.lua")
--
-- Everything here draws with the built-in 6x8 face and expects a frame to be
-- open already: the caller decides the refresh mode and when to show(), because
-- only the caller knows whether this is a full redraw or a region.

local ui = {}

ui.MARGIN = 12
ui.NAV_HEIGHT = 40          -- navbar, closed by a full-width 2px rule
ui.ROW_HEIGHT = 22
ui.LIST_TOP = ui.NAV_HEIGHT + 14

-- How many rows fit below the navbar on a panel of the given height.
function ui.visibleRows(height, top)
    return math.floor((height - (top or ui.LIST_TOP) - 6) / ui.ROW_HEIGHT)
end

--------------------------------------------------------------------- icons --

-- How many bars the icon would fill, 0 to 4.
--
-- Separate from the drawing because a screen comparing itself to what is on the
-- panel has to compare this and not the rssi: the raw value moves by a dBm or
-- two every time it is read, and signing a screen with it would buy a 609ms
-- refresh every half minute to redraw an identical icon.
function ui.wifiBars(wifi)
    if not (wifi.enabled and wifi.connected) then
        return 0
    end
    local rssi = wifi.rssi or -100
    return (rssi >= -55 and 4) or (rssi >= -65 and 3)
        or (rssi >= -75 and 2) or 1
end

-- Four ascending signal bars, 18px wide in total. The filled count follows the
-- RSSI. Offline draws them hollow with a strike-through; switched off draws the
-- same strike over an empty frame, because "no signal" and "radio off" are
-- different states and guessing between them wastes debugging time.
function ui.wifiIcon(x, baseline, wifi)
    local bars = ui.wifiBars(wifi)

    for i = 1, 4 do
        local h = 4 + (i - 1) * 3
        display.rect(x + (i - 1) * 5, baseline - h, 3, h, i <= bars)
    end

    if not wifi.enabled then
        -- A cross, not a slash: unmistakably off rather than merely weak.
        display.line(x - 1, baseline, x + 18, baseline - 14)
        display.line(x - 1, baseline - 14, x + 18, baseline)
    elseif bars == 0 then
        display.line(x - 1, baseline, x + 18, baseline - 14)
    end
end

-- Hamburger. Focused draws it inverted inside a filled box, the same treatment
-- a selected row gets, so "this has focus" means one thing everywhere.
function ui.menuIcon(x, y, focused)
    if focused then
        display.rect(x - 5, y - 5, 26, 26, true)
        display.color("white")
    end

    for i = 0, 2 do
        display.rect(x, y + i * 5, 16, 2, true)
    end

    if focused then
        display.color("black")
    end
end

--------------------------------------------------------------------- chrome --

-- The launcher's navbar: name, version, wifi state, menu icon.
function ui.navbar(opts)
    local width = opts.width
    local titleW = display.measure(opts.title, 3)
    display.text(ui.MARGIN, 8, opts.title, 3)

    if opts.version then
        display.text(ui.MARGIN + titleW + 6, 24, opts.version, 1)
    end

    local menuX = width - ui.MARGIN - 16
    ui.menuIcon(menuX, 14, opts.menuFocused)
    if opts.wifi then
        ui.wifiIcon(menuX - 10 - 18, 28, opts.wifi)
    end

    display.rect(0, ui.NAV_HEIGHT, width, 2, true)
end

-- Header for a sub-screen: a title and the same rule, but no icons. Sub-screens
-- are modal, so there is nothing up there to reach.
function ui.header(title, width)
    display.text(ui.MARGIN, 12, title, 2)
    display.rect(0, ui.NAV_HEIGHT, width, 2, true)
end

-- A hint line along the bottom, above a thin rule.
function ui.footer(text, width, height)
    display.rect(0, height - 20, width, 1, true)
    display.text(ui.MARGIN, height - 14, text, 1)
end

---------------------------------------------------------------------- list --

-- Generic scrolling list. `render(item, y, active, index)` draws one row; this
-- owns the selection bar, the ink switch and the scrollbar, which are the parts
-- every list gets wrong in the same way.
--
-- opts: items, selected, top, visible, width, height, y (first row's top)
function ui.list(opts)
    local top = opts.y or ui.LIST_TOP
    local visible = opts.visible or ui.visibleRows(opts.height, top)

    for offset = 0, visible - 1 do
        local index = opts.top + offset
        local item = opts.items[index]
        if item then
            local y = top + offset * ui.ROW_HEIGHT
            local active = index == opts.selected

            if active then
                display.rect(ui.MARGIN, y - 4,
                    opts.width - 2 * ui.MARGIN, ui.ROW_HEIGHT, true)
                display.color("white")
                display.text(ui.MARGIN + 6, y, ">", 2)
            end

            opts.render(item, y, active, index)
            display.color("black")
        end
    end

    ui.scrollbar(opts.items, opts.top, visible, opts.width, opts.height, top)
end

-- Right-edge scrollbar, only when the list does not fit. The thumb tracks the
-- scroll window, not the selection.
function ui.scrollbar(items, top, visible, width, height, listTop)
    if #items <= visible then
        return
    end

    local x = width - 8
    local trackY = listTop or ui.LIST_TOP
    local trackH = height - 8 - trackY
    display.rect(x, trackY, 4, trackH)

    local thumbH = math.max(10, math.floor(trackH * visible / #items))
    local maxTop = #items - visible
    local thumbY = trackY + math.floor((trackH - thumbH) * (top - 1) / maxTop)
    display.rect(x, thumbY, 4, thumbH, true)
end

-- A section label with a rule running out to the right margin. Occupies a full
-- row so the list geometry stays uniform -- a shorter header would mean every
-- offset, and the scrollbar, having to know which rows are which.
function ui.groupHeader(text, y, width)
    local label = text:upper()
    display.text(ui.MARGIN, y + 6, label, 1)

    local startX = ui.MARGIN + display.measure(label, 1) + 6
    display.rect(startX, y + 9, width - ui.MARGIN - startX, 1, true)
end

-- Walks to the next row that can actually be selected, skipping group headers.
-- Returns nil at either end, which is what makes the list stop there instead of
-- landing on a label.
function ui.nextSelectable(items, from, direction)
    local index = from + direction
    while index >= 1 and index <= #items do
        if not items[index].header then
            return index
        end
        index = index + direction
    end
    return nil
end

-- Right-aligned label on a row, for a version or a setting's value.
function ui.rowValue(text, y, width)
    if text == nil or text == "" then
        return
    end
    local textW = display.measure(text, 1)
    display.text(width - ui.MARGIN - 10 - textW, y + 4, text, 1)
end

--------------------------------------------------------------------- input --

-- One vocabulary for both controllers, so no screen has to know which is
-- attached. Event names carry their source; these tables map them to intent.
--
-- CONFIRM and BACK are what input.take() reports as an action. The four
-- direction tables are for apps still reading the raw event stream: take() has
-- already turned those events into dx, dy and wheel.
ui.DOWN = { rotary_cw = true, rotary_down = true, gamepad_down = true }
ui.UP = { rotary_ccw = true, rotary_up = true, gamepad_up = true }
ui.LEFT = { rotary_left = true, gamepad_left = true }
ui.RIGHT = { rotary_right = true, gamepad_right = true }
ui.CONFIRM = { rotary_select = true, gamepad_a = true, gamepad_start = true }
ui.BACK = { rotary_select_long = true, gamepad_b = true }

-- A digest read as lines of a list: the dial and the d-pad mean the same thing
-- here. A screen that walks a grid uses nav.dy and nav.wheel separately, which
-- is the only reason they arrive apart.
function ui.steps(nav)
    return nav.dy + nav.wheel
end

-- The band between the navbar and the footer.
function ui.bodyRegion(height, footer)
    local top = ui.NAV_HEIGHT + 2
    return { 0, top, nil, height - top - (footer or 0) }
end

-- The rectangle one list row occupies, matching the selection bar ui.list
-- draws. nil for anything that is not a row, which is how the launcher's menu
-- icon opts out.
function ui.rowRect(index, top, width, listTop)
    if index == nil or index < 1 then
        return nil
    end
    local y = (listTop or ui.LIST_TOP) + (index - top) * ui.ROW_HEIGHT
    return { ui.MARGIN, y - 4, width - 2 * ui.MARGIN, ui.ROW_HEIGHT }
end

-- The smallest box covering two rectangles, with a pixel of bleed. One region
-- refresh instead of two: the cost of a refresh is mostly fixed, so two small
-- ones are far worse than one slightly larger one.
--
-- nil for either argument gives nil -- there is no box covering "somewhere"
-- and a caller has to fall back to a bigger frame.
function ui.unionRect(a, b)
    if a == nil or b == nil then
        return nil
    end
    local x = math.min(a[1], b[1])
    local y = math.min(a[2], b[2])
    local right = math.max(a[1] + a[3], b[1] + b[3])
    local bottom = math.max(a[2] + a[4], b[2] + b[4])
    return { x - 1, y - 1, right - x + 2, bottom - y + 2 }
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
function ui.run(opts)
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

return ui
