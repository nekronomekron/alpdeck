#include "net/FtpService.h"

#include <ESP-FTP-Server-Lib.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <SD.h>
#include <WiFi.h>

#include "config/AppConfig.h"
#include "utils/Logger.h"

namespace FtpService {
namespace {

// Heap-allocated per start/stop cycle: the library has no stop(), but the
// destructor chain (~FTPServer -> ~WiFiServer -> end()) closes the listening
// socket and drops every connection, which is the only clean way to stop it.
FTPServer* server = nullptr;
bool sdAvailable = false;

// Its own namespace, alongside the WiFi credentials rather than in the general
// settings store: a password that any app could read back is not a password.
constexpr const char* kPrefsNamespace = "alpdeck-ftp";
constexpr const char* kKeyUser = "user";
constexpr const char* kKeyPass = "pass";

struct Login {
    String user;
    String password;
};

Login loadLogin() {
    Preferences prefs;
    Login login;
    prefs.begin(kPrefsNamespace, true);
    login.user = prefs.getString(kKeyUser, Config::FTP_USER);
    login.password = prefs.getString(kKeyPass, Config::FTP_PASSWORD);
    prefs.end();
    return login;
}

}  // namespace

void start(bool sdMounted) {
    if (server != nullptr) {
        return;
    }

    sdAvailable = sdMounted;

    const Login login = loadLogin();
    server = new FTPServer();
    server->addUser(login.user, login.password);
    server->addFilesystem(Config::FTP_MOUNT_FLASH, &LittleFS);
    if (sdMounted) {
        server->addFilesystem(Config::FTP_MOUNT_SD, &SD);
    }
    server->begin();

    LOGI(kLogTag, "Server started on %s", WiFi.localIP().toString().c_str());
}

void stop() {
    if (server == nullptr) {
        return;
    }

    delete server;
    server = nullptr;

    LOGI(kLogTag, "Server stopped");
}

void configure(const String& user, const String& password) {
    Preferences prefs;
    prefs.begin(kPrefsNamespace, false);
    prefs.putString(kKeyUser, user);
    prefs.putString(kKeyPass, password);
    prefs.end();

    LOGI(kLogTag, "Login changed to '%s'", user.c_str());

    // The library reads its user list at construction, so a running server
    // keeps the old login until it is rebuilt.
    if (server != nullptr) {
        stop();
        start(sdAvailable);
    }
}

bool hasCustomLogin() {
    Preferences prefs;
    prefs.begin(kPrefsNamespace, true);
    const bool custom = prefs.isKey(kKeyUser);
    prefs.end();
    return custom;
}

void loop() {
    if (server != nullptr) {
        server->handle();
    }
}

}  // namespace FtpService
