#pragma once
// Low-memory watcher: polls available physical memory and flags critical
// pressure. For MVP we surface a status-bar warning + optional message
// instead of a dedicated window.

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

namespace platform {

class LowMemoryWatcher {
 public:
  static LowMemoryWatcher& instance();

  // Callback invoked (on watcher thread) when state changes:
  //   true  = pressure is critical
  //   false = pressure eased
  using Callback = std::function<void(bool critical)>;

  void start(Callback on_change);
  void stop();

  bool critical() const { return critical_.load(); }
  uint64_t available_bytes() const { return available_.load(); }

 private:
  LowMemoryWatcher() = default;
  void thread_main();

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> critical_{false};
  std::atomic<uint64_t> available_{0};
  Callback callback_;
};

}  // namespace platform
