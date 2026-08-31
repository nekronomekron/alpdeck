#include "core/Settings.h"

#include <Preferences.h>
#include <string.h>

#include "utils/Logger.h"

namespace Settings {
namespace {

// Separate from the WiFi credential namespace on purpose: nothing that reads
// settings should be able to reach a password by accident.
constexpr const char* kNamespace = "alpdeck-cfg";

Preferences prefs;
bool ready = false;
std::function<void(const char*)> changeCallback;

const Key kKeys[] = {
    // WiFi off means the radio never comes up and FTP has nothing to run on.
    {kWifiEnabled, Type::Bool, 1, 0, 1},
    {kFtpEnabled, Type::Bool, 1, 0, 1},
    // Off by default: blanking the panel is the quieter behaviour, and a
    // standby screen is a preference rather than an expectation.
    {kStandbyScreen, Type::Bool, 0, 0, 1},
    // Minutes of no input before sleeping. 0 disables, which is the default --
    // a device that switches itself off unasked is a support call.
    {kSleepAfterMin, Type::Int, 0, 0, 240},
    // Every Nth launcher frame is a full refresh, to clear ghosting.
    {kRefreshEvery, Type::Int, 8, 1, 64},
};

int32_t read(const Key& key) {
    if (!ready) {
        return key.fallback;
    }
    return prefs.getInt(key.name, key.fallback);
}

bool write(const Key& key, int32_t value) {
    if (!ready) {
        return false;
    }
    if (value < key.minimum || value > key.maximum) {
        LOGW(kLogTag, "%s = %d is outside %d..%d", key.name,
             static_cast<int>(value), static_cast<int>(key.minimum),
             static_cast<int>(key.maximum));
        return false;
    }
    prefs.putInt(key.name, value);
    return true;
}

}  // namespace

void begin() {
    ready = prefs.begin(kNamespace, false);
    if (!ready) {
        LOGE(kLogTag, "Could not open NVS; every setting falls back to its default");
        return;
    }
    LOGI(kLogTag, "wifi=%d ftp=%d standby=%d sleep=%dmin refresh=%d",
         getBool(kWifiEnabled), getBool(kFtpEnabled), getBool(kStandbyScreen),
         static_cast<int>(getInt(kSleepAfterMin)),
         static_cast<int>(getInt(kRefreshEvery)));
}

const Key* find(const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    for (const Key& key : kKeys) {
        if (strcmp(key.name, name) == 0) {
            return &key;
        }
    }
    return nullptr;
}

const Key* all(size_t& count) {
    count = sizeof(kKeys) / sizeof(kKeys[0]);
    return kKeys;
}

bool getBool(const char* name) {
    const Key* key = find(name);
    return key != nullptr && read(*key) != 0;
}

int32_t getInt(const char* name) {
    const Key* key = find(name);
    return key != nullptr ? read(*key) : 0;
}

bool setBool(const char* name, bool value) {
    const Key* key = find(name);
    if (key == nullptr || !write(*key, value ? 1 : 0)) {
        return false;
    }
    if (changeCallback) {
        changeCallback(key->name);
    }
    return true;
}

bool setInt(const char* name, int32_t value) {
    const Key* key = find(name);
    if (key == nullptr || !write(*key, value)) {
        return false;
    }
    if (changeCallback) {
        changeCallback(key->name);
    }
    return true;
}

void onChanged(std::function<void(const char*)> callback) {
    changeCallback = std::move(callback);
}

}  // namespace Settings
