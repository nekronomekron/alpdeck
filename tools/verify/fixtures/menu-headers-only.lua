-- Regression fixture: a menu of group labels and nothing selectable.
--
-- The other half of the empty-menu bug. Here there ARE rows, so the old code
-- got past items[1], and then ui.firstSelectable has nowhere to put the cursor.
-- The screen has to draw the labels, show no selection bar, and still leave
-- when the user presses back.
local menu = sys.import("/lib/menu.lua")

menu.run{
    title = "labels only",
    items = {
        { header = true, label = "first" },
        { header = true, label = "second" },
    },
    footer = "nothing selectable",
}
