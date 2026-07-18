#pragma once

// FTP access to the device's storage: LittleFS as /flash and, when a card is
// mounted, SD as /sd. Started from the network's connect callback and torn
// down again on disconnect, so it only ever exists while it is reachable.
class DynamicFTPServer {
public:
    static constexpr const char* kLogTag = "FTP";

    static void init(bool sdMounted);
    static void shutdown();

    // Services client connections. Call from loop(); no-op while stopped.
    static void loop();
};
