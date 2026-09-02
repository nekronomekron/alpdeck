#include "core/lua/FtpApi.h"

#include "core/lua/LuaApi.h"
#include "net/FtpService.h"

namespace FtpApi {
namespace {

// configure(user, password) -- write-only: nothing reads the password back
// out, for the same reason the WiFi key is not readable.
int configure(lua_State* L) {
    const char* user = luaL_checkstring(L, 1);
    const char* password = luaL_checkstring(L, 2);
    FtpService::configure(user, password);
    return 0;
}

const luaL_Reg kFunctions[] = {
    {"configure", configure},
    {nullptr, nullptr},
};

}  // namespace

void install(lua_State* L) { LuaApi::installTable(L, "ftp", kFunctions); }

}  // namespace FtpApi
