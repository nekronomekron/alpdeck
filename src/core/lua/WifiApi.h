#pragma once

#include <LuaWrapper.h>

// The `wifi` table: what a script may know and ask about the radio.
//
// Its own table rather than a prefix inside `sys`, because these are the one
// group of bindings that are not about the machine the script runs on. What it
// deliberately does NOT have is an on/off switch: that is settings.set
// ("wifi_enabled"), so the kernel decides what a stated intent means for the
// hardware. Reporting lives here, managing does not.
namespace WifiApi {

void install(lua_State* L);

}  // namespace WifiApi
