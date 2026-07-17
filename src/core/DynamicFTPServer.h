#pragma once

class DynamicFTPServer {
public:
    static void init(bool sdMounted);
    static void shutdown();

    static void loop();

private:
    static bool _started;
};