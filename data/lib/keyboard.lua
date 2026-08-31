-- On-screen keyboard.
--
--     local keyboard = sys.import("/lib/keyboard.lua")
--     local password = keyboard.prompt{ title = "password", mask = true }
--
-- Returns the string, or nil if the user backed out. It takes over the screen
-- and the input loop until then, which is what keeps callers to two lines --
-- nothing else is happening while someone is typing.
--
-- The refresh strategy is the whole design. A whole-panel partial refresh is
-- about 400ms, so redrawing everything per keystroke would make a password take
-- a minute of waiting. Instead a cursor move repaints only the box containing
-- the two cells that changed, and a keypress repaints only the text field.
--
-- Both controllers are first class: the d-pad walks the grid in two dimensions,
-- and the dial walks the same cells in reading order, which is what a wheel is
-- good at. Neither user has to think in the other's terms.

local ui = sys.import("/lib/ui.lua")

local keyboard = {}

local W, H = display.size()

-- Character rows, per layer. Rows may differ in length; the column is clamped
-- when moving between them rather than the grid being padded with dead cells.
local LAYERS = {
    {
        "1234567890",
        "qwertyuiop",
        "asdfghjkl",
        "zxcvbnm.-_",
    },
    {
        "!\"#$%&'()*",
        "+,-./:;<=>",
        "?@[\\]^_`{|",
        "}~",
    },
}

local CHAR_ROWS = 4
local ACTION_ROW = CHAR_ROWS + 1

-- Actions live on their own row with wider cells, because "SPACE" and "DEL"
-- need to be readable and a single 36px cell cannot hold them.
local ACTIONS = { "SHIFT", "#+=", "SPACE", "DEL", "DONE" }
local ACTION_COUNT = #ACTIONS

local CELL_W, CELL_H = 36, 26
local GRID_X = (W - 10 * CELL_W) // 2
local GRID_Y = 80
local ROW_PITCH = 30

local ACTION_W = (W - 2 * ui.MARGIN) // ACTION_COUNT
local ACTION_Y = GRID_Y + CHAR_ROWS * ROW_PITCH + 10
local ACTION_H = 30

local FIELD_X, FIELD_Y = ui.MARGIN, 36
local FIELD_W, FIELD_H = W - 2 * ui.MARGIN, 28

----------------------------------------------------------------- geometry --

local function charCell(row, col)
    return GRID_X + (col - 1) * CELL_W, GRID_Y + (row - 1) * ROW_PITCH,
        CELL_W, CELL_H
end

local function actionCell(index)
    return ui.MARGIN + (index - 1) * ACTION_W, ACTION_Y, ACTION_W, ACTION_H
end

local function cellRect(row, col)
    if row == ACTION_ROW then
        return actionCell(col)
    end
    return charCell(row, col)
end

-- The smallest box covering two cells. One region refresh instead of two: each
-- refresh has a fixed cost, so two small ones are worse than one slightly
-- larger one.
local function unionRect(a, b)
    local ax, ay, aw, ah = a[1], a[2], a[3], a[4]
    local bx, by, bw, bh = b[1], b[2], b[3], b[4]
    local x = math.min(ax, bx)
    local y = math.min(ay, by)
    local right = math.max(ax + aw, bx + bw)
    local bottom = math.max(ay + ah, by + bh)
    return x - 1, y - 1, right - x + 2, bottom - y + 2
end

------------------------------------------------------------------- model --

local function newState(opts)
    return {
        layer = 1,
        shift = false,
        row = 2,
        col = 1,
        value = opts.value or "",
        title = opts.title or "input",
        mask = opts.mask and true or false,
        max = opts.max or 63,
    }
end

local function rows(state)
    return LAYERS[state.layer]
end

-- The character a cell carries, with shift applied. Digits are unaffected:
-- shifting them would mean a third layout to remember for no gain.
local function charAt(state, row, col)
    local line = rows(state)[row]
    if not line or col > #line then
        return nil
    end
    local char = line:sub(col, col)
    if state.shift then
        return char:upper()
    end
    return char
end

local function rowLength(state, row)
    if row == ACTION_ROW then
        return ACTION_COUNT
    end
    local line = rows(state)[row]
    return line and #line or 0
end

local function label(state, row, col)
    if row == ACTION_ROW then
        local text = ACTIONS[col]
        if text == "SHIFT" then
            return state.shift and "abc" or "ABC"
        elseif text == "#+=" then
            return state.layer == 1 and "#+=" or "abc"
        end
        return text
    end
    return charAt(state, row, col)
end

