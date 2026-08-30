# Native unit tests (not runnable yet)

Reserved for `pio test -e native`: unit tests over the parts of the firmware
that carry no Arduino dependency — path resolution and the write sandbox, the
joystick digitisation and its hysteresis, event naming.

**These do not run on this machine yet.** PlatformIO's `native` platform builds
with the *system* compiler, and only the ESP32 cross-toolchains are installed
here — there is no g++, clang or MSVC. The tests are written against extracted,
Arduino-free logic so they compile the day a host toolchain exists; installing
MSYS2/MinGW-w64 is all that is missing.

Until then the automated gate is `tools/verify/` plus `pio run` over both
environments.
