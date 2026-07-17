#include "LuaWrapper.h"

#include <esp_heap_caps.h>

// Tripwire for the LUA_32BITS build flag. If it is dropped, this TU and the Lua
// library would disagree on sizeof(lua_Integer) and integers would cross the C
// boundary corrupted -- a bug that is invisible until an integer is actually
// passed. Fail the build instead.
static_assert(sizeof(lua_Integer) == 4,
              "lua_Integer must be 32-bit; add -DLUA_32BITS to build_flags");

namespace {
// The VM hook fires this often. Small enough that a stop request is noticed
// promptly, large enough that the check does not dominate execution.
constexpr int kHookInstructionCount = 1000;

// A script that never blocks would starve the idle task and trip the task
// watchdog, so the hook yields on this interval regardless of what it is doing.
constexpr uint32_t kYieldIntervalMs = 50;

// Marks the error raised by a stop request, so it can be told apart from a
// genuine script error as it unwinds.
constexpr const char* kCancelMarker = "@alpdeck:cancelled";

// An unprotected API error would otherwise reach Lua's default panic handler,
// which returns and lets the C library abort() -- rebooting the whole device
// because one app misbehaved. Everything we run goes through lua_pcall, so this
// should be unreachable; it exists so that "should" cannot cost a reboot.
extern "C" int lua_wrapper_panic(lua_State* L) {
    const char* message = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1)
                                                         : "non-string error";
    Serial.printf("[ERROR][Lua] PANIC: %s\n", message);
    return 0;
}

// Installs one standard library and binds its global name.
//
// Calling luaopen_*() directly does NOT work for most libraries: luaopen_base
// installs into _G itself, but luaopen_math/string/table build a table with
// luaL_newlib and merely leave it on the stack, so the global stays nil and the
// table leaks. luaL_requiref is what binds the name -- this is exactly what
// Lua's own linit.c does.
void openLibrary(lua_State* state, const char* name, lua_CFunction opener) {
    luaL_requiref(state, name, opener, 1);  // 1 = also store as a global
    lua_pop(state, 1);                      // requiref leaves the module behind
}

extern "C" int lua_wrapper_print(lua_State* L) {
    const int count = lua_gettop(L);
    lua_getglobal(L, "tostring");
    for (int i = 1; i <= count; i++) {
        lua_pushvalue(L, -1);
        lua_pushvalue(L, i);
        lua_call(L, 1, 1);

        size_t length;
        const char* text = lua_tolstring(L, -1, &length);
        if (text == nullptr) {
            return luaL_error(L, "'tostring' must return a string to 'print'");
        }
        if (i > 1) {
            Serial.write("\t");
        }
        Serial.write(text);
        lua_pop(L, 1);
    }
    Serial.println();
    return 0;
}
}  // namespace

LuaWrapper::LuaWrapper() {
    // This Lua takes a hash seed; luaL_newstate is not usable here because it
    // hardcodes its own allocator.
    _state = lua_newstate(allocate, nullptr, luaL_makeseed(nullptr));
    if (_state == nullptr) {
        return;  // out of memory; every entry point below tolerates a null state
    }

    lua_atpanic(_state, lua_wrapper_panic);

    // Lets the hook and any binding recover the owning wrapper from the state.
    *static_cast<LuaWrapper**>(lua_getextraspace(_state)) = this;

    // Deliberately partial: no io, os or package. Those would hand apps an
    // unsandboxed path to the filesystem and to process control, which is what
    // the fs/sys bindings exist to mediate. Consequence: `require` is
    // unavailable, so an app is a single file.
    openLibrary(_state, LUA_GNAME, luaopen_base);
    openLibrary(_state, LUA_TABLIBNAME, luaopen_table);
    openLibrary(_state, LUA_STRLIBNAME, luaopen_string);
    openLibrary(_state, LUA_MATHLIBNAME, luaopen_math);

    lua_register(_state, "print", lua_wrapper_print);

    // Set as real globals rather than prepended to the source. The old wrapper
    // concatenated four lines of constants ahead of every chunk, which shifted
    // every line number in every error message by four.
    lua_pushinteger(_state, INPUT);
    lua_setglobal(_state, "INPUT");
    lua_pushinteger(_state, OUTPUT);
    lua_setglobal(_state, "OUTPUT");
    lua_pushinteger(_state, LOW);
    lua_setglobal(_state, "LOW");
    lua_pushinteger(_state, HIGH);
    lua_setglobal(_state, "HIGH");
}

