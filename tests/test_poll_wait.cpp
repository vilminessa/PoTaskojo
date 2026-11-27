// The wait every collector sleeps its interval in.
//
// The reason this exists is recorded in poll_wait.h: a chain of short Sleep
// calls compounds their overshoot, and the performance collector was sampling
// every 1.54 s while configured for 1.0. These tests pin the two properties
// that fix depends on - that a wait really waits, and that an interrupt ends it
// well before the interval - without asserting on an exact duration, which
// would be flaky on a loaded machine.

#include <chrono>
#include <thread>

#include "collectors/poll_wait.h"
#include "test_support.h"

using collectors::PollWait;

namespace {

long long since_ms(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - t0)
      .count();
}

}  // namespace

TEST_CASE(poll_wait_waits_out_its_interval) {
  PollWait wait;
  const auto t0 = std::chrono::steady_clock::now();
  CHECK(wait.wait_for(60));
  // It must not have come back early; the upper bound is generous because a
  // busy machine can stretch any sleep.
  CHECK(since_ms(t0) >= 50);
}

TEST_CASE(poll_wait_returns_immediately_when_already_interrupted) {
  PollWait wait;
  wait.interrupt();
  const auto t0 = std::chrono::steady_clock::now();
  CHECK(!wait.wait_for(10000));
  // Ten seconds configured, and it must not have spent them.
  CHECK(since_ms(t0) < 3000);
}

TEST_CASE(poll_wait_is_woken_by_an_interrupt_during_the_wait) {
  PollWait wait;
  bool returned = true;
  long long waited = 0;

  std::thread waiter([&] {
    const auto t0 = std::chrono::steady_clock::now();
    returned = wait.wait_for(10000);
    waited = since_ms(t0);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  wait.interrupt();
  waiter.join();

  // A lost wakeup here would show up as the full ten seconds.
  CHECK(!returned);
  CHECK(waited < 3000);
}

TEST_CASE(poll_wait_reset_lets_it_wait_again) {
  PollWait wait;
  wait.interrupt();
  CHECK(!wait.wait_for(10000));

  // Collectors are started once in practice, but a restart must not inherit
  // the latch and spin through its interval doing nothing.
  wait.reset();
  const auto t0 = std::chrono::steady_clock::now();
  CHECK(wait.wait_for(40));
  CHECK(since_ms(t0) >= 30);
}
