#pragma once
// Services collector: enumerate services + start/stop/pause/restart actions.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "collectors/poll_wait.h"

namespace collectors {

struct ServiceRow {
  std::wstring name;          // "Spooler"
  std::wstring display_name;  // "Print Spooler"
  std::wstring status;        // Running / Stopped / Paused / ...
  uint32_t state = 0;         // SERVICE_STATE
  uint32_t startup_type = 0;  // SERVICE_START_TYPE
  uint32_t pid = 0;           // 0 if stopped
  std::wstring description;
};

struct ServiceSnapshot {
  uint64_t seq = 0;
  std::vector<ServiceRow> rows;
};

class ServiceCollector {
 public:
  ~ServiceCollector();
  void start(uint32_t interval_ms = 3000);
  void stop();
  std::shared_ptr<const ServiceSnapshot> latest() const;
  void refresh_now();  // force immediate re-enumeration

 private:
  void thread_main();
  void collect(ServiceSnapshot& out);

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> refresh_requested_{false};
  uint32_t interval_ms_ = 3000;
  PollWait wait_;
  mutable std::mutex mutex_;
  std::shared_ptr<const ServiceSnapshot> latest_;
};

// Service actions (require administrator for most).
struct ServiceActionResult {
  bool ok = false;
  std::string message;
};
ServiceActionResult service_action(const std::wstring& name, const wchar_t* action);
// action in: L"start", L"stop", L"pause", L"continue", L"restart"

}  // namespace collectors