LuaWrapper::~LuaWrapper() {
    if (_state != nullptr) {
        lua_close(_state);
        _state = nullptr;
    }
}

void* LuaWrapper::allocate(void* ud, void* ptr, size_t osize, size_t nsize) {
    (void)ud;
    (void)osize;

    if (nsize == 0) {
        heap_caps_free(ptr);
        return nullptr;
    }

    // Prefer PSRAM: app data then cannot exhaust internal RAM, which the WiFi
    // stack and FreeRTOS also draw from. Falls back to internal when the board
    // has no PSRAM, so the same build still runs.
    void* block = heap_caps_realloc(ptr, nsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (block == nullptr) {
        block = heap_caps_realloc(ptr, nsize, MALLOC_CAP_8BIT);
    }
    return block;
}

void LuaWrapper::hook(lua_State* state, lua_Debug* debug) {
    (void)debug;

    LuaWrapper* self = *static_cast<LuaWrapper**>(lua_getextraspace(state));
    if (self == nullptr) {
        return;
    }

    if (self->_stopRequested) {
        // Unwinds through lua_pcall like any error, so destructors and the
        // state's own cleanup still run. Deleting the task instead could strand
        // the SPI mutex and hang the display and SD for the whole device.
        luaL_error(state, kCancelMarker);
        return;
    }

    const uint32_t now = millis();
    if (now - self->_lastYieldMs >= kYieldIntervalMs) {
        self->_lastYieldMs = now;
        vTaskDelay(1);
    }
}

int LuaWrapper::messageHandler(lua_State* state) {
    const char* message = lua_tostring(state, 1);

    if (message == nullptr) {
        if (luaL_callmeta(state, 1, "__tostring") &&
            lua_type(state, -1) == LUA_TSTRING) {
            return 1;
        }
        message = lua_pushfstring(state, "(error object is a %s value)",
                                  luaL_typename(state, 1));
    }

    // Attaches a traceback while the failing stack is still live; it is gone by
    // the time lua_pcall returns.
    luaL_traceback(state, state, message, 1);
    return 1;
}

LuaWrapper::Result LuaWrapper::run(const String& source, const String& chunkName,
                                   String& message) {
    if (_state == nullptr) {
        message = "lua state could not be created (out of memory)";
        return Result::Error;
    }

    _stopRequested = false;
    _lastYieldMs = millis();

    lua_sethook(_state, hook, LUA_MASKCOUNT, kHookInstructionCount);

    lua_settop(_state, 0);
    lua_pushcfunction(_state, messageHandler);
    const int handlerIndex = lua_gettop(_state);

    // '=' prefix keeps the name verbatim in errors, instead of Lua quoting the
    // whole source as [string "..."].
    const String chunk = "=" + chunkName;

    int status = luaL_loadbuffer(_state, source.c_str(), source.length(),
                                 chunk.c_str());
    if (status == LUA_OK) {
        status = lua_pcall(_state, 0, 0, handlerIndex);
    }

    return finish(status, message);
}

LuaWrapper::Result LuaWrapper::finish(int status, String& message) {
    if (status == LUA_OK) {
        message = "";
        lua_settop(_state, 0);
        return Result::Ok;
    }

    const char* text = lua_tostring(_state, -1);
    message = text != nullptr ? text : "unknown error";
    lua_settop(_state, 0);

    if (_stopRequested && message.indexOf(kCancelMarker) >= 0) {
        message = "";
        return Result::Cancelled;
    }
    return Result::Error;
}

void LuaWrapper::stop() { _stopRequested = true; }

bool LuaWrapper::stopRequested() const { return _stopRequested; }

void LuaWrapper::registerFunction(const char* name, lua_CFunction function) {
    if (_state != nullptr) {
        lua_register(_state, name, function);
    }
}

size_t LuaWrapper::memoryUsed() const {
    if (_state == nullptr) {
        return 0;
    }
    return static_cast<size_t>(lua_gc(_state, LUA_GCCOUNT)) * 1024 +
           static_cast<size_t>(lua_gc(_state, LUA_GCCOUNTB));
}