-- Every cell in reading order, so the dial can walk the same grid the d-pad
-- does without anyone having to model rows.
local function cellSequence(state)
    local cells = {}
    for row = 1, ACTION_ROW do
        for col = 1, rowLength(state, row) do
            cells[#cells + 1] = { row, col }
        end
    end
    return cells
end

local function indexOfCell(cells, row, col)
    for index, cell in ipairs(cells) do
        if cell[1] == row and cell[2] == col then
            return index
        end
    end
    return 1
end

----------------------------------------------------------------- drawing --

local function drawCell(state, row, col, focused)
    local x, y, w, h = cellRect(row, col)
    local text = label(state, row, col)
    if not text then
        return
    end

    if focused then
        display.rect(x, y, w, h, true)
        display.color("white")
    else
        display.rect(x, y, w, h)
    end

    local size = (row == ACTION_ROW or #text > 1) and 1 or 2
    local textW, textH = display.measure(text, size)
    display.text(x + (w - textW) // 2, y + (h - textH) // 2, text, size)

    if focused then
        display.color("black")
    end
end

local function drawField(state)
    display.rect(FIELD_X, FIELD_Y, FIELD_W, FIELD_H)

    local shown = state.value
    if state.mask then
        shown = string.rep("*", #shown)
    end

    -- Keep the tail visible: what was just typed matters more than the start,
    -- and there is no cursor to scroll to otherwise.
    local maxChars = (FIELD_W - 16) // 12
    if #shown > maxChars then
        shown = shown:sub(#shown - maxChars + 1)
    end

    display.text(FIELD_X + 8, FIELD_Y + 7, shown .. "_", 2)
end

local function drawGrid(state)
    for row = 1, ACTION_ROW do
        for col = 1, rowLength(state, row) do
            drawCell(state, row, col, row == state.row and col == state.col)
        end
    end
end

local function drawAll(state)
    display.begin("full")
    display.text(ui.MARGIN, 8, state.title, 2)
    drawField(state)
    drawGrid(state)
    ui.footer("select to type   long-press / B to cancel", W, H)
    display.show()
end

-- One region covering both cells. This is the hot path: it runs on every
-- cursor move, and it is why typing is bearable at all.
local function redrawMove(state, fromRow, fromCol)
    local x, y, w, h = unionRect({ cellRect(fromRow, fromCol) },
        { cellRect(state.row, state.col) })
    display.begin("partial", x, y, w, h)
    drawCell(state, fromRow, fromCol, false)
    drawCell(state, state.row, state.col, true)
    display.show()
end

local function redrawField(state)
    display.begin("partial", FIELD_X - 1, FIELD_Y - 1, FIELD_W + 2, FIELD_H + 2)
    drawField(state)
    display.show()
end

local function redrawGrid(state)
    local top = GRID_Y - 2
    local bottom = ACTION_Y + ACTION_H + 2
    display.begin("partial", 0, top, W, bottom - top)
    drawGrid(state)
    display.show()
end

------------------------------------------------------------------- input --

local function moveTo(state, row, col)
    if row == state.row and col == state.col then
        return false
    end

    local fromRow, fromCol = state.row, state.col
    state.row, state.col = row, col
    redrawMove(state, fromRow, fromCol)
    return true
end

-- Moving between rows keeps the column where it was, clamped to the new row.
-- Landing back on column 1 every time would make the grid feel like a list.
local function moveBy(state, dRow, dCol)
    local row = state.row + dRow
    if row < 1 or row > ACTION_ROW then
        return false
    end

    local col = state.col
    if dRow ~= 0 then
        if row == ACTION_ROW then
            -- Four narrow columns map onto five wide ones by proportion, so the
            -- action under the cursor is roughly the one it was above.
            col = math.min(ACTION_COUNT,
                1 + (state.col - 1) * ACTION_COUNT // rowLength(state, state.row))
        elseif state.row == ACTION_ROW then
            col = math.min(rowLength(state, row),
                1 + (state.col - 1) * rowLength(state, row) // ACTION_COUNT)
        end
    else
        col = col + dCol
    end

    local length = rowLength(state, row)
    if length == 0 then
        return false
    end
    if col < 1 then
        col = 1
    elseif col > length then
        col = length
    end

    return moveTo(state, row, col)
end

local function step(state, delta)
    local cells = cellSequence(state)
    local index = indexOfCell(cells, state.row, state.col) + delta
    if index < 1 then
        index = 1
    elseif index > #cells then
        index = #cells
    end
    return moveTo(state, cells[index][1], cells[index][2])
end

-- Returns "done", "cancel", or nil to keep going.
local function activate(state)
    if state.row ~= ACTION_ROW then
        local char = charAt(state, state.row, state.col)
        if char and #state.value < state.max then
            state.value = state.value .. char
            redrawField(state)
        end
        return nil
    end

    local action = ACTIONS[state.col]
    if action == "SHIFT" then
        state.shift = not state.shift
        redrawGrid(state)
    elseif action == "#+=" then
        state.layer = state.layer == 1 and 2 or 1
        -- The symbol layer's rows are shorter; keep the cursor on a real cell.
        state.row = math.min(state.row, CHAR_ROWS)
        state.col = math.min(state.col, math.max(1, rowLength(state, state.row)))
        redrawGrid(state)
    elseif action == "SPACE" then
        if #state.value < state.max then
            state.value = state.value .. " "
            redrawField(state)
        end
    elseif action == "DEL" then
        if #state.value > 0 then
            state.value = state.value:sub(1, #state.value - 1)
            redrawField(state)
        end
    elseif action == "DONE" then
        return "done"
    end
    return nil
end

------------------------------------------------------------------ public --

-- prompt{ title, value, mask, max } -> string | nil
function keyboard.prompt(opts)
    local state = newState(opts or {})
    drawAll(state)

    while true do
        local event = input.read(120000)

        if event == nil then
            -- Nothing typed for two minutes. Treat it as walking away rather
            -- than leaving a password field open on a screen that holds its
            -- image with no power.
            return nil
        elseif ui.BACK[event] then
            return nil
        elseif ui.CONFIRM[event] then
            local outcome = activate(state)
            if outcome == "done" then
                return state.value
            end
        elseif event == "rotary_cw" then
            step(state, 1)
        elseif event == "rotary_ccw" then
            step(state, -1)
        elseif ui.UP[event] then
            moveBy(state, -1, 0)
        elseif ui.DOWN[event] then
            moveBy(state, 1, 0)
        elseif ui.LEFT[event] then
            moveBy(state, 0, -1)
        elseif ui.RIGHT[event] then
            moveBy(state, 0, 1)
        elseif event == "gamepad_y" then
            -- A dedicated backspace: the most-used key should not need a trip
            -- to the action row.
            if #state.value > 0 then
                state.value = state.value:sub(1, #state.value - 1)
                redrawField(state)
            end
        end
    end
end

return keyboard
