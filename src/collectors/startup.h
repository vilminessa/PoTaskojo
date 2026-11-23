#pragma once
// Startup apps collector: Run keys + Startup folders + StartupApproved state.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace collectors {

struct StartupRow {
  std::wstring name;
  std::wstring command;
  std::wstring location;   // "HKCU Run", "User startup folder", ...
  bool enabled = false;
  bool is_packaged = false;
  std::wstring file_path;  // resolved target (may be empty)
};

struct StartupSnapshot {
  uint64_t seq = 0;
  std::vector<StartupRow> rows;
  size_t enabled_count = 0;
};

class StartupCollector {
 public:
  ~StartupCollector();
  void start(uint32_t interval_ms = 10000);
  void stop();
  std::shared_ptr<const StartupSnapshot> latest() const;
  void refresh_now();

  // Actions.
  bool set_enabled(const StartupRow& row, bool enabled);
  bool remove(const StartupRow& row);

 private:
  void thread_main();
  void collect(StartupSnapshot& out);

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> refresh_requested_{false};
  uint32_t interval_ms_ = 10000;
  mutable std::mutex mutex_;
  std::shared_ptr<const StartupSnapshot> latest_;
};

}  // namespace collectors
