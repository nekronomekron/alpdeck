#ifndef LUA_WRAPPER_H
#define LUA_WRAPPER_H

#include "Arduino.h"

#define LUA_USE_C89
#include "lua/lua.hpp"

// One instance owns one lua_State. Construct per app launch and destroy on exit
// so every app starts from a clean global table and nothing an app leaves
// behind can reach the next one.
class LuaWrapper {
public:
    LuaWrapper();
    ~LuaWrapper();

    // Non-copyable: the instance owns a raw lua_State.
    LuaWrapper(const LuaWrapper&) = delete;
    LuaWrapper& operator=(const LuaWrapper&) = delete;

    enum class Result : uint8_t {
        Ok,
        Error,     // syntax or runtime error; see the returned message
        Cancelled, // stop() was requested and the VM unwound
    };

    // Runs source, returning Ok and an empty message on success. On failure the
    // message carries a Lua traceback, not just the error line. chunkName is
    // what tracebacks report, so pass the script's path.
    //
    // Loading the source is the caller's job: apps live on SD and the launcher
    // on LittleFS, and the wrapper stays filesystem-agnostic.
    Result run(const String& source, const String& chunkName, String& message);

    // Asks the VM to unwind at its next hook. Safe to call from another task:
    // the flag is only read by the hook, on the Lua task. Cooperative by
    // design -- killing the task outright could strand the shared SPI lock.
    void stop();
    bool stopRequested() const;

    void registerFunction(const char* name, lua_CFunction function);

    // Exposes the raw state for binding registration.
    lua_State* state() const { return _state; }

    // Bytes currently allocated by the VM, for diagnostics.
    size_t memoryUsed() const;

private:
    static void* allocate(void* ud, void* ptr, size_t osize, size_t nsize);
    static void hook(lua_State* state, lua_Debug* debug);
    static int messageHandler(lua_State* state);

    Result finish(int status, String& message);

    lua_State* _state = nullptr;
    volatile bool _stopRequested = false;
    uint32_t _lastYieldMs = 0;
};

#endif
