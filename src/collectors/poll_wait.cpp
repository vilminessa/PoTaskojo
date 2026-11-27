#include "collectors/poll_wait.h"

#include <chrono>

namespace collectors {

bool PollWait::wait_for(uint32_t interval_ms) {
  std::unique_lock<std::mutex> lock(mutex_);
  const bool interrupted = cv_.wait_for(
      lock, std::chrono::milliseconds(interval_ms),
      [this] { return interrupted_; });
  // wait_for with a predicate reports the predicate, not the timeout.
  return !interrupted;
}

void PollWait::interrupt() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    interrupted_ = true;
  }
  cv_.notify_all();
}

void PollWait::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  interrupted_ = false;
}

}  // namespace collectors
