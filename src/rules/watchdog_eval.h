#pragma once
// Watchdog decision logic.
//
// Kept free of Win32, of the store and of the clock so it can be unit-tested:
// given a metric value and a timestamp, decide whether the configured response
// should fire and remember just enough to honour the sustain window and the
// repeat policy. The engine owns the per-process bookkeeping and the actions
// themselves.

#include <cstdint>

#include "rules/rules.h"

namespace rules {

// Per (process, watchdog) memory. Dropped when the process exits, so every
// process starts with a clean slate.
struct WatchdogState {
  bool armed = false;              // inside the current sustain window
  bool above_before = false;       // previous sample was above the threshold
  uint64_t armed_since_ms = 0;     // when the window opened
  uint64_t last_fired_ms = 0;      // 0 = never fired for this process
};

// True when the response should fire for this sample. Updates |state|.
//
// Semantics of Frequency:
//  - Immediately      - the sustain window is skipped, and the response fires
//                       once per crossing: the value has to drop below the
//                       threshold and cross it again to fire a second time.
//  - OncePerProcess   - sustains for sustain_seconds, then fires exactly once
//                       for the lifetime of the process.
//  - EveryNMinutes    - sustains, fires, and may fire again at most once per
//                       every_n_minutes while the condition keeps holding.
//
// A value at or below the threshold closes the window, so a flapping metric
// never accumulates partial progress towards a firing.
bool watchdog_should_fire(const Watchdog& w, double value, uint64_t now_ms,
                          WatchdogState& state);

}  // namespace rules
