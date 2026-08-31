#pragma once

#include <LuaWrapper.h>

// The `settings` table. See docs/LUA_API.md for the contract.
namespace SettingsApi {

void install(lua_State* L);

}  // namespace SettingsApi
