#pragma once

#include <LuaWrapper.h>

// The `input` table. See docs/LUA_API.md for the contract.
namespace InputApi {

void install(lua_State* L);

}  // namespace InputApi
