#pragma once

#include <Arduino.h>

#include "peripherals/Input.h"
#include "peripherals/SeesawButtons.h"

// One input controller on the I2C daisy chain.
//
// Every controller is optional and the device runs on any one of them, so the
// facade holds them as a list and asks each the same four questions rather than
// naming them. Adding a third is a class and one entry in that list: nothing in
// Input::init() or Input::poll() grows a branch for it.
//
// What a new controller does still cost, honestly: its events have to be named
// in Input::Event and eventName(), its digest behaviour classified, and any
// level-triggered state it wants to expose needs fields in Input::Snapshot.
// That is the event vocabulary, and no abstraction here can invent it.
class Controller {
public:
    virtual ~Controller() = default;

    // Probes the hardware. False when it is absent or reports the wrong seesaw
    // product; the rest of the system carries on without it, which is why this
    // must never be fatal on its own.
    virtual bool begin() = 0;

    virtual bool available() const = 0;

    // Reads the device and publishes edges through `publish`. Main loop only:
    // this is the half that touches I2C.
    virtual void poll(uint32_t nowMs, SeesawButtons::PublishFn publish) = 0;

    // Adds this controller's level-triggered state to the shared snapshot,
    // from what the last poll() sampled. No I2C, and only the fields that are
    // this controller's -- the snapshot is one struct because apps want one
    // answer, not because the controllers know about each other.
    virtual void fill(Input::Snapshot& snapshot) const = 0;
};
