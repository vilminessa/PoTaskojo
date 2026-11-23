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

namespace collectors {

struct CpuSample {
  double total_percent = 0.0;   // 0..100 across all cores
  std::vector<double> per_core; // 0..100 each
};

struct MemorySample {
  uint64_t total_bytes = 0;
  uint64_t used_bytes = 0;
  uint64_t committed_bytes = 0;
  uint64_t commit_limit = 0;
};

struct DiskSample {
  uint64_t bytes_per_sec = 0;   // physical disk total (read+write)
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

class PerfCollector {
 public:
  ~PerfCollector();

  void start(uint32_t interval_ms = 1000);
  void stop();
  std::shared_ptr<const PerfSnapshot> latest() const;

 private:
  void thread_main();
  bool init_pdh();
  void close_pdh();
  void sample(PerfSnapshot& out);

  std::thread thread_;
  std::atomic<bool> running_{false};
  uint32_t interval_ms_ = 1000;

  mutable std::mutex mutex_;
  std::shared_ptr<const PerfSnapshot> latest_;

  void* pdh_state_ = nullptr;  // opaque PdhState*
  bool pdh_ready_ = false;
};

}  // namespace collectors
