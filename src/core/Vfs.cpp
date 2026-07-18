#include "core/Vfs.h"

#include <LittleFS.h>
#include <SD.h>

#include "config/AppConfig.h"

namespace Vfs {

fs::FS& resolve(const String& path, String& localPath) {
    const String prefix = String("/") + Config::FTP_MOUNT_SD;
    if (path == prefix) {
        localPath = "/";
        return SD;
    }
    if (path.startsWith(prefix + "/")) {
        localPath = path.substring(prefix.length());
        return SD;
    }
    localPath = path;
    return LittleFS;
}

bool isOnSd(const String& path) {
    String localPath;
    return &resolve(path, localPath) == &SD;
}

}  // namespace Vfs
