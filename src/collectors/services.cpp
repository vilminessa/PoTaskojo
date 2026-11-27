#include "collectors/services.h"

#include <windows.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include "core/log.h"

namespace collectors {
namespace {

std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(),
                                    static_cast<int>(w.size()),
                                    nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                      s.data(), n, nullptr, nullptr);
  return s;
}

const wchar_t* state_str(uint32_t st) {
  switch (st) {
    case SERVICE_STOPPED: return L"Stopped";
    case SERVICE_START_PENDING: return L"Starting";
    case SERVICE_STOP_PENDING: return L"Stopping";
    case SERVICE_RUNNING: return L"Running";
    case SERVICE_CONTINUE_PENDING: return L"Resuming";
    case SERVICE_PAUSE_PENDING: return L"Pausing";
    case SERVICE_PAUSED: return L"Paused";
    default: return L"?";
  }
}

const wchar_t* startup_str(uint32_t t) {
  switch (t) {
    case SERVICE_BOOT_START: return L"Boot";
    case SERVICE_SYSTEM_START: return L"System";
    case SERVICE_AUTO_START: return L"Automatic";
    case SERVICE_DEMAND_START: return L"Manual";
    case SERVICE_DISABLED: return L"Disabled";
    default: return L"?";
  }
}

}  // namespace

ServiceCollector::~ServiceCollector() { stop(); }

void ServiceCollector::start(uint32_t interval_ms) {
  if (running_.exchange(true)) return;
  interval_ms_ = interval_ms ? interval_ms : 3000;
  wait_.reset();
  thread_ = std::thread([this] { thread_main(); });
}

void ServiceCollector::stop() {
  if (!running_.exchange(false)) return;
  wait_.interrupt();
  if (thread_.joinable()) thread_.join();
}

void ServiceCollector::refresh_now() {
  refresh_requested_ = true;
  // Without this the button would wait out the whole interval first.
  wait_.interrupt();
}

std::shared_ptr<const ServiceSnapshot> ServiceCollector::latest() const {
  std::lock_guard lock(mutex_);
  return latest_;
}

void ServiceCollector::thread_main() {
  core::log::info("service collector started");
  while (running_) {
    auto snap = std::make_shared<ServiceSnapshot>();
    collect(*snap);
    {
      std::lock_guard lock(mutex_);
      snap->seq = latest_ ? latest_->seq + 1 : 1;
      latest_ = std::move(snap);
    }
    refresh_requested_ = false;
    if (!wait_.wait_for(interval_ms_)) {
      // A refresh wakes the same wait a stop does. Stop has already cleared
      // running_, so testing it first keeps the two apart; the latch is only
      // cleared when polling is meant to continue.
      if (!running_) break;
      wait_.reset();
    }
  }
  core::log::info("service collector stopped");
}

void ServiceCollector::collect(ServiceSnapshot& out) {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE |
                                                       SC_MANAGER_CONNECT);
  if (!scm) {
    core::log::warn("OpenSCManager failed (access denied?)");
    return;
  }

  DWORD bytes_needed = 0, count = 0, resume = 0;
  EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                        SERVICE_STATE_ALL, nullptr, 0, &bytes_needed, &count,
                        &resume, nullptr);
  std::vector<uint8_t> buf(bytes_needed + 4096);
  resume = 0;
  if (EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                            SERVICE_STATE_ALL, buf.data(),
                            static_cast<DWORD>(buf.size()), &bytes_needed,
                            &count, &resume, nullptr)) {
    auto* entries = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buf.data());
    out.rows.reserve(count);
    for (DWORD i = 0; i < count; ++i) {
      ServiceRow r;
      r.name = entries[i].lpServiceName;
      r.display_name = entries[i].lpDisplayName;
      r.state = entries[i].ServiceStatusProcess.dwCurrentState;
      r.pid = entries[i].ServiceStatusProcess.dwProcessId;
      r.status = state_str(r.state);

      // Startup type requires per-service handle (best effort).
      if (SC_HANDLE svc = OpenServiceW(scm, r.name.c_str(), SERVICE_QUERY_CONFIG)) {
        DWORD need = 0;
        QueryServiceConfigW(svc, nullptr, 0, &need);
        std::vector<uint8_t> cfg(need + 64);
        if (QueryServiceConfigW(svc, reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(cfg.data()),
                                static_cast<DWORD>(cfg.size()), &need)) {
          auto* c = reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(cfg.data());
          r.startup_type = c->dwStartType;
        }
        CloseServiceHandle(svc);
      }
      out.rows.push_back(std::move(r));
    }
  }
  CloseServiceHandle(scm);
}

ServiceActionResult service_action(const std::wstring& name, const wchar_t* action) {
  ServiceActionResult res;
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) {
    res.message = "Access denied (run as administrator?)";
    return res;
  }
  SC_HANDLE svc = OpenServiceW(scm, name.c_str(),
                               SERVICE_START | SERVICE_STOP | SERVICE_PAUSE_CONTINUE |
                                   SERVICE_QUERY_STATUS);
  if (!svc) {
    CloseServiceHandle(scm);
    res.message = "Cannot open that service (access denied?)";
    return res;
  }

  SERVICE_STATUS_PROCESS st{};
  bool ok = false;
  if (wcscmp(action, L"start") == 0) ok = StartServiceW(svc, 0, nullptr) != FALSE;
  else if (wcscmp(action, L"stop") == 0) ok = ControlService(svc, SERVICE_CONTROL_STOP, reinterpret_cast<LPSERVICE_STATUS>(&st)) != FALSE;
  else if (wcscmp(action, L"pause") == 0) ok = ControlService(svc, SERVICE_CONTROL_PAUSE, reinterpret_cast<LPSERVICE_STATUS>(&st)) != FALSE;
  else if (wcscmp(action, L"continue") == 0) ok = ControlService(svc, SERVICE_CONTROL_CONTINUE, reinterpret_cast<LPSERVICE_STATUS>(&st)) != FALSE;
  else if (wcscmp(action, L"restart") == 0) {
    SERVICE_STATUS sst{};
    ControlService(svc, SERVICE_CONTROL_STOP, &sst);
    // Wait up to 10 s for stop.
    for (int i = 0; i < 100; ++i) {
      SERVICE_STATUS_PROCESS cur{};
      if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO,
                               reinterpret_cast<LPBYTE>(&cur), sizeof(cur), nullptr) &&
          cur.dwCurrentState == SERVICE_STOPPED)
        break;
      Sleep(100);
    }
    ok = StartServiceW(svc, 0, nullptr) != FALSE;
  }

  CloseServiceHandle(svc);
  CloseServiceHandle(scm);

  res.ok = ok;
  res.message = ok ? "Service operation succeeded"
                   : ("Failed (error " + std::to_string(GetLastError()) + ")");
  return res;
}

}  // namespace collectors
