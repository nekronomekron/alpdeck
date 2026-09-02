#pragma once

#include <LuaWrapper.h>

// The `ftp` table. One binding today, and its own table anyway: an FTP login is
// not a property of `sys`, and a `wifi.ftp_configure` would have read as though
// it configured the radio.
namespace FtpApi {

void install(lua_State* L);

}  // namespace FtpApi
