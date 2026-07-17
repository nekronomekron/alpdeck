#pragma once

#include <Arduino.h>

class LuaWrapper;

// The API every Lua app sees. This is a public contract: once apps use it,
// changing a signature breaks them, so prefer adding over reshaping.
//
// Tables installed:
//   display.clear([full])  -- opens the frame AND picks its refresh mode;
//                             partial (~400ms) unless full (~1200ms, clears
//                             ghosting). The mode cannot be changed later.
//           text(x,y,s[,size[,invert]]) -- invert draws white, for text on a
//                                          filled rect
//           rect(x,y,w,h[,fill]) / line(x1,y1,x2,y2) / pixel(x,y)
//           size() -> w,h
//           show()  -- pushes the frame to the panel
//   input.read([timeoutMs]) -> string|nil    ("cw","ccw","up","down","left",
//                                             "right","select","select_long")
//   fs.list(dir) -> {{name=,dir=,size=},...} / read(path) -> string|nil
//         exists(path) -> bool / write(path, text) -> bool
//   sys.millis() / delay(ms) / log(msg) / launch(path) / exit()
//         memory() -> luaBytes, freeHeapBytes
//
// The base library is opened without io, os or package, so fs.* is the only
// route to storage and every path passes through the sandbox check here.
class LuaBindings {
public:
    static constexpr const char* kLogTag = "LuaApi";

    // Registers every table into the wrapper's state. Call once per launch,
    // before running the script.
    static void install(LuaWrapper& wrapper);

    // Set by the host before a launch: fs writes are confined to this directory
    // and reads to it plus the shared read-only roots. Empty disables the
    // restriction, which the launcher needs to browse /sd/apps.
    static void setSandboxRoot(const String& root);

    // Consumed by the host after a script exits: a script that called
    // sys.launch() names the next app here.
    static String takeLaunchRequest();
    static bool hasLaunchRequest();
};
