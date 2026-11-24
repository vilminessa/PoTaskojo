#include "rules/watchdog_eval.h"

namespace rules {

bool watchdog_should_fire(const Watchdog& w, double value, uint64_t now_ms,
                          WatchdogState& state) {
  if (!(value > w.threshold)) {
    // Close the window: a metric that dips below the threshold starts the
    // sustain count over on the next crossing.
    state.armed = false;
    state.above_before = false;
    state.armed_since_ms = 0;
    return false;
  }

  const bool fresh_crossing = !state.above_before;
  state.above_before = true;
  if (!state.armed) {
    state.armed = true;
    state.armed_since_ms = now_ms;
  }

  const bool skips_sustain =
      w.frequency == Watchdog::Frequency::Immediately;
  const uint64_t needed_ms =
      skips_sustain ? 0ull
                    : static_cast<uint64_t>(w.sustain_seconds) * 1000ull;
  if (now_ms - state.armed_since_ms < needed_ms) return false;

  // "Immediately" is once per crossing rather than once per sample, otherwise
  // a metric that stays high would fire on every snapshot.
  if (skips_sustain && !fresh_crossing) return false;

  switch (w.frequency) {
    case Watchdog::Frequency::OncePerProcess:
      if (state.last_fired_ms != 0) return false;
      break;
    case Watchdog::Frequency::EveryNMinutes: {
      const uint64_t interval_ms =
          static_cast<uint64_t>(w.every_n_minutes) * 60000ull;
      if (state.last_fired_ms != 0 && now_ms - state.last_fired_ms < interval_ms)
        return false;
      break;
    }
    case Watchdog::Frequency::Immediately:
      break;
  }

  state.last_fired_ms = now_ms;
  return true;
}

}  // namespace rules
