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
//           circle(cx,cy,r[,fill]) / roundrect(x,y,w,h,r[,fill])
//           triangle(x0,y0,x1,y1,x2,y2[,fill])
//           size() -> w,h
//           show()  -- pushes the frame to the panel
//   input.read([timeoutMs]) -> string|nil
//       Event names carry their source controller:
//       rotary_cw, rotary_ccw, rotary_up, rotary_down, rotary_left,
//       rotary_right, rotary_select, rotary_select_long,
//       gamepad_up, gamepad_down, gamepad_left, gamepad_right,
//       gamepad_a, gamepad_b, gamepad_x, gamepad_y,
//       gamepad_start, gamepad_select
//         controllers() -> {rotary=bool, gamepad=bool}
//         state() -> {rotary  = {present, select, up, left, down, right,
//                                encoder},
//                     gamepad = {present, a, b, x, y, start, select,
//                                left, right, up, down, dx, dy,
//                                stick_x, stick_y}}
//       dx/dy are the stick's signed travel from centre (invert already
//       applied, so the sign matches left/right/up/down); stick_x/stick_y are
//       the raw ADC counts, for threshold and orientation calibration.
//       read() is edge-triggered and reports no releases; state() is the
//       level-triggered mirror, for "what is held right now". Gamepad
//       directions are in the board's own frame -- an app mounted at an angle
//       rotates them itself.
//   fs.list(dir) -> {{name=,dir=,size=},...} / read(path) -> string|nil
//         exists(path) -> bool / write(path, text) -> bool
//   sys.millis() / delay(ms) / log(msg) / launch(path) / exit()
//         memory() -> luaBytes, freeHeapBytes
//         temperature() -> degrees C (die temperature, internal sensor)
//         info() -> {chip, revision, cores, cpu_mhz, flash_bytes,
//                    psram_bytes, psram_free_bytes, heap_bytes,
//                    heap_free_bytes, heap_min_free_bytes, uptime_ms,
//                    reset_reason, version}
//         wifi() -> {connected[, ssid, ip, rssi]}
//
// The base library is opened without io, os or package, so fs.* is the only
// route to storage and every path passes through the sandbox check here.
namespace LuaBindings {

constexpr const char* kLogTag = "LuaApi";

// Registers every table into the wrapper's state. Call once per launch, before
// running the script.
void install(LuaWrapper& wrapper);

// Set by the host before a launch: fs writes are confined to this directory and
// reads to it plus the shared read-only roots. Empty disables the restriction,
// which the launcher needs to browse /sd/apps.
void setSandboxRoot(const String& root);

// Consumed by the host after a script exits: a script that called sys.launch()
// names the next app here.
String takeLaunchRequest();
bool hasLaunchRequest();

}  // namespace LuaBindings
