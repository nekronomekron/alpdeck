#pragma once

#include <Arduino.h>

#include <functional>

// Device settings, in NVS.
//
// The kernel needs several of these before any Lua runs -- whether to bring the
// radio up at all, for one -- so they cannot live in a Lua file that only an
// interpreter could read. NVS is readable from the first line of setup().
//
// Every key is declared here with its type and default, and that declaration
// is the single source of truth: Lua asks for a value without supplying a
// default of its own, so the two can never disagree about what "unset" means.
// Labels and ordering are the launcher's business, not this file's.
//
// Secrets are deliberately absent. WiFi and FTP credentials are set through
// their own write-only calls and stored where nothing can read them back; a
// generic get() over the same store would hand any app the WiFi key.
namespace Settings {

constexpr const char* kLogTag = "Settings";

enum class Type : uint8_t { Bool, Int };

struct Key {
    const char* name;
    Type type;
    int32_t fallback;
    int32_t minimum;  // Int only
    int32_t maximum;  // Int only
};

// NVS caps a key at 15 characters, so these names are the storage names too --
// one name for one thing, rather than a mapping to get wrong.
constexpr const char* kWifiEnabled = "wifi_enabled";
constexpr const char* kFtpEnabled = "ftp_enabled";
constexpr const char* kStandbyScreen = "standby_screen";
constexpr const char* kSleepAfterMin = "sleep_after_min";
constexpr const char* kRefreshEvery = "refresh_every";

// Opens the store. Call before anything reads a setting.
void begin();

// Declared keys, for the Lua binding to validate against and for anything that
// wants to enumerate them.
const Key* find(const char* name);
const Key* all(size_t& count);

bool getBool(const char* name);
int32_t getInt(const char* name);

// False when the key is unknown or the value is out of its declared range.
bool setBool(const char* name, bool value);
bool setInt(const char* name, int32_t value);

// Fires after a successful set, with the key that changed. This is what keeps
// the settings declarative: Lua states an intent by writing a value, and the
// kernel decides what that means for the hardware.
void onChanged(std::function<void(const char*)> callback);

}  // namespace Settings
