#pragma once

#include <LuaWrapper.h>

// The `sys` table. See docs/LUA_API.md for the contract.
namespace SysApi {

void install(lua_State* L);

}  // namespace SysApi
