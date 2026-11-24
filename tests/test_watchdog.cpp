// The sustain window and the repeat policy are the whole point of a watchdog:
// get them wrong and a machine gets hammered with actions, or the response
// never fires at all. This is the pure part of that behaviour.

#include "rules/watchdog_eval.h"
#include "test_support.h"

namespace {

rules::Watchdog make(rules::Watchdog::Frequency freq, double threshold = 90.0,
                     uint32_t sustain_seconds = 30,
                     uint32_t every_n_minutes = 5) {
  rules::Watchdog w;
  w.frequency = freq;
  w.threshold = threshold;
  w.sustain_seconds = sustain_seconds;
  w.every_n_minutes = every_n_minutes;
  return w;
}

}  // namespace

TEST_CASE(watchdog_never_fires_at_or_below_the_threshold) {
  auto w = make(rules::Watchdog::Frequency::OncePerProcess);
  rules::WatchdogState st;
  CHECK(!rules::watchdog_should_fire(w, 90.0, 1000, st));  // exactly at it
  CHECK(!rules::watchdog_should_fire(w, 89.9, 2000, st));
  CHECK(!rules::watchdog_should_fire(w, 0.0, 3000, st));
  CHECK(!st.armed);
}

TEST_CASE(watchdog_waits_out_the_sustain_window) {
  auto w = make(rules::Watchdog::Frequency::OncePerProcess, 90.0, 30);
  rules::WatchdogState st;
  CHECK(!rules::watchdog_should_fire(w, 95.0, 1000, st));   // window opens
  CHECK(st.armed);
  CHECK(!rules::watchdog_should_fire(w, 95.0, 20000, st));  // 19 s held
  CHECK(!rules::watchdog_should_fire(w, 95.0, 30999, st));  // one ms short
  CHECK(rules::watchdog_should_fire(w, 95.0, 31000, st));   // 30 s held
}

TEST_CASE(watchdog_once_per_process_fires_exactly_once) {
  auto w = make(rules::Watchdog::Frequency::OncePerProcess, 90.0, 0);
  rules::WatchdogState st;
  CHECK(rules::watchdog_should_fire(w, 95.0, 1000, st));
  CHECK(!rules::watchdog_should_fire(w, 95.0, 2000, st));
  CHECK(!rules::watchdog_should_fire(w, 99.0, 999999, st));

  // A dip and a fresh crossing must not grant a second firing.
  CHECK(!rules::watchdog_should_fire(w, 10.0, 1000000, st));
  CHECK(!rules::watchdog_should_fire(w, 95.0, 1001000, st));
}

TEST_CASE(watchdog_immediately_skips_the_window_but_fires_per_crossing) {
  auto w = make(rules::Watchdog::Frequency::Immediately, 90.0, 30);
  rules::WatchdogState st;
  CHECK(rules::watchdog_should_fire(w, 95.0, 1000, st));    // no waiting
  CHECK(!rules::watchdog_should_fire(w, 95.0, 1001, st));   // same crossing
  CHECK(!rules::watchdog_should_fire(w, 95.0, 5000, st));   // still held
  CHECK(!rules::watchdog_should_fire(w, 50.0, 6000, st));   // drops below
  CHECK(rules::watchdog_should_fire(w, 95.0, 7000, st));    // crossed again
}

TEST_CASE(watchdog_every_n_minutes_respects_the_interval) {
  auto w = make(rules::Watchdog::Frequency::EveryNMinutes, 90.0, 0, 5);
  rules::WatchdogState st;
  CHECK(rules::watchdog_should_fire(w, 95.0, 1000, st));
  CHECK(!rules::watchdog_should_fire(w, 95.0, 60000, st));            // 1 min
  CHECK(!rules::watchdog_should_fire(w, 95.0, 1000 + 5 * 60000 - 1,
                                     st));                             // 1 ms early
  CHECK(rules::watchdog_should_fire(w, 95.0, 1000 + 5 * 60000, st));  // 5 min
}

TEST_CASE(watchdog_a_dip_restarts_the_sustain_window) {
  auto w = make(rules::Watchdog::Frequency::OncePerProcess, 90.0, 30);
  rules::WatchdogState st;
  CHECK(!rules::watchdog_should_fire(w, 95.0, 1000, st));
  CHECK(!rules::watchdog_should_fire(w, 95.0, 20000, st));  // 19 s in
  CHECK(!rules::watchdog_should_fire(w, 10.0, 21000, st));  // dips below
  CHECK(!st.armed);

  CHECK(!rules::watchdog_should_fire(w, 95.0, 40000, st));   // reopens here
  CHECK(!rules::watchdog_should_fire(w, 95.0, 69000, st));   // 29 s later
  CHECK(rules::watchdog_should_fire(w, 95.0, 70000, st));    // 30 s later
}
