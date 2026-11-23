#include "rules/rules.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>

#include "core/log.h"

namespace rules {
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

// Safety breaker thresholds (spec §5.2).
constexpr uint32_t kMaxActionsPerMinute = 20;   // end/suspend per rolling minute
constexpr uint32_t kActionWindowMs = 60000;

bool is_critical_pid(uint32_t pid) {
  if (pid == 0 || pid == 4) return true;   // idle / System
  if (pid == GetCurrentProcessId()) return true;
  // csrss/smss/wininit/winlogon/lsass/services live in session 0 with
  // base priorities 9..13; refuse them by name (same set Task Manager guards).
  return false;  // name check done by caller (needs snapshot)
}

bool name_is_protected(const std::wstring& exe) {
  static const wchar_t* kProtected[] = {
      L"csrss.exe",   L"smss.exe",     L"wininit.exe", L"winlogon.exe",
      L"lsass.exe",   L"services.exe", L"System",      L"Registry",
      L"Memory Compression", L"Secure System", L"fontdrvhost.exe",
      L"dwmsrv.exe",  L"svchost.exe",  L"taskhostw.exe",
  };
  std::wstring l = exe;
  for (auto& c : l) c = static_cast<wchar_t>(towlower(c));
  for (const wchar_t* p : kProtected)
    if (l == p) return true;
  return false;
}

int priority_class_for(PriorityAction a) {
  switch (a) {
    case PriorityAction::Idle: return IDLE_PRIORITY_CLASS;
    case PriorityAction::BelowNormal: return BELOW_NORMAL_PRIORITY_CLASS;
    case PriorityAction::Normal: return NORMAL_PRIORITY_CLASS;
    case PriorityAction::AboveNormal: return ABOVE_NORMAL_PRIORITY_CLASS;
    case PriorityAction::High: return HIGH_PRIORITY_CLASS;
    case PriorityAction::Realtime: return REALTIME_PRIORITY_CLASS;
    default: return 0;
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// RuleEngine
// ---------------------------------------------------------------------------

RuleEngine& RuleEngine::instance() {
  static RuleEngine s;
  return s;
}

void RuleEngine::start() {
  if (running_.exchange(true)) return;
  RuleStore::instance().load();
  core::log::info("rule engine started (rules=" +
                  std::to_string(RuleStore::instance().count()) + ")");
}

void RuleEngine::stop() {
  running_ = false;
}

void RuleEngine::set_paused(bool p) {
  paused_ = p;
  paused_reason_ = p ? 2 : 0;
  core::log::info(p ? "rules paused by user" : "rules resumed");
}

RuleEngine::Status RuleEngine::status() const {
  std::lock_guard lock(status_mutex_);
  Status s = status_;
  s.paused_reason = paused_reason_.load();
  return s;
}

bool RuleEngine::safety_ok() const {
  std::lock_guard lock(breaker_mutex_);
  const uint64_t now = GetTickCount64();
  // Drop timestamps older than the window.
  auto& ts = const_cast<std::vector<uint64_t>&>(action_timestamps_ms_);
  ts.erase(std::remove_if(ts.begin(), ts.end(),
                          [&](uint64_t t) { return now - t > kActionWindowMs; }),
           ts.end());
  return ts.size() < kMaxActionsPerMinute;
}

void RuleEngine::note_action() {
  std::lock_guard lock(breaker_mutex_);
  action_timestamps_ms_.push_back(GetTickCount64());
}

void RuleEngine::on_process_snapshot(
    const std::vector<std::wstring>& running_exes,
    const std::vector<uint32_t>& running_pids) {
  if (!running_ || paused_) return;

  const auto store_rules = RuleStore::instance().all();
  if (store_rules.empty()) return;

  if (!safety_ok()) {
    if (!paused_.exchange(true)) {
      paused_reason_ = 1;
      core::log::warn("All rules paused: more than " +
                      std::to_string(kMaxActionsPerMinute) +
                      " processes were ended or suspended in a minute.");
      std::lock_guard lock(status_mutex_);
      status_.paused_reason = 1;
      status_.last_event = "safety breaker: rules paused";
    }
    return;
  }

  // Applied set is keyed by PID (not exe) so every new instance of a matched
  // program gets the rule, and exited PIDs are forgotten.
  static std::mutex applied_mutex;
  static std::vector<std::pair<uint32_t, std::wstring>> applied;  // pid, exe

  // Forget exited PIDs.
  {
    std::lock_guard lock(applied_mutex);
    applied.erase(std::remove_if(applied.begin(), applied.end(),
                                 [&](const auto& a) {
                                   return std::find(running_pids.begin(),
                                                    running_pids.end(),
                                                    a.first) == running_pids.end();
                                 }),
                  applied.end());
  }

  uint32_t matched = 0;
  for (size_t idx = 0; idx < running_exes.size(); ++idx) {
    const auto& exe = running_exes[idx];
    const uint32_t pid = (idx < running_pids.size()) ? running_pids[idx] : 0;
    if (!pid) continue;

    for (const auto& rule : store_rules) {
      if (_wcsicmp(rule.exe_name.c_str(), exe.c_str()) != 0) continue;
      matched++;
      if (!rule.enabled) continue;
      if (name_is_protected(exe)) continue;
      if (is_critical_pid(pid)) continue;

      {
        std::lock_guard lock(applied_mutex);
        if (std::any_of(applied.begin(), applied.end(),
                        [&](const auto& a) { return a.first == pid; }))
          continue;  // already applied to this instance
        applied.emplace_back(pid, exe);
      }

      // Start action.
      if (rule.start_action != StartAction::None) {
        HANDLE proc = OpenProcess(PROCESS_SET_INFORMATION |
                                      PROCESS_QUERY_LIMITED_INFORMATION,
                                  FALSE, pid);
        if (proc) {
          if (rule.start_action == StartAction::EndIt) {
            note_action();
            TerminateProcess(proc, 0);
            core::log::info("rule: ended " + narrow(exe));
          } else if (rule.start_action == StartAction::Suspend) {
            if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
              using NtFn = LONG(NTAPI*)(HANDLE);
              if (auto fn = reinterpret_cast<NtFn>(
                      GetProcAddress(ntdll, "NtSuspendProcess"))) {
                note_action();
                fn(proc);
                core::log::info("rule: suspended " + narrow(exe));
              }
            }
          }
          CloseHandle(proc);
        }
      }

      // Priority.
      if (rule.priority != PriorityAction::Unchanged) {
        if (HANDLE proc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid)) {
          const int pc = priority_class_for(rule.priority);
          if (pc) SetPriorityClass(proc, static_cast<DWORD>(pc));
          CloseHandle(proc);
        }
      }

      {
        std::lock_guard lock(status_mutex_);
        status_.applied_count++;
        status_.matched_rules = matched;
        status_.last_event = "applied rule for " + narrow(exe);
      }
      break;  // one rule per exe (store enforces uniqueness)
    }
  }

  if (matched) {
    std::lock_guard lock(status_mutex_);
    status_.matched_rules = matched;
  }
}

}  // namespace rules
