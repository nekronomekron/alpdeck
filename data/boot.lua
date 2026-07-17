-- Runs once at the end of setup(), before the launcher starts.
--
-- This is the user hook: whatever is here happens on every boot. Returning
-- normally hands control to the launcher. To boot straight into one app
-- instead, call sys.launch("/sd/apps/<name>/main.lua") and return -- the host
-- honours the request and skips the launcher.

sys.log("boot.lua: hello from lua")
