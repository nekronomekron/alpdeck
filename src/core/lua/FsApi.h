#pragma once

#include <LuaWrapper.h>

// The `fs` table. See docs/LUA_API.md for the contract.
namespace FsApi {

void install(lua_State* L);

}  // namespace FsApi
