#include <Arduino.h>
#include <LittleFS.h>
#include <SD.h>

#include "config/AppConfig.h"
#include "core/Display.h"
#include "core/DynamicFTPServer.h"
#include "core/Input.h"
#include "core/Logger.h"
#include "core/LuaBindings.h"
#include "core/LuaHost.h"
#include "core/Network.h"
#include "utils/Bootscreen.h"

namespace {
bool sdMounted = false;
Bootscreen bootscreen;

// Fatal boot error: shown on the bootscreen below the logo (warning sign plus
// message), then the device halts. Nothing else runs -- a device that cannot
// reach the launcher is better stopped on a readable screen than half-alive.
[[noreturn]] void bootFail(const char* message) {
    LOGE("Boot", "Fatal: %s", message);

    Display::drawFullWindow([&](Adafruit_GFX& gfx) {
        bootscreen.init(gfx);
        bootscreen.drawError(gfx, message);
    });

    while (true) {
        yield();
    }
}
}  // namespace

// Everything runnable is a Lua script on the host's task: boot.lua first, then
// the launcher, then whatever the launcher picks. Exactly one runs at a time.
void startLauncher() {
    // The launcher only browses; an empty root denies it every fs write.
    LuaBindings::setSandboxRoot("");

    if (!LuaHost::run(Config::LAUNCHER_PATH)) {
        // Without a launcher there is nothing to operate. A fresh flash where
        // the filesystem image was never uploaded lands here.
        bootFail("launcher not found\nflash the filesystem (uploadfs)");
    }
}

void startApp(const String& path) {
    // Confine writes to the app's own directory: strip the trailing entry file.
    String root = path;
    const int slash = root.lastIndexOf('/');
    if (slash > 0) {
        root = root.substring(0, slash);
    }
    LuaBindings::setSandboxRoot(root);

    if (!LuaHost::run(path)) {
        LOGE("Boot", "%s could not start; returning to the launcher",
             path.c_str());
        startLauncher();
    }
}

// Fires on the main loop once a script's VM is fully torn down, so starting the
// next one here can never leave two states alive.
void onScriptFinished(const LuaHost::Finished& finished) {
    const String request = LuaBindings::takeLaunchRequest();

    if (!request.isEmpty()) {
        startApp(request);
        return;
    }

    // Anything else -- clean return, error, or cancellation -- lands back at
    // the launcher, which is what makes a crashing app survivable.
    if (finished.path != Config::LAUNCHER_PATH) {
        startLauncher();
        return;
    }

    // The launcher itself returned without a launch request. Restarting it
    // immediately would spin, so only do so if it failed outright.
    if (finished.exit == LuaHost::Exit::Failed ||
        finished.exit == LuaHost::Exit::NotFound) {
        LOGE("Boot", "Launcher stopped unexpectedly; not restarting");
    }
}

void setup() {
    Serial.begin(115200);

    // Give the USB-CDC host (serial monitor) a moment to attach, but never
    // block forever: with no host attached — or in the Wokwi simulator, where
    // the CDC connected-state isn't asserted — this must fall through.
    const unsigned long serialStart = millis();
    while (!Serial && millis() - serialStart < 1000) {
        yield();
    }
    delay(1000);

    Logger::begin(Serial, static_cast<Logger::Level>(Config::LOG_LEVEL));
    Logger::setSerialOutputEnabled(Config::LOG_SERIAL_OUTPUT);

    Display::init();

    Display::drawFullWindow([&](Adafruit_GFX& gfx) { bootscreen.init(gfx); });

    // The launcher lives on LittleFS; without the mount the device can never
    // reach anything runnable.
    if (!LittleFS.begin(true)) {
        bootFail("flash filesystem failed");
    }
    LOGI("FS", "LittleFS mounted (%u/%u bytes used)", LittleFS.usedBytes(),
         LittleFS.totalBytes());

    // The SD card shares the display's SPI bus; Display::init() already called
    // SPI.begin() for it, and the display is hibernated by now with CS released.
    sdMounted = SD.begin(Config::SD_PIN_CS, SPI);
    if (!sdMounted) {
        LOGW("FS", "SD mount failed; /%s will not be served",
             Config::FTP_MOUNT_SD);
    } else {
        LOGI("FS", "SD mounted (%llu bytes)", SD.cardSize());

        // Log the card's top level. Apps live at /apps here (the /sd prefix is
        // a virtual mount the bindings strip), so this is a quick check that the
        // layout is right.
        File root = SD.open("/");
        if (root && root.isDirectory()) {
            for (File e = root.openNextFile(); e; e = root.openNextFile()) {
                LOGD("FS", "  sd:/%s%s", e.name(), e.isDirectory() ? "/" : "");
                e.close();
            }
            root.close();
        }
    }

    // Input comes before the network: with no controller at all the device is
    // unusable and must stop on the error screen before anything else starts.
    // The simulator has no seesaw hardware, so it boots on without input.
    if (!Input::init()) {
#ifndef WOKWI_SIMULATOR
        bootFail("no input controller found\nconnect a rotary or gamepad");
#else
        LOGW("Boot", "No input controller (simulator build); continuing");
#endif
    }

    // FTP only exists once there's a network to serve it on, so it is started
    // from the connect callback rather than here — that covers both a boot-time
    // auto-connect and credentials arriving later via the setup portal.
    Network::onConnected([]() { DynamicFTPServer::init(sdMounted); });
    Network::onDisconnected(DynamicFTPServer::shutdown);
    Network::init();

    if (!LuaHost::init()) {
        bootFail("system error: lua host failed");
    }
    LuaHost::onFinished(onScriptFinished);

    // Deliberately no Display::shutdown() here any more. It blanked the panel
    // and hibernated it immediately before the first script ran, so anything
    // drawing afterwards started from a cleared screen. The bootscreen now
    // stays up until the launcher's first frame replaces it.

    // boot.lua is the user hook, so it runs with the same privileges as an app
    // but rooted at LittleFS. If it requests a launch, onScriptFinished honours
    // it and the launcher is skipped.
    LuaBindings::setSandboxRoot("");
    if (!LuaHost::run(Config::BOOT_SCRIPT_PATH)) {
        LOGW("Boot", "%s missing; starting the launcher directly",
             Config::BOOT_SCRIPT_PATH);
        startLauncher();
    }
}

void loop() {
    Network::loop();
    DynamicFTPServer::loop();
    Input::poll();
    LuaHost::loop();
}