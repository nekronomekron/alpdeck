#include "core/DynamicFTPServer.h"

#include <ESP-FTP-Server-Lib.h>
#include <LittleFS.h>
#include <SD.h>
#include <WiFi.h>

#include "config/AppConfig.h"
#include "core/Logger.h"

namespace {
// Heap-allocated per init/shutdown cycle: the library has no stop(), but the
// destructor chain (~FTPServer -> ~WiFiServer -> end()) closes the listening
// socket and drops every connection, which is the only clean way to stop it.
FTPServer* ftpServer = nullptr;
}  // namespace

void DynamicFTPServer::init(bool sdMounted) {
    if (ftpServer != nullptr) {
        return;
    }

    ftpServer = new FTPServer();
    ftpServer->addUser(Config::FTP_USER, Config::FTP_PASSWORD);
    ftpServer->addFilesystem(Config::FTP_MOUNT_FLASH, &LittleFS);
    if (sdMounted) {
        ftpServer->addFilesystem(Config::FTP_MOUNT_SD, &SD);
    }
    ftpServer->begin();

    LOGI(kLogTag, "Server started on %s", WiFi.localIP().toString().c_str());
}

void DynamicFTPServer::shutdown() {
    if (ftpServer == nullptr) {
        return;
    }

    delete ftpServer;
    ftpServer = nullptr;

    LOGI(kLogTag, "Server stopped");
}

void DynamicFTPServer::loop() {
    if (ftpServer != nullptr) {
        ftpServer->handle();
    }
}
