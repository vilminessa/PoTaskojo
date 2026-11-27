#pragma once
// Connections collector: TCP/UDP endpoints via IP Helper (GetExtendedTcpTable
// / GetExtendedUdpTable) — the same source Windows Task Manager uses.

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

struct ConnectionRow {
  bool udp = false;
  std::wstring local_addr;
  uint16_t local_port = 0;
  std::wstring remote_addr;
  uint16_t remote_port = 0;
  uint32_t state = 0;        // MIB_TCP_STATE (TCP only)
  uint32_t pid = 0;
  std::wstring process_name; // resolved best-effort
};

struct ConnectionSnapshot {
  uint64_t seq = 0;
  std::vector<ConnectionRow> rows;
  size_t tcp_count = 0;
  size_t udp_count = 0;
  size_t established = 0;
  size_t listening = 0;
};

class ConnectionCollector {
 public:
  ~ConnectionCollector();
  void start(uint32_t interval_ms = 2000);
  void stop();
  std::shared_ptr<const ConnectionSnapshot> latest() const;

 private:
  void thread_main();
  void collect(ConnectionSnapshot& out);

  std::thread thread_;
  std::atomic<bool> running_{false};
  uint32_t interval_ms_ = 2000;
  PollWait wait_;
  mutable std::mutex mutex_;
  std::shared_ptr<const ConnectionSnapshot> latest_;
  std::unordered_map<uint32_t, std::wstring> name_cache_;
};

}  // namespace collectors
