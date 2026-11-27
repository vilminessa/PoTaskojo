#pragma once
// Performance metrics collector: CPU (total + per-core), memory, disk, network.
// Uses PDH counters (pdh.dll) — the same source Task Manager relies on.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "collectors/poll_wait.h"

namespace collectors {

struct CpuSample {
  double total_percent = 0.0;   // 0..100 across all cores
  double kernel_percent = 0.0;  // % Privileged Time (kernel + DPC/ISR)
  std::vector<double> per_core; // 0..100 each
};

struct MemorySample {
  uint64_t total_bytes = 0;
  uint64_t used_bytes = 0;
  uint64_t committed_bytes = 0;
  uint64_t commit_limit = 0;
  uint64_t swap_used_bytes = 0;   // pagefile usage
  uint64_t swap_total_bytes = 0;  // pagefile size
};

struct DiskSample {
  uint64_t bytes_per_sec = 0;   // physical disk total (read+write)
  uint64_t read_bytes_s = 0;    // read rate
  uint64_t write_bytes_s = 0;   // write rate
};

struct NetworkSample {
  uint64_t bytes_sent_s = 0;
  uint64_t bytes_recv_s = 0;
};

struct PerfSnapshot {
  uint64_t seq = 0;
  uint64_t taken_ms = 0;
  CpuSample cpu;
  MemorySample memory;
  DiskSample disk;
  NetworkSample net;
};

// Upper bound on the polling interval, in milliseconds. The collector measures
// what a sample actually costs early on and backs off from this figure if four
// of them would not fit into it - see thread_main(). The history cap is derived
// from this planned figure, which is safe in one direction only: too large a
// cap costs memory, too small one would silently shorten the window.
inline constexpr uint32_t kSampleIntervalMs = 100;

class PerfCollector {
 public:
  ~PerfCollector();

  void start(uint32_t interval_ms = kSampleIntervalMs);
  void stop();
  std::shared_ptr<const PerfSnapshot> latest() const;

 private:
  void thread_main();
  bool init_pdh();
  void close_pdh();
  void sample(PerfSnapshot& out);

  std::thread thread_;
  std::atomic<bool> running_{false};
  uint32_t interval_ms_ = kSampleIntervalMs;
  PollWait wait_;

  mutable std::mutex mutex_;
  std::shared_ptr<const PerfSnapshot> latest_;

  void* pdh_state_ = nullptr;  // opaque PdhState*
  bool pdh_ready_ = false;
};

}  // namespace collectors
