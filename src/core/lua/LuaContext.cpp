#include "core/lua/LuaContext.h"

namespace LuaContext {
namespace {

String root;
String pendingLaunch;

}  // namespace

void setSandboxRoot(const String& value) { root = value; }

const String& sandboxRoot() { return root; }

void requestLaunch(const String& path) { pendingLaunch = path; }

String takeLaunchRequest() {
    const String request = pendingLaunch;
    pendingLaunch = "";
    return request;
}

bool hasLaunchRequest() { return !pendingLaunch.isEmpty(); }

}  // namespace LuaContext
