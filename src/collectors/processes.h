#pragma once
// Process collector: enumerates processes with CPU / memory / I/O metrics.
//
// Design mirrors the recovered architecture (spec §1.3, §4):
//   worker thread  -> builds a snapshot into a staging buffer
//   UI thread      -> atomically swaps in the latest completed snapshot
// The UI never blocks on collection.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "collectors/poll_wait.h"

namespace collectors {

struct ProcessRow {
  uint32_t pid = 0;
  uint32_t parent_pid = 0;
  std::wstring name;          // exe file name
  std::wstring image_path;    // full path (may be empty)
  std::wstring title;         // main window title (may be empty)
  uint64_t cpu_total_ms = 0;  // cumulative kernel+user time
  double cpu_percent = 0.0;   // usage since previous sample (all cores = 100)
  uint64_t working_set = 0;
  uint64_t private_bytes = 0;
  uint64_t read_bytes = 0;    // cumulative I/O
  uint64_t write_bytes = 0;
  int priority_class = 0;     // win32 priority class (0 = dynamic/unknown)
  int base_priority = 0;      // raw base priority (4..31), for "Base N" display
  bool is_elevated = false;
  bool is_wow64 = false;
  bool is_new = false;        // appeared since last snapshot
};

struct ProcessSnapshot {
  uint64_t seq = 0;           // monotonically increasing
  uint64_t taken_ms = 0;      // GetTickCount64 at capture
  std::vector<ProcessRow> rows;
};

// Thread-safe snapshot handoff: producer publishes, UI reads.
class ProcessCollector {
 public:
  ~ProcessCollector();

  void start(uint32_t interval_ms = 1000);
  void stop();

  // Copies the latest snapshot (cheap: shared_ptr swap semantics).
  std::shared_ptr<const ProcessSnapshot> latest() const;

  // Totals for the status bar.
  struct Totals {
    uint32_t process_count = 0;
    double cpu_percent = 0.0;
    uint64_t working_set = 0;
  };
  Totals totals() const;

 private:
  void thread_main();
  void collect(ProcessSnapshot& out);

  std::thread thread_;
  std::atomic<bool> running_{false};
  uint32_t interval_ms_ = 1000;
  PollWait wait_;

  mutable std::mutex mutex_;
  std::shared_ptr<const ProcessSnapshot> latest_;
  Totals totals_{};

  // Previous sample for CPU delta computation: pid -> (cpu_total_ms, tick).
  struct PrevEntry {
    uint64_t cpu_ms;
    uint64_t tick_ms;
  };
  std::unordered_map<uint32_t, PrevEntry> prev_;
};

}  // namespace collectors
