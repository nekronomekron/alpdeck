#pragma once

#include <Arduino.h>

// State that belongs to one launch rather than to any single API table: where
// the running script may write, and which app it has asked to run next.
//
// The host sets the root before a launch and reads the request after the VM is
// gone; the fs and sys bindings are the only other users.
namespace LuaContext {

// fs writes are confined to this directory, and relative paths resolve against
// it. Empty means "no app directory", which is the launcher's case: it browses
// and never writes.
void setSandboxRoot(const String& root);
const String& sandboxRoot();

// Recorded by sys.launch(). The script is expected to return afterwards; the
// host reads this once the VM has been torn down, which is what keeps exactly
// one lua_State alive at a time.
void requestLaunch(const String& path);
String takeLaunchRequest();
bool hasLaunchRequest();

}  // namespace LuaContext
