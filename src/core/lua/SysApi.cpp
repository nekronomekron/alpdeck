#include "core/lua/SysApi.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include "config/AppConfig.h"
#include "core/lua/LuaApi.h"
#include "core/lua/LuaContext.h"
#include "net/Network.h"
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

int exit(lua_State* L) {
    LuaWrapper* self = *static_cast<LuaWrapper**>(lua_getextraspace(L));
    if (self != nullptr) {
        self->stop();  // the VM hook unwinds within a few hundred instructions
    }
    return 0;
}

int memory(lua_State* L) {
    LuaWrapper* self = *static_cast<LuaWrapper**>(lua_getextraspace(L));
    lua_pushinteger(L, self != nullptr
                           ? static_cast<lua_Integer>(self->memoryUsed())
                           : 0);
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
    LuaApi::setField(L, "cpu_mhz", static_cast<lua_Integer>(ESP.getCpuFreqMHz()));

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

// wifi() -> table {connected[, ssid, ip, rssi]}. Read-only status; managing
// the connection stays with the kernel (portal), not with apps.
int wifi(lua_State* L) {
    const bool connected = Network::isConnected();

    lua_newtable(L);
    LuaApi::setField(L, "connected", connected);

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
    {"memory", memory},
    {"temperature", temperature},
    {"info", info},
    {"wifi", wifi},
    {nullptr, nullptr},
};

}  // namespace

void install(lua_State* L) { LuaApi::installTable(L, "sys", kFunctions); }

}  // namespace SysApi
