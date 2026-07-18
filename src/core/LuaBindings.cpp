#include "core/LuaBindings.h"

#include <LuaWrapper.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include "config/AppConfig.h"
#include "core/Display.h"
#include "core/Input.h"
#include "core/Logger.h"
#include "core/Network.h"
#include "core/Vfs.h"

namespace {
String sandboxRoot;
String launchRequest;

// ---------------------------------------------------------------- path safety

// Rejects anything that could climb out of the sandbox. Checked before any
// prefix comparison, because "/sd/apps/x/../../boot.lua" would otherwise pass a
// naive startsWith test.
bool isTraversal(const String& path) {
    if (path.indexOf("..") >= 0) {
        return true;
    }
    if (!path.startsWith("/")) {
        return true;  // relative paths have no well-defined root here
    }
    return false;
}

bool readAllowed(const String& path) { return !isTraversal(path); }

// Writes stay inside the running app's own directory. The launcher runs with an
// empty root and so may write nowhere -- it only ever reads.
bool writeAllowed(const String& path) {
    if (isTraversal(path)) {
        return false;
    }
    if (sandboxRoot.isEmpty()) {
        return false;
    }
    return path.startsWith(sandboxRoot + "/") || path == sandboxRoot;
}

// ------------------------------------------------------------------- display

// Draw calls open a frame implicitly, so a script may just start drawing.
Adafruit_GFX& canvas() {
    if (!Display::frameOpen()) {
        Display::beginFrame();
    }
    return Display::canvas();
}

// clear([full]) -- opens the frame and picks its refresh mode.
//
// The mode is fixed when the frame opens (setFullWindow vs setPartialWindow),
// so it cannot be chosen at show() time. Partial is the default: ~400ms against
// ~1200ms. Pass true periodically to clear the ghosting partials leave behind.
int l_display_clear(lua_State* L) {
    const bool full = lua_toboolean(L, 1);
    if (!Display::frameOpen()) {
        Display::beginFrame(!full);
    }
    // beginFrame's firstPage() already whitens the buffer; this keeps clear()
    // meaningful when a frame is somehow already open.
    Display::canvas().fillScreen(Display::kWhite);
    return 0;
}

// text(x, y, s [, size [, invert]]) -- invert draws white, for text sitting on
// a filled rect. Without it, drawing on a filled row is black on black.
int l_display_text(lua_State* L) {
    const int16_t x = luaL_checkinteger(L, 1);
    const int16_t y = luaL_checkinteger(L, 2);
    const char* text = luaL_checkstring(L, 3);
    // Clamped: size 0 renders nothing and a negative value would wrap huge.
    const lua_Integer rawSize = luaL_optinteger(L, 4, 1);
    const uint8_t size = rawSize < 1 ? 1 : (rawSize > 8 ? 8 : rawSize);
    const bool invert = lua_toboolean(L, 5);

    Adafruit_GFX& gfx = canvas();
    gfx.setTextSize(size);
    gfx.setTextColor(invert ? Display::kWhite : Display::kBlack);
    gfx.setCursor(x, y);
    gfx.print(text);
    return 0;
}

int l_display_rect(lua_State* L) {
    const int16_t x = luaL_checkinteger(L, 1);
    const int16_t y = luaL_checkinteger(L, 2);
    const int16_t w = luaL_checkinteger(L, 3);
    const int16_t h = luaL_checkinteger(L, 4);
    const bool fill = lua_toboolean(L, 5);

    if (fill) {
        canvas().fillRect(x, y, w, h, Display::kBlack);
    } else {
        canvas().drawRect(x, y, w, h, Display::kBlack);
    }
    return 0;
}

int l_display_line(lua_State* L) {
    canvas().drawLine(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2),
                      luaL_checkinteger(L, 3), luaL_checkinteger(L, 4),
                      Display::kBlack);
    return 0;
}

int l_display_pixel(lua_State* L) {
    canvas().drawPixel(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2),
                       Display::kBlack);
    return 0;
}

int l_display_size(lua_State* L) {
    lua_pushinteger(L, Display::width());
    lua_pushinteger(L, Display::height());
    return 2;
}

// show() -- pushes the frame to the panel. Takes no mode: that was decided by
// clear(). Opening a second frame here would only render a blank one, because
// GxEPD2's firstPage() whitens the buffer.
int l_display_show(lua_State* L) {
    (void)L;
    Display::endFrame();  // no-op when nothing was drawn
    return 0;
}

// --------------------------------------------------------------------- input

int l_input_read(lua_State* L) {
    // A negative timeout would wrap to ~49 days once cast; treat it as "poll".
    const lua_Integer rawTimeout = luaL_optinteger(L, 1, 0);
    const uint32_t timeoutMs = rawTimeout < 0 ? 0 : rawTimeout;

    // Blocks this task, not the main loop -- which is the whole point of
    // running apps off-task. It also lets the idle task run.
    const Input::Event event = Input::read(timeoutMs);
    if (event == Input::Event::None) {
        lua_pushnil(L);
    } else {
        lua_pushstring(L, Input::eventName(event));
    }
    return 1;
}

