#include "net/FtpService.h"

#include <ESP-FTP-Server-Lib.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <SD.h>
#include <WiFi.h>

#include "config/AppConfig.h"
#include "core/Vfs.h"
#include "utils/Logger.h"

namespace FtpService {
namespace {

// Heap-allocated per start/stop cycle: the library has no stop(), but the
// destructor chain (~FTPServer -> ~WiFiServer -> end()) closes the listening
// socket and drops every connection, which is the only clean way to stop it.
//
// Owned by loop(), and touched from nowhere else. See the header.
FTPServer* server = nullptr;

// What the server should be, written from whichever task asked. Single words
// read once per main-loop pass, so no lock: the worst a torn read could do is
// reconcile one pass later, which is 1ms of a server nobody is talking to yet.
volatile bool wantRunning = false;
volatile bool wantRebuild = false;

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

// Both of these run on the main loop only, from reconcile().
void startServer() {
    const Login login = loadLogin();
    server = new FTPServer();
    server->addUser(login.user, login.password);
    server->addFilesystem(Config::FTP_MOUNT_FLASH, &LittleFS);

    // Asked at construction, because the library reads its filesystem list
    // once. A card mounted later arrives through requestRebuild().
    if (Vfs::sdMounted()) {
        server->addFilesystem(Config::FTP_MOUNT_SD, &SD);
    }
    server->begin();

    LOGI(kLogTag, "Server started on %s", WiFi.localIP().toString().c_str());
}

void stopServer() {
    delete server;
    server = nullptr;

    LOGI(kLogTag, "Server stopped");
}

// Brings the server in line with what was asked for. The rebuild is handled
// first so that a request arriving in the same pass as a stop does not resurrect
// a server nobody wants.
void reconcile() {
    if (wantRebuild) {
        wantRebuild = false;
        if (server != nullptr && wantRunning) {
            stopServer();
            startServer();
        }
    }

    if (wantRunning && server == nullptr) {
        startServer();
    } else if (!wantRunning && server != nullptr) {
        stopServer();
    }
}

}  // namespace

void setEnabled(bool enabled) { wantRunning = enabled; }

void requestRebuild() { wantRebuild = true; }

void configure(const String& user, const String& password) {
    Preferences prefs;
    prefs.begin(kPrefsNamespace, false);
    prefs.putString(kKeyUser, user);
    prefs.putString(kKeyPass, password);
    prefs.end();

    LOGI(kLogTag, "Login changed to '%s'", user.c_str());

    // The library reads its user list at construction, so a running server
    // keeps the old login until it is rebuilt.
    requestRebuild();
}

bool hasCustomLogin() {
    Preferences prefs;
    prefs.begin(kPrefsNamespace, true);
    const bool custom = prefs.isKey(kKeyUser);
    prefs.end();
    return custom;
}

void loop() {
    reconcile();

    if (server != nullptr) {
        server->handle();
    }
}

}  // namespace FtpService
