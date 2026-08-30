#pragma once

#include <LuaWrapper.h>

// The `display` table. See docs/LUA_API.md for the contract.
namespace DisplayApi {

void install(lua_State* L);

}  // namespace DisplayApi
