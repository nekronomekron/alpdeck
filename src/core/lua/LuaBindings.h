#pragma once

class LuaWrapper;

// Installs the four API tables into a fresh Lua state.
//
// The tables themselves live one file each -- DisplayApi, InputApi, FsApi,
// SysApi -- and the contract they implement is docs/LUA_API.md, which is the
// document to read and to keep current. Per-launch state (sandbox root, launch
// request) lives in LuaContext.
namespace LuaBindings {

// Call once per launch, before running the script.
void install(LuaWrapper& wrapper);

}  // namespace LuaBindings
