#include "core/lua/SettingsApi.h"

#include "core/Settings.h"
#include "core/lua/LuaApi.h"

namespace SettingsApi {
namespace {

// get(name) -> boolean or number, by the key's declared type.
//
// No default argument: the kernel declares the default, so Lua cannot hold a
// different opinion about what an unset key means. An unknown key is an error
// rather than nil, because a typo that silently reads as "off" is exactly the
// bug this schema exists to prevent.
int get(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const Settings::Key* key = Settings::find(name);
    if (key == nullptr) {
        return luaL_error(L, "unknown setting '%s'", name);
    }

    if (key->type == Settings::Type::Bool) {
        lua_pushboolean(L, Settings::getBool(name));
    } else {
        lua_pushinteger(L, Settings::getInt(name));
    }
    return 1;
}

// set(name, value) -> boolean
//
// Writing is stating an intent, not performing an action: the kernel watches
// this store and decides what a change means for the hardware. That is what
// keeps radio management out of Lua while still letting the launcher own the
// settings UI.
int set(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const Settings::Key* key = Settings::find(name);
    if (key == nullptr) {
        return luaL_error(L, "unknown setting '%s'", name);
    }

    bool ok = false;
    if (key->type == Settings::Type::Bool) {
        luaL_checkany(L, 2);
        ok = Settings::setBool(name, lua_toboolean(L, 2));
    } else {
        const lua_Integer value = luaL_checkinteger(L, 2);
        ok = Settings::setInt(name, static_cast<int32_t>(value));
    }

    lua_pushboolean(L, ok);
    return 1;
}

// keys() -> { {name=, type="bool"|"int", min=, max=}, ... }
//
// So a settings screen can validate and step values without duplicating the
// schema. Labels and ordering stay in Lua, where a typo costs a file copy
// rather than a reflash.
int keys(lua_State* L) {
    size_t count = 0;
    const Settings::Key* declared = Settings::all(count);

    lua_newtable(L);
    for (size_t index = 0; index < count; index++) {
        const Settings::Key& key = declared[index];

        lua_newtable(L);
        LuaApi::setField(L, "name", key.name);
        LuaApi::setField(
            L, "type", key.type == Settings::Type::Bool ? "bool" : "int");
        if (key.type == Settings::Type::Int) {
            LuaApi::setField(L, "min", static_cast<lua_Integer>(key.minimum));
            LuaApi::setField(L, "max", static_cast<lua_Integer>(key.maximum));
        }
        lua_seti(L, -2, static_cast<lua_Integer>(index + 1));
    }
    return 1;
}

const luaL_Reg kFunctions[] = {
    {"get", get},
    {"set", set},
    {"keys", keys},
    {nullptr, nullptr},
};

}  // namespace

void install(lua_State* L) {
    LuaApi::installTable(L, "settings", kFunctions);
}

}  // namespace SettingsApi
