#include "core/lua/WifiApi.h"

#include <WiFi.h>

#include "core/lua/LuaApi.h"
#include "net/NetworkService.h"

namespace WifiApi {
namespace {

// scan() -> { {ssid=, rssi=, open=}, ... }, strongest first
//
// Blocks for a couple of seconds. That is fine here and nowhere else: Lua runs
// on its own task, so the main loop, FTP and input polling all keep running.
int scan(lua_State* L) {
    if (!NetworkService::isEnabled()) {
        return luaL_error(L, "wifi is switched off");
    }

    constexpr int kMaxResults = 32;
    const int found = WiFi.scanNetworks();

    String seen[kMaxResults];
    int32_t bestRssi[kMaxResults];
    bool isOpen[kMaxResults];
    int count = 0;

    for (int index = 0; index < found; index++) {
        const String ssid = WiFi.SSID(index);
        if (ssid.isEmpty()) {
            continue;  // hidden network: nothing to show and nothing to pick
        }
        const int32_t rssi = WiFi.RSSI(index);
        const bool open = WiFi.encryptionType(index) == WIFI_AUTH_OPEN;

        // One row per name, keeping the strongest -- the same deduplication the
        // captive portal does, for the same reason: a mesh shows up repeatedly.
        int existing = -1;
        for (int j = 0; j < count; j++) {
            if (seen[j] == ssid) {
                existing = j;
                break;
            }
        }
        if (existing >= 0) {
            if (rssi > bestRssi[existing]) {
                bestRssi[existing] = rssi;
                isOpen[existing] = open;
            }
            continue;
        }
        if (count >= kMaxResults) {
            continue;
        }
        seen[count] = ssid;
        bestRssi[count] = rssi;
        isOpen[count] = open;
        count++;
    }

    WiFi.scanDelete();

    // Insertion sort by signal: the list is short and the strongest network is
    // almost always the one being looked for.
    for (int i = 1; i < count; i++) {
        for (int j = i; j > 0 && bestRssi[j] > bestRssi[j - 1]; j--) {
            const String ssid = seen[j];
            seen[j] = seen[j - 1];
            seen[j - 1] = ssid;
            const int32_t rssi = bestRssi[j];
            bestRssi[j] = bestRssi[j - 1];
            bestRssi[j - 1] = rssi;
            const bool open = isOpen[j];
            isOpen[j] = isOpen[j - 1];
            isOpen[j - 1] = open;
        }
    }

    lua_newtable(L);
    for (int i = 0; i < count; i++) {
        lua_newtable(L);
        LuaApi::setField(L, "ssid", seen[i].c_str());
        LuaApi::setField(L, "rssi", static_cast<lua_Integer>(bestRssi[i]));
        LuaApi::setField(L, "open", isOpen[i]);
        lua_seti(L, -2, i + 1);
    }
    return 1;
}

// wifi_configure(ssid, password) -- write-only.
//
// The credentials go straight into the network's own store and are never
// readable from Lua. Any app can call this, which is worth being plain about:
// it can break your connection, but it cannot learn your key.
int configure(lua_State* L) {
    const char* ssid = luaL_checkstring(L, 1);
    const char* password = luaL_optstring(L, 2, "");
    NetworkService::configure(ssid, password);
    return 0;
}

int forget(lua_State* L) {
    (void)L;
    NetworkService::forget();
    return 0;
}

int portal(lua_State* L) {
    (void)L;
    NetworkService::startSetupPortal();
    return 0;
}

// status() -> {enabled, connected, portal [, ssid, ip, rssi]}. Read-only:
// managing the connection stays with the kernel, not with apps.
int status(lua_State* L) {
    const bool connected = NetworkService::isConnected();

    lua_newtable(L);
    LuaApi::setField(L, "connected", connected);
    // Switched off is not the same as not connected, and the launcher draws
    // them differently.
    LuaApi::setField(L, "enabled", NetworkService::isEnabled());
    LuaApi::setField(L, "portal", NetworkService::isPortalActive());

    if (connected) {
        LuaApi::setField(L, "ssid", WiFi.SSID().c_str());
        LuaApi::setField(L, "ip", WiFi.localIP().toString().c_str());
        LuaApi::setField(L, "rssi", static_cast<lua_Integer>(WiFi.RSSI()));
    }
    return 1;
}

const luaL_Reg kFunctions[] = {
    {"status", status},
    {"scan", scan},
    {"configure", configure},
    {"forget", forget},
    {"portal", portal},
    {nullptr, nullptr},
};

}  // namespace

void install(lua_State* L) { LuaApi::installTable(L, "wifi", kFunctions); }

}  // namespace WifiApi
