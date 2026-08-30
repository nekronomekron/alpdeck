#include "core/lua/InputApi.h"

#include "core/lua/LuaApi.h"
#include "peripherals/Input.h"

namespace InputApi {
namespace {

int read(lua_State* L) {
    // A negative timeout would wrap to ~49 days once cast; treat it as "poll".
    const lua_Integer raw = luaL_optinteger(L, 1, 0);
    const uint32_t timeoutMs = raw < 0 ? 0 : static_cast<uint32_t>(raw);

    // Blocks this task, not the main loop -- which is the whole point of
    // running apps off-task. It also lets the idle task run.
    const Input::Event event = Input::read(timeoutMs);
    if (event == Input::Event::None) {
        lua_pushnil(L);
    } else {
        lua_pushstring(L, Input::eventName(event));
    }
    return 1;
}

// controllers() -> {rotary = bool, gamepad = bool}. Both are optional and the
// device runs on either alone, so an app that draws or maps them per
// controller has to ask rather than assume.
int controllers(lua_State* L) {
    lua_newtable(L);
    LuaApi::setField(L, "rotary", Input::hasRotary());
    LuaApi::setField(L, "gamepad", Input::hasGamepad());
    return 1;
}

// state() -> {rotary = {...}, gamepad = {...}} -- what is held *right now*.
//
// read() is edge-triggered: it reports a press and never a release, so it
// cannot drive a "this button is currently down" display. This returns the
// level-triggered mirror the main loop keeps instead. Directions are in the
// gamepad board's own frame, matching the gamepad_* event names; an app
// mounted at an angle rotates them itself.
int state(lua_State* L) {
    const Input::Snapshot snapshot = Input::snapshot();

    lua_newtable(L);

    lua_newtable(L);
    LuaApi::setField(L, "present", snapshot.hasRotary);
    LuaApi::setField(L, "select", snapshot.rotarySelect);
    LuaApi::setField(L, "up", snapshot.rotaryUp);
    LuaApi::setField(L, "left", snapshot.rotaryLeft);
    LuaApi::setField(L, "down", snapshot.rotaryDown);
    LuaApi::setField(L, "right", snapshot.rotaryRight);
    LuaApi::setField(L, "encoder",
                     static_cast<lua_Integer>(snapshot.rotaryEncoder));
    lua_setfield(L, -2, "rotary");

    lua_newtable(L);
    LuaApi::setField(L, "present", snapshot.hasGamepad);
    LuaApi::setField(L, "a", snapshot.gamepadA);
    LuaApi::setField(L, "b", snapshot.gamepadB);
    LuaApi::setField(L, "x", snapshot.gamepadX);
    LuaApi::setField(L, "y", snapshot.gamepadY);
    LuaApi::setField(L, "start", snapshot.gamepadStart);
    LuaApi::setField(L, "select", snapshot.gamepadSelect);
    LuaApi::setField(L, "left", snapshot.gamepadAxisX < 0);
    LuaApi::setField(L, "right", snapshot.gamepadAxisX > 0);
    LuaApi::setField(L, "up", snapshot.gamepadAxisY < 0);
    LuaApi::setField(L, "down", snapshot.gamepadAxisY > 0);
    LuaApi::setField(L, "dx",
                     static_cast<lua_Integer>(snapshot.gamepadDeflectionX));
    LuaApi::setField(L, "dy",
                     static_cast<lua_Integer>(snapshot.gamepadDeflectionY));
    LuaApi::setField(L, "stick_x",
                     static_cast<lua_Integer>(snapshot.gamepadStickX));
    LuaApi::setField(L, "stick_y",
                     static_cast<lua_Integer>(snapshot.gamepadStickY));
    lua_setfield(L, -2, "gamepad");

    return 1;
}

const luaL_Reg kFunctions[] = {
    {"read", read},
    {"state", state},
    {"controllers", controllers},
    {nullptr, nullptr},
};

}  // namespace

void install(lua_State* L) { LuaApi::installTable(L, "input", kFunctions); }

}  // namespace InputApi
