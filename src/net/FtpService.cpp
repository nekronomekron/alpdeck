#include "net/FtpService.h"

#include <ESP-FTP-Server-Lib.h>
#include <LittleFS.h>
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

}  // namespace

void start(bool sdMounted) {
    if (server != nullptr) {
        return;
    }

    server = new FTPServer();
    server->addUser(Config::FTP_USER, Config::FTP_PASSWORD);
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

void loop() {
    if (server != nullptr) {
        server->handle();
    }
}

}  // namespace FtpService
