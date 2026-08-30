#pragma once

#include <LuaWrapper.h>

// Small helpers shared by the four API tables. Filling a Lua table field by
// field is the bulk of every binding, and spelling out push/setfield pairs
// each time buries the interesting part.
namespace LuaApi {

constexpr const char* kLogTag = "LuaApi";

// This document's version: docs/LUA_API.md. Bump on every breaking change.
constexpr lua_Integer kApiVersion = 1;

inline void setField(lua_State* L, const char* key, bool value) {
    lua_pushboolean(L, value);
    lua_setfield(L, -2, key);
}

inline void setField(lua_State* L, const char* key, lua_Integer value) {
    lua_pushinteger(L, value);
    lua_setfield(L, -2, key);
}

inline void setField(lua_State* L, const char* key, const char* value) {
    lua_pushstring(L, value);
    lua_setfield(L, -2, key);
}

inline void installTable(lua_State* L, const char* name,
                         const luaL_Reg* functions) {
    lua_newtable(L);
    luaL_setfuncs(L, functions, 0);
    lua_setglobal(L, name);
}

}  // namespace LuaApi
