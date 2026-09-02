#include "core/Vfs.h"

#include <LittleFS.h>
#include <SD.h>

#include "config/AppConfig.h"
#include "utils/Logger.h"

namespace Vfs {
namespace {

bool mounted = false;

}  // namespace

bool mountSd() {
    // end() on a card that was never mounted is harmless, and skipping it on a
    // card that WAS would leave the old handle open -- which is exactly the
    // case this function exists for.
    if (mounted) {
        SD.end();
        mounted = false;
    }

    // Shares the display's SPI bus, so only CS is passed; SPI.begin() belongs
    // to Display::init() and has run long before this.
    mounted = SD.begin(Config::SD_PIN_CS, SPI);
    if (mounted) {
        LOGI(kLogTag, "SD mounted (%llu bytes)", SD.cardSize());
    } else {
        LOGW(kLogTag, "SD mount failed; /%s is not available",
             Config::FTP_MOUNT_SD);
    }
    return mounted;
}

bool sdMounted() { return mounted; }

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
