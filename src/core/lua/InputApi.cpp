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

// take([timeoutMs]) -> {dx =, dy =, wheel =, action =} or nil on timeout.
//
// The coalescing counterpart to read(), and what every screen should use. A
// refresh measures 609ms on this panel, and a user turning the dial through one
// puts a dozen events behind it; this hands back where they ended up instead of
// a queue to replay 609ms at a time.
//
// dy and wheel are kept apart because they are not always the same gesture: on
// a list they both mean "a line", but on a grid the d-pad walks rows while the
// dial walks cells in reading order.
int take(lua_State* L) {
    const lua_Integer raw = luaL_optinteger(L, 1, 0);
    const uint32_t timeoutMs = raw < 0 ? 0 : static_cast<uint32_t>(raw);

    const Input::Digest digest = Input::take(timeoutMs);

    // Nil rather than a table of zeroes: a screen wants to tell "nothing
    // happened for a minute" from "the user moved", and comparing four fields
    // to say so at every call site is how that check gets forgotten.
    if (!digest.any()) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    LuaApi::setField(L, "dx", static_cast<lua_Integer>(digest.navX));
    LuaApi::setField(L, "dy", static_cast<lua_Integer>(digest.navY));
    LuaApi::setField(L, "wheel", static_cast<lua_Integer>(digest.wheel));
    if (digest.action != Input::Event::None) {
        LuaApi::setField(L, "action", Input::eventName(digest.action));
    }
    return 1;
}

// flush() -- drop everything buffered.
//
// For the moment a screen is replaced: opening a menu, returning from one,
// starting to type. Whatever the user did to the old screen was aimed at the
// old screen, and letting it land on the new one moves a cursor nobody was
// watching.
int flush(lua_State* L) {
    (void)L;
    Input::flush();
    return 0;
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
    {"take", take},
    {"flush", flush},
    {"state", state},
    {"controllers", controllers},
    {nullptr, nullptr},
};

}  // namespace

void install(lua_State* L) { LuaApi::installTable(L, "input", kFunctions); }

}  // namespace InputApi
