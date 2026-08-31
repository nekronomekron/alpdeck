#pragma once

#include <Arduino.h>
#include <LuaWrapper.h>

// The `fs` table. See docs/LUA_API.md for the contract.
namespace FsApi {

void install(lua_State* L);

// Resolves a path the way every fs call does: absolute as given, relative
// against the running app's own directory, '..' refused. Returns an empty
// string on refusal and sets `reason`. Shared with sys.import so a module path
// and a data path obey one rule.
String resolvePath(const String& path, const char** reason);

}  // namespace FsApi
