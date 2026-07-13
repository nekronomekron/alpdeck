#pragma once

// Optional local secrets (ignored by git). See include/secrets.h.example.
#if defined(__has_include)
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#endif

namespace Config {
constexpr uint8_t LOG_LEVEL = 3;  // 0=error, 1=warn, 2=info, 3=debug
constexpr bool LOG_SERIAL_OUTPUT = true;
constexpr bool LOG_SERIAL_SENSORS_OUTPUT = false;
}  // namespace Config
