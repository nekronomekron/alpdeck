// alpdeck -- an e-paper handheld that runs Lua apps.
//
// Everything of substance lives in BootSequence: this file exists only because
// the Arduino framework wants a setup() and a loop().

#include <Arduino.h>

#include "core/BootSequence.h"

void setup() { BootSequence::run(); }

void loop() { BootSequence::loop(); }
