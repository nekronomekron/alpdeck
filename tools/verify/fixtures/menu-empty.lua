-- Regression fixture: a menu with no rows at all must draw, not crash.
--
-- Not a shipped script. menu.run used to index items[1] before checking there
-- was one, so a menu built from something that can come back empty -- a wifi
-- scan, a directory listing -- took the launcher down. Every caller in the
-- launcher guards against that before it gets here, so the state is only
-- reachable by asking for it directly, which is exactly why it needs a golden.
local menu = sys.import("/lib/menu.lua")

menu.run{
    title = "empty menu",
    items = {},
    footer = "nothing here",
}
