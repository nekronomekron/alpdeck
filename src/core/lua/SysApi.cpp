#include "core/lua/SysApi.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include "config/AppConfig.h"
#include "core/Vfs.h"
#include "core/lua/FsApi.h"
#include "core/lua/LuaApi.h"
#include "core/lua/LuaContext.h"
#include "net/FtpService.h"
#include "net/NetworkService.h"
#include "utils/Logger.h"

namespace SysApi {
namespace {

int millisSinceBoot(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(millis()));
    return 1;
}

int delay(lua_State* L) {
    // Same wrap hazard as input.read: a negative delay must not become ~49
    // days of sleep on a task the host can only stop cooperatively.
    const lua_Integer raw = luaL_checkinteger(L, 1);
    const uint32_t ms = raw < 0 ? 0 : static_cast<uint32_t>(raw);
    vTaskDelay(pdMS_TO_TICKS(ms));  // yields the task rather than spinning
    return 0;
}

int log(lua_State* L) {
    LOGI("LuaApp", "%s", luaL_checkstring(L, 1));
    return 0;
}

int appdir(lua_State* L) {
    lua_pushstring(L, LuaContext::sandboxRoot().c_str());
    return 1;
}

int launch(lua_State* L) {
    LuaContext::requestLaunch(luaL_checkstring(L, 1));
    // Only records the request. The script is expected to return afterwards;
    // the host reads it once the VM has been torn down, which is what keeps
    // exactly one lua_State alive at a time.
    return 0;
}

// restart() -- reboots the device. Does not return.
//
// The panel is left holding whatever was last drawn, which on e-paper means it
// stays readable through the reboot; the caller is expected to have put
// something honest on it first, because a stale menu sitting there for the two
// seconds of a boot looks like a device that has hung.
int restart(lua_State* L) {
    (void)L;
    LOGI("Sys", "Restart requested by a script");
    Serial.flush();
    ESP.restart();
    return 0;
}

// sd_remount() -> bool -- re-reads the SD card, and reports whether one is
// there afterwards.
//
// For a card seated after boot, or swapped, or written to over a card reader.
// Nothing else can pick that up: the mount happens once during boot, and every
// /sd path resolves through it.
int sdRemount(lua_State* L) {
    const bool mounted = Vfs::mountSd();

    // FTP built its filesystem list at start-up from the old answer, so it has
    // to be told; without this a card mounted now stays invisible over the
    // network until the next reboot.
    FtpService::remount();

    lua_pushboolean(L, mounted);
    return 1;
}

int exit(lua_State* L) {
    LuaWrapper* self = *static_cast<LuaWrapper**>(lua_getextraspace(L));
    if (self != nullptr) {
        self->stop();  // the VM hook unwinds within a few hundred instructions
    }
    return 0;
}

int memory(lua_State* L) {
    LuaWrapper* self = *static_cast<LuaWrapper**>(lua_getextraspace(L));
    lua_pushinteger(
        L, self != nullptr ? static_cast<lua_Integer>(self->memoryUsed()) : 0);
    lua_pushinteger(
        L, static_cast<lua_Integer>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
    return 2;
}

// temperature() -> degrees Celsius from the ESP32-S3's internal sensor. Reads
// the die, not the room: expect several degrees above ambient under load.
int temperature(lua_State* L) {
    lua_pushnumber(L, temperatureRead());
    return 1;
}

const char* resetReasonName() {
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
        return "poweron";
    case ESP_RST_EXT:
        return "external";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
        return "watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deepsleep";
    case ESP_RST_BROWNOUT:
        return "brownout";  // supply dipped: bad cable, empty battery
    case ESP_RST_SDIO:
        return "sdio";
    default:
        return "unknown";
    }
}

// info() -> table of chip and firmware facts. reset_reason is the power-supply
// diagnostic: "brownout" means the supply sagged on the previous run.
int info(lua_State* L) {
    char version[16];
    snprintf(version, sizeof(version), "%u.%u", Config::APP_VERSION_MAJOR,
             Config::APP_VERSION_MINOR);

    lua_newtable(L);

    // The API version an app checks against; see docs/LUA_API.md.
    LuaApi::setField(L, "api", LuaApi::kApiVersion);
    LuaApi::setField(L, "version", version);

    LuaApi::setField(L, "chip", ESP.getChipModel());
    LuaApi::setField(L, "revision",
                     static_cast<lua_Integer>(ESP.getChipRevision()));
    LuaApi::setField(L, "cores", static_cast<lua_Integer>(ESP.getChipCores()));
    LuaApi::setField(L, "cpu_mhz",
                     static_cast<lua_Integer>(ESP.getCpuFreqMHz()));

    LuaApi::setField(L, "flash_bytes",
                     static_cast<lua_Integer>(ESP.getFlashChipSize()));
    LuaApi::setField(L, "psram_bytes",
                     static_cast<lua_Integer>(ESP.getPsramSize()));
    LuaApi::setField(L, "psram_free_bytes",
                     static_cast<lua_Integer>(ESP.getFreePsram()));
    LuaApi::setField(L, "heap_bytes",
                     static_cast<lua_Integer>(ESP.getHeapSize()));
    LuaApi::setField(L, "heap_free_bytes",
                     static_cast<lua_Integer>(ESP.getFreeHeap()));
    LuaApi::setField(L, "heap_min_free_bytes",
                     static_cast<lua_Integer>(ESP.getMinFreeHeap()));

    LuaApi::setField(L, "uptime_ms", static_cast<lua_Integer>(millis()));
    LuaApi::setField(L, "reset_reason", resetReasonName());

    return 1;
}

// Registry key for the module cache. Per lua_State, and a state lives for one
// launch, so a module is loaded at most once per app run.
constexpr const char* kImportCache = "alpdeck.imports";

// import(path) -> whatever the module returns
//
// There is no `require`: the standard library is opened without `package`, on
// purpose. This is the whole of the module system -- read a file, run it, keep
// the result. A module is a file that returns a table, and it sees the same
// restricted environment as its caller, because a chunk loaded here inherits
// the globals table rather than getting one of its own.
int import(lua_State* L) {
    const char* requested = luaL_checkstring(L, 1);

    const char* reason = "";
    const String path = FsApi::resolvePath(requested, &reason);
    if (path.isEmpty()) {
        return luaL_error(L, "sys.import denied: %s", reason);
    }

    if (lua_getfield(L, LUA_REGISTRYINDEX, kImportCache) != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, kImportCache);
    }

