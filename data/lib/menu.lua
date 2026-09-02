-- A modal list screen: draws itself, runs its own loop, returns when the user
-- backs out.
--
--     local menu = sys.import("/lib/menu.lua")
--     menu.run{ title = "options", items = { ... } }
--
-- Every sub-screen the launcher has is one of these, which is why they all
-- behave the same way, and why an app that wants a settings screen writes a
-- table of rows rather than a loop.

local ui = sys.import("/lib/ui.lua")
local screen = sys.import("/lib/screen.lua")

local menu = {}

local W, H = display.size()

-- The band a menu leaves for its footer. Rows are laid out against a shorter
-- panel so the last one cannot slide under the hint line.
local FOOTER_H = 24

-- opts: title, items, footer. An item is
--   {label=, value=, action=, disabled=, header=, screen=}
-- where `screen` marks a row whose action takes the panel over -- a keyboard,
-- a message, another menu. Those need a repaint afterwards; a row that only
-- changes a setting does not, because the value is part of the signature and
-- redraws like any other change.
function menu.run(opts)
    local listTop = ui.LIST_TOP
    local visible = ui.visibleRows(H - FOOTER_H, listTop)

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
            height = H - FOOTER_H,
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

        top = ui.scrollTo(top, selected, visible)

        -- Scrolling up onto the first row of a group brings its label along,
        -- otherwise the section arrives unlabelled. Only when the selection is
        -- at the top of the window: shifting it in any other case would push
        -- the selection off the bottom.
        if selected == top and top > 1 and opts.items[top - 1].header then
            top = top - 1
        end
    end

    local function apply(nav)
        local steps = screen.steps(nav)
        if steps ~= 0 then
            moveBy(steps)
        end

        if nav.action and screen.BACK[nav.action] then
            return "close"
        end

        -- Select and right both mean "forward", left means "back one value".
        -- The dial's travel goes through as a count, so a setting steps by as
        -- far as the user actually turned rather than by one per refresh.
        local direction = (nav.action and screen.CONFIRM[nav.action]) and 1 or nav.dx
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

    screen.run{
        idleMs = screen.IDLE_MS,
        body = ui.bodyRegion(H, FOOTER_H),
        cursorRect = function(index)
            return ui.rowRect(index, top, W, listTop)
        end,
        signature = signature,
        draw = draw,
        apply = apply,
        onIdle = function() return "close" end,
    }
end

return menu
