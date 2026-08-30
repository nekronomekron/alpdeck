#pragma once

// FTP access to the device's storage: LittleFS as /flash and, when a card is
// mounted, SD as /sd. Started from the network's connect callback and torn
// down again on disconnect, so it only ever exists while it is reachable.
namespace FtpService {

constexpr const char* kLogTag = "FTP";

void start(bool sdMounted);
void stop();

// Services client connections. Call from loop(); no-op while stopped.
void loop();

}  // namespace FtpService