// ------------------------------------------------------------------------ fs

int l_fs_list(lua_State* L) {
    const String path = luaL_checkstring(L, 1);
    if (!readAllowed(path)) {
        return luaL_error(L, "fs.list denied for '%s'", path.c_str());
    }

    String localPath;
    fs::FS& fs = Vfs::resolve(path, localPath);
    const bool onSd = &fs == &SD;

    File dir = fs.open(localPath);
    if (!dir || !dir.isDirectory()) {
        LOGW(LuaBindings::kLogTag, "fs.list('%s') -> %s:'%s' not a directory (open=%d)",
             path.c_str(), onSd ? "SD" : "LittleFS", localPath.c_str(),
             static_cast<bool>(dir));
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    // Absolute index of the result table, so storing into it never depends on
    // the fluctuating relative top during entry construction.
    const int resultIndex = lua_gettop(L);
    lua_Integer index = 1;

    for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
        // entry.name() is a bare name on some cores and a full path on others;
        // normalise to the last segment so apps see one shape.
        String name = entry.name();
        const int slash = name.lastIndexOf('/');
        if (slash >= 0) {
            name = name.substring(slash + 1);
        }
        const bool isDir = entry.isDirectory();
        const size_t size = entry.size();
        entry.close();

        lua_newtable(L);
        lua_pushstring(L, name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushboolean(L, isDir);
        lua_setfield(L, -2, "dir");
        lua_pushinteger(L, static_cast<lua_Integer>(size));
        lua_setfield(L, -2, "size");
        lua_seti(L, resultIndex, index++);
    }
    dir.close();

    LOGD(LuaBindings::kLogTag, "fs.list('%s') -> %s:'%s' : %d entries",
         path.c_str(), onSd ? "SD" : "LittleFS", localPath.c_str(),
         static_cast<int>(index - 1));
    return 1;
}

int l_fs_read(lua_State* L) {
    const String path = luaL_checkstring(L, 1);
    if (!readAllowed(path)) {
        return luaL_error(L, "fs.read denied for '%s'", path.c_str());
    }

    String localPath;
    fs::FS& fs = Vfs::resolve(path, localPath);

    File file = fs.open(localPath, "r");
    if (!file || file.isDirectory()) {
        lua_pushnil(L);
        return 1;
    }

    const String content = file.readString();
    file.close();

    lua_pushlstring(L, content.c_str(), content.length());
    return 1;
}

int l_fs_exists(lua_State* L) {
    const String path = luaL_checkstring(L, 1);
    if (!readAllowed(path)) {
        lua_pushboolean(L, false);
        return 1;
    }

    String localPath;
    fs::FS& fs = Vfs::resolve(path, localPath);
    const bool exists = fs.exists(localPath);
    LOGD(LuaBindings::kLogTag, "fs.exists('%s') -> %s:'%s' = %d", path.c_str(),
         &fs == &SD ? "SD" : "LittleFS", localPath.c_str(), exists);
    lua_pushboolean(L, exists);
    return 1;
}

int l_fs_write(lua_State* L) {
    const String path = luaL_checkstring(L, 1);
    size_t length = 0;
    const char* data = luaL_checklstring(L, 2, &length);

    if (!writeAllowed(path)) {
        return luaL_error(L, "fs.write denied for '%s' (outside '%s')",
                          path.c_str(), sandboxRoot.c_str());
    }

    String localPath;
    fs::FS& fs = Vfs::resolve(path, localPath);

    File file = fs.open(localPath, "w");
    if (!file) {
        lua_pushboolean(L, false);
        return 1;
    }

    const size_t written = file.write(reinterpret_cast<const uint8_t*>(data),
                                      length);
    file.close();

    lua_pushboolean(L, written == length);
    return 1;
}

// ----------------------------------------------------------------------- sys

int l_sys_millis(lua_State* L) {
    lua_pushinteger(L, millis());
    return 1;
}

int l_sys_delay(lua_State* L) {
    // Same wrap hazard as input.read: a negative delay must not become ~49
    // days of sleep on a task the host can only stop cooperatively.
    const lua_Integer rawMs = luaL_checkinteger(L, 1);
    const uint32_t ms = rawMs < 0 ? 0 : rawMs;
    vTaskDelay(pdMS_TO_TICKS(ms));  // yields the task rather than spinning
    return 0;
}

int l_sys_log(lua_State* L) {
    LOGI("LuaApp", "%s", luaL_checkstring(L, 1));
    return 0;
}

int l_sys_launch(lua_State* L) {
    launchRequest = luaL_checkstring(L, 1);
    // Only records the request. The script is expected to return afterwards;
    // the host reads this once the VM has been torn down, which is what keeps
    // exactly one lua_State alive at a time.
    return 0;
}

int l_sys_exit(lua_State* L) {
    LuaWrapper* self = *static_cast<LuaWrapper**>(lua_getextraspace(L));
    if (self != nullptr) {
        self->stop();  // the VM hook unwinds within a few hundred instructions
    }
    return 0;
}

int l_sys_memory(lua_State* L) {
    LuaWrapper* self = *static_cast<LuaWrapper**>(lua_getextraspace(L));
    lua_pushinteger(L, self != nullptr ? self->memoryUsed() : 0);
    lua_pushinteger(L, heap_caps_get_free_size(MALLOC_CAP_8BIT));
    return 2;
}

// temperature() -> degrees Celsius from the ESP32-S3's internal sensor. Reads
// the die, not the room: expect several degrees above ambient under load.
int l_sys_temperature(lua_State* L) {
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
int l_sys_info(lua_State* L) {
    lua_newtable(L);

    lua_pushstring(L, ESP.getChipModel());
    lua_setfield(L, -2, "chip");
    lua_pushinteger(L, ESP.getChipRevision());
    lua_setfield(L, -2, "revision");
    lua_pushinteger(L, ESP.getChipCores());
    lua_setfield(L, -2, "cores");
    lua_pushinteger(L, ESP.getCpuFreqMHz());
    lua_setfield(L, -2, "cpu_mhz");

    lua_pushinteger(L, (lua_Integer)ESP.getFlashChipSize());
    lua_setfield(L, -2, "flash_bytes");
    lua_pushinteger(L, (lua_Integer)ESP.getPsramSize());
    lua_setfield(L, -2, "psram_bytes");
    lua_pushinteger(L, (lua_Integer)ESP.getFreePsram());
    lua_setfield(L, -2, "psram_free_bytes");
    lua_pushinteger(L, (lua_Integer)ESP.getHeapSize());
    lua_setfield(L, -2, "heap_bytes");
    lua_pushinteger(L, (lua_Integer)ESP.getFreeHeap());
    lua_setfield(L, -2, "heap_free_bytes");
    lua_pushinteger(L, (lua_Integer)ESP.getMinFreeHeap());
    lua_setfield(L, -2, "heap_min_free_bytes");

    lua_pushinteger(L, (lua_Integer)millis());
    lua_setfield(L, -2, "uptime_ms");
    lua_pushstring(L, resetReasonName());
    lua_setfield(L, -2, "reset_reason");

    char version[16];
    snprintf(version, sizeof(version), "%u.%u", Config::APP_VERSION_MAJOR,
             Config::APP_VERSION_MINOR);
    lua_pushstring(L, version);
    lua_setfield(L, -2, "version");

    return 1;
}

// wifi() -> table {connected[, ssid, ip, rssi]}. Read-only status; managing
// the connection stays with the kernel (portal), not with apps.
int l_sys_wifi(lua_State* L) {
    const bool connected = Network::isConnected();

    lua_newtable(L);
    lua_pushboolean(L, connected);
    lua_setfield(L, -2, "connected");

    if (connected) {
        lua_pushstring(L, WiFi.SSID().c_str());
        lua_setfield(L, -2, "ssid");
        lua_pushstring(L, WiFi.localIP().toString().c_str());
        lua_setfield(L, -2, "ip");
        lua_pushinteger(L, WiFi.RSSI());
        lua_setfield(L, -2, "rssi");
    }
    return 1;
}

// ------------------------------------------------------------- registration

void installTable(lua_State* L, const char* name, const luaL_Reg* functions) {
    lua_newtable(L);
    luaL_setfuncs(L, functions, 0);
    lua_setglobal(L, name);
}

const luaL_Reg kDisplay[] = {
    {"clear", l_display_clear}, {"text", l_display_text},
    {"rect", l_display_rect},   {"line", l_display_line},
    {"pixel", l_display_pixel}, {"size", l_display_size},
    {"show", l_display_show},   {nullptr, nullptr},
};

const luaL_Reg kInput[] = {
    {"read", l_input_read},
    {nullptr, nullptr},
};

const luaL_Reg kFs[] = {
    {"list", l_fs_list},     {"read", l_fs_read},
    {"exists", l_fs_exists}, {"write", l_fs_write},
    {nullptr, nullptr},
};

const luaL_Reg kSys[] = {
    {"millis", l_sys_millis}, {"delay", l_sys_delay},
    {"log", l_sys_log},       {"launch", l_sys_launch},
    {"exit", l_sys_exit},     {"memory", l_sys_memory},
    {"temperature", l_sys_temperature},
    {"info", l_sys_info},     {"wifi", l_sys_wifi},
    {nullptr, nullptr},
};
}  // namespace

void LuaBindings::install(LuaWrapper& wrapper) {
    lua_State* L = wrapper.state();
    if (L == nullptr) {
        return;
    }

    launchRequest = "";

    installTable(L, "display", kDisplay);
    installTable(L, "input", kInput);
    installTable(L, "fs", kFs);
    installTable(L, "sys", kSys);
}

void LuaBindings::setSandboxRoot(const String& root) { sandboxRoot = root; }

String LuaBindings::takeLaunchRequest() {
    const String request = launchRequest;
    launchRequest = "";
    return request;
}

bool LuaBindings::hasLaunchRequest() { return !launchRequest.isEmpty(); }
