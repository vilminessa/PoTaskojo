#pragma once
// Waiting out one polling interval.

#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace collectors {

// Waits out a polling interval, waking early when the collector is stopped.
//
// Replaces a chain of short Sleep calls. Every Sleep overshoots by up to a
// timer tick, and a chain of fifty of them compounds: the performance
// collector was configured for a one-second interval and actually sampled
// every 1.54 seconds. One wait bounds the error to a single tick.
//
// interrupt() sets the flag under the mutex and only then notifies. Setting it
// first and notifying without the lock would leave a window in which a waiter
// has already tested the predicate but not yet blocked, and the notification
// would be lost - the collector would then sleep out its full interval while
// trying to stop.
class PollWait {
 public:
  PollWait() = default;
  PollWait(const PollWait&) = delete;
  PollWait& operator=(const PollWait&) = delete;

  // Blocks for |interval_ms|. True when the interval elapsed on its own, false
  // when interrupt() was called first or arrived during the wait.
  bool wait_for(uint32_t interval_ms);

  // Wakes any waiter immediately. Idempotent.
  void interrupt();

  // Clears a previous interrupt so the same object can be started again.
  void reset();

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool interrupted_ = false;
};

}  // namespace collectors