    // Two imports of the same path return the same table, so a module can hold
    // state and callers can compare identities.
    if (lua_getfield(L, -1, path.c_str()) != LUA_TNIL) {
        return 1;
    }
    lua_pop(L, 1);

    String localPath;
    fs::FS& fs = Vfs::resolve(path, localPath);
    File file = fs.open(localPath, "r");
    if (!file || file.isDirectory()) {
        return luaL_error(L, "sys.import('%s'): not found", path.c_str());
    }

    const size_t expected = file.size();
    const String source = file.readString();
    file.close();

    if (source.length() != expected) {
        return luaL_error(L, "sys.import('%s'): short read, %d of %d bytes",
                          path.c_str(), static_cast<int>(source.length()),
                          static_cast<int>(expected));
    }

    // "=path" so a syntax error names the module file rather than a copy of
    // its source.
    const String chunkName = String("=") + path;
    if (luaL_loadbuffer(L, source.c_str(), source.length(),
                        chunkName.c_str()) != LUA_OK) {
        return luaL_error(L, "sys.import('%s'): %s", path.c_str(),
                          lua_tostring(L, -1));
    }

    lua_call(L, 0, 1);

    // Cache and return the same value.
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, path.c_str());
    return 1;
}

// wifi_scan() -> { {ssid=, rssi=, open=}, ... }, strongest first
//
// Blocks for a couple of seconds. That is fine here and nowhere else: Lua runs
// on its own task, so the main loop, FTP and input polling all keep running.
int wifiScan(lua_State* L) {
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
int wifiConfigure(lua_State* L) {
    const char* ssid = luaL_checkstring(L, 1);
    const char* password = luaL_optstring(L, 2, "");
    NetworkService::configure(ssid, password);
    return 0;
}

int wifiForget(lua_State* L) {
    (void)L;
    NetworkService::forget();
    return 0;
}

int wifiPortal(lua_State* L) {
    (void)L;
    NetworkService::startSetupPortal();
    return 0;
}

// ftp_configure(user, password) -- write-only, same reasoning.
int ftpConfigure(lua_State* L) {
    const char* user = luaL_checkstring(L, 1);
    const char* password = luaL_checkstring(L, 2);
    FtpService::configure(user, password);
    return 0;
}

// wifi() -> table {connected[, ssid, ip, rssi]}. Read-only status; managing
// the connection stays with the kernel (portal), not with apps.
int wifi(lua_State* L) {
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
    {"millis", millisSinceBoot},
    {"delay", delay},
    {"log", log},
    {"appdir", appdir},
    {"launch", launch},
    {"exit", exit},
    {"restart", restart},
    {"sd_remount", sdRemount},
    {"memory", memory},
    {"temperature", temperature},
    {"info", info},
    {"wifi", wifi},
    {"import", import},
    {"wifi_scan", wifiScan},
    {"wifi_configure", wifiConfigure},
    {"wifi_forget", wifiForget},
    {"wifi_portal", wifiPortal},
    {"ftp_configure", ftpConfigure},
    {nullptr, nullptr},
};

}  // namespace

void install(lua_State* L) { LuaApi::installTable(L, "sys", kFunctions); }

}  // namespace SysApi
