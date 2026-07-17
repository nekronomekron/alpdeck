#include "DynamicFTPServer.h"

#include <ESP-FTP-Server-Lib.h>
#include <LittleFS.h>
#include <SD.h>
#include <WiFi.h>

#include "config/AppConfig.h"
#include "core/Logger.h"

FTPServer ftpSrv;

bool DynamicFTPServer::_started = false;

void DynamicFTPServer::init(bool sdMounted) {
    if (_started) {
        return;
    }

    ftpSrv.addUser(Config::FTP_USER, Config::FTP_PASSWORD);
    ftpSrv.addFilesystem(Config::FTP_MOUNT_FLASH, &LittleFS);
    if (sdMounted) {
        ftpSrv.addFilesystem(Config::FTP_MOUNT_SD, &SD);
    }
    ftpSrv.begin();
    _started = true;

    LOGI("FTP", "Server started on %s", WiFi.localIP().toString().c_str());
}

void DynamicFTPServer::shutdown() {
    if (!_started) {
        return;
    }

    delete &ftpSrv;
    _started = false;
}

void DynamicFTPServer::loop() {
    if (_started) {
        ftpSrv.handle();
    }
}