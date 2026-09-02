#pragma once

#include <Arduino.h>

#include "peripherals/Input.h"

// The coalescing half of Input: everything that happened since the last take(),
// collapsed into one answer.
//
// Its own translation unit because it is its own problem. Input is a facade
// over two I2C drivers; this is a concurrency question -- what a screen that
// cannot ask for 609ms at a time should be told when it finally does. The two
// change for different reasons and share nothing but the event names.
//
// Threading: the accumulating half runs on the main loop inside Input::poll(),
// the consuming half on whatever task an app runs on. A spinlock covers the
// state and a binary semaphore lets a consumer block without polling.
namespace InputDigest {

// Allocates the wake semaphore. False means take() can still be called, it
// just cannot block -- which is worth knowing rather than crashing on.
bool init();

// Sorts one event into the digest. Navigation adds up; a discrete action is
// held as a queue of one and closes the digest behind it. Wheel events are
// ignored on purpose: the dial is folded in from its absolute position by
// addWheel(), so counting its edges here would double every detent.
void classify(Input::Event event);

// Folds in encoder travel measured as a difference of absolute positions.
// Saturates rather than wraps -- nothing on a 400x300 panel means anything past
// a few dozen steps, and a counter that wrapped would send the cursor the
// opposite way, the one failure a user could never explain.
void addWheel(int32_t detents);

// Takes the digest and clears it, without blocking. Whatever arrived behind a
// pending action becomes the next digest rather than being merged into this
// one, which is what makes "turn, then press" and "press, then turn" two
// different things.
Input::Digest consume();

// Blocks the calling task until something lands in the digest, or the timeout
// elapses. False when there is no semaphore to wait on, so a caller loops
// rather than spins.
bool wait(uint32_t timeoutMs);

// Drops everything held, including a wake that was already signalled.
void reset();

}  // namespace InputDigest
