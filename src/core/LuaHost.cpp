#include "core/LuaHost.h"

#include <LuaWrapper.h>

#include "config/AppConfig.h"
#include "core/LuaBindings.h"
#include "core/Logger.h"
#include "core/Vfs.h"

namespace {
TaskHandle_t taskHandle = nullptr;
QueueHandle_t results = nullptr;
SemaphoreHandle_t lock = nullptr;

// Only ever touched under `lock`: the Lua task publishes it while running so
// the main loop can ask it to stop, and clears it before the wrapper is
// destroyed. Without the lock, requestStop() could reach a freed wrapper.
LuaWrapper* activeWrapper = nullptr;
String activePath;
}  // namespace

std::function<void(const LuaHost::Finished&)> LuaHost::_onFinished;

bool LuaHost::init() {
    lock = xSemaphoreCreateMutex();
    results = xQueueCreate(4, sizeof(Finished*));

    if (lock == nullptr || results == nullptr) {
        LOGE(kLogTag, "Could not allocate host primitives");
        return false;
    }
    return true;
}

bool LuaHost::readScript(const String& path, String& source) {
    String localPath;
    fs::FS& fs = Vfs::resolve(path, localPath);

    File file = fs.open(localPath, "r");
    if (!file || file.isDirectory()) {
        return false;
    }

    const size_t expected = file.size();
    source = file.readString();
    file.close();

    // A short read (I/O error, out of memory) would otherwise surface later as
    // a baffling syntax error in a truncated chunk; fail it here instead.
    if (source.length() != expected) {
        LOGE(kLogTag, "Short read for %s (%u of %u bytes)", path.c_str(),
             source.length(), expected);
        return false;
    }
    return true;
}

bool LuaHost::run(const String& path) {
    if (results == nullptr || lock == nullptr) {
        LOGE(kLogTag, "Host not initialized; cannot run %s", path.c_str());
        return false;
    }

    if (isRunning()) {
        LOGW(kLogTag, "'%s' is already running; refusing to start '%s'",
             activePath.c_str(), path.c_str());
        return false;
    }

    String localPath;
    fs::FS& fs = Vfs::resolve(path, localPath);
    if (!fs.exists(localPath)) {
        LOGE(kLogTag, "%s not found", path.c_str());
        return false;
    }

    // The task takes ownership and frees this.
    String* param = new String(path);
    activePath = path;

    const BaseType_t created = xTaskCreatePinnedToCore(
        task, "lua", Config::LUA_TASK_STACK_BYTES, param,
        Config::LUA_TASK_PRIORITY, &taskHandle, Config::LUA_TASK_CORE);

    if (created != pdPASS) {
        LOGE(kLogTag, "Could not create the Lua task");
        delete param;
        taskHandle = nullptr;
        activePath = "";
        return false;
    }

    LOGI(kLogTag, "Running %s", path.c_str());
    return true;
}

void LuaHost::task(void* param) {
    String* pathParam = static_cast<String*>(param);
    const String path = *pathParam;
    delete pathParam;

    Finished* done = new Finished{Exit::Returned, path, ""};

    String source;
    if (!readScript(path, source)) {
        done->exit = Exit::NotFound;
        done->message = "could not read " + path;
    } else {
        // Scoped so the wrapper -- and with it lua_close and every byte the app
        // allocated -- is torn down before the completion is reported.
        LuaWrapper wrapper;
        LuaBindings::install(wrapper);

        xSemaphoreTake(lock, portMAX_DELAY);
        activeWrapper = &wrapper;
        xSemaphoreGive(lock);

        String message;
        const LuaWrapper::Result result = wrapper.run(source, path, message);

        xSemaphoreTake(lock, portMAX_DELAY);
        activeWrapper = nullptr;
        xSemaphoreGive(lock);

        switch (result) {
        case LuaWrapper::Result::Ok:
            done->exit = Exit::Returned;
            break;
        case LuaWrapper::Result::Cancelled:
            done->exit = Exit::Cancelled;
            break;
        default:
            done->exit = Exit::Failed;
            done->message = message;
            break;
        }
    }

    xQueueSend(results, &done, portMAX_DELAY);

    // taskHandle stays set until the main loop drains the queue, so isRunning()
    // errs towards "busy" and can never spawn a second VM alongside this one.
    vTaskDelete(nullptr);
}

void LuaHost::loop() {
    if (results == nullptr) {
        return;
    }

    Finished* done = nullptr;
    if (xQueueReceive(results, &done, 0) != pdPASS) {
        return;
    }

    taskHandle = nullptr;
    activePath = "";

    switch (done->exit) {
    case Exit::Returned:
        LOGI(kLogTag, "%s finished", done->path.c_str());
        break;
    case Exit::Cancelled:
        LOGW(kLogTag, "%s cancelled", done->path.c_str());
        break;
    case Exit::NotFound:
        LOGE(kLogTag, "%s", done->message.c_str());
        break;
    case Exit::Failed:
        LOGE(kLogTag, "%s failed:\n%s", done->path.c_str(),
             done->message.c_str());
        break;
    }

    if (_onFinished) {
        _onFinished(*done);
    }
    delete done;
}

bool LuaHost::isRunning() { return taskHandle != nullptr; }

String LuaHost::currentPath() { return activePath; }

void LuaHost::requestStop() {
    if (lock == nullptr) {
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    if (activeWrapper != nullptr) {
        activeWrapper->stop();
        LOGI(kLogTag, "Stop requested for %s", activePath.c_str());
    }
    xSemaphoreGive(lock);
}

void LuaHost::onFinished(std::function<void(const Finished&)> callback) {
    _onFinished = std::move(callback);
}
