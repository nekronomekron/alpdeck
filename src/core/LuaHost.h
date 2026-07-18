#pragma once

#include <Arduino.h>

#include <functional>

// Runs Lua scripts on their own FreeRTOS task, so a busy or wedged app cannot
// stall the main loop and take the network and FTP down with it.
//
// Exactly one script runs at a time. The launcher is simply the app that runs
// when nothing else does: it asks to launch something, exits, and the host then
// starts that app; when the app finishes the host brings the launcher back. A
// nested launch would mean two live lua_States and an ambiguous "back".
class LuaHost {
public:
    static constexpr const char* kLogTag = "LuaHost";

    enum class Exit : uint8_t {
        Returned,  // script ran to completion
        Failed,    // syntax or runtime error
        Cancelled, // stop() was honoured
        NotFound,  // script could not be read
    };

    struct Finished {
        Exit exit;
        String path;
        String message;  // traceback when exit == Failed
    };

    // Allocates the host primitives. Returns false when allocation fails, in
    // which case no script will ever run and the boot should not continue.
    static bool init();

    // Starts a script. Returns false when one is already running, the file is
    // missing, or the task could not be created. Non-blocking: the outcome of
    // the script itself arrives via onFinished.
    static bool run(const String& path);

    static bool isRunning();
    static String currentPath();

    // Asks a running script to unwind at its next VM hook. Cooperative: a
    // script trapped in a C binding will not notice until it returns.
    static void requestStop();

    // Fires on the main loop, never on the Lua task, so handlers can safely
    // start the next script.
    static void onFinished(std::function<void(const Finished&)> callback);

    // Delivers completions. Call from loop().
    static void loop();

private:
    static void task(void* param);
    static bool readScript(const String& path, String& source);

    static std::function<void(const Finished&)> _onFinished;
};
