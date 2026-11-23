#include "platform/low_memory.h"

#include <windows.h>

#include "core/log.h"

namespace platform {
namespace {

// Critical threshold: < 256 MB free OR < 3% of total.
constexpr uint64_t kAbsThresholdBytes = 256ull * 1024 * 1024;
constexpr double kPctThreshold = 0.03;

bool compute_critical(uint64_t& available, uint64_t& total) {
  MEMORYSTATUSEX ms{};
  ms.dwLength = sizeof(ms);
  if (!GlobalMemoryStatusEx(&ms)) return false;
  available = ms.ullAvailPhys;
  total = ms.ullTotalPhys;
  const double pct = total ? static_cast<double>(available) / total : 1.0;
  return available < kAbsThresholdBytes || pct < kPctThreshold;
}

}  // namespace

LowMemoryWatcher& LowMemoryWatcher::instance() {
  static LowMemoryWatcher s;
  return s;
}

void LowMemoryWatcher::start(Callback on_change) {
  if (running_.exchange(true)) return;
  callback_ = std::move(on_change);
  thread_ = std::thread([this] { thread_main(); });
}

void LowMemoryWatcher::stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
}

void LowMemoryWatcher::thread_main() {
  core::log::info("low-memory watcher started");
  while (running_) {
    uint64_t avail = 0, total = 0;
    const bool crit = compute_critical(avail, total);
    available_ = avail;

    const bool was = critical_.exchange(crit);
    if (crit != was) {
      core::log::warn(crit ? "memory pressure: critically low"
                           : "memory pressure cleared");
      if (callback_) callback_(crit);
    }

    // Poll every 2 s (faster when critical for quicker recovery notice).
    for (int i = 0; i < (crit ? 10 : 20) && running_; ++i) Sleep(100);
  }
  core::log::info("low-memory watcher stopped");
}

}  // namespace platform
