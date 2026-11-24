#include "rules/rules.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>

#include "core/log.h"
#include "core/text.h"
#include "platform/crash_handler.h"
#include "platform/tray.h"
#include "rules/action_exec.h"
#include "rules/cpu_affinity.h"
#include "rules/watchdog_eval.h"

namespace rules {
namespace {

using core::text::narrow;
using core::text::widen;

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

// --- Watchdog bookkeeping -------------------------------------------------

// Per watched pid: the I/O counters a disk rate is derived from, plus one
// decision state per monitor of the rule. Dropped when the pid disappears, so
// a recycled pid never inherits somebody else's progress.
struct PidTrack {
  uint64_t prev_io_bytes = 0;
  uint64_t prev_ms = 0;
  bool have_io_baseline = false;
  std::vector<WatchdogState> monitors;
};

// Spec's dump ceiling: a full dump can be gigabytes, so a watchdog firing in a
// loop must not be able to fill the disk. Rolling window, shared by all rules.
constexpr uint32_t kMaxDumpsPerHour = 10;
constexpr uint64_t kDumpWindowMs = 60ull * 60ull * 1000ull;
// Windows replaces a balloon that is still on screen, so showing one every
// snapshot would announce almost nothing and annoy in the process.
constexpr uint64_t kBalloonIntervalMs = 5000;
std::mutex g_dump_mutex;
std::vector<uint64_t> g_dump_times_ms;

bool claim_dump_slot() {
  std::lock_guard lock(g_dump_mutex);
  const uint64_t now = GetTickCount64();
  g_dump_times_ms.erase(
      std::remove_if(g_dump_times_ms.begin(), g_dump_times_ms.end(),
                     [&](uint64_t t) { return now - t > kDumpWindowMs; }),
      g_dump_times_ms.end());
  if (g_dump_times_ms.size() >= kMaxDumpsPerHour) return false;
  g_dump_times_ms.push_back(now);
  return true;
}

// Reads one metric for this sample. False means "no usable number": the disk
// rate needs two samples to exist, so the first one only opens a baseline and
// the monitor is skipped rather than judged on a made-up value.
bool read_metric(Watchdog::Metric m, const ProcessInfo& p, PidTrack& track,
                 uint64_t now_ms, double& out) {
  switch (m) {
    case Watchdog::Metric::CpuPercent:
      out = p.cpu_percent;
      return true;
    case Watchdog::Metric::MemoryBytes:
      out = static_cast<double>(p.memory_bytes);
      return true;
    case Watchdog::Metric::DiskBytesPerSec: {
      const uint64_t total = p.read_bytes + p.write_bytes;
      if (!track.have_io_baseline) {
        track.have_io_baseline = true;
        track.prev_io_bytes = total;
        track.prev_ms = now_ms;
        return false;
      }
      const uint64_t dt = now_ms - track.prev_ms;
      const uint64_t delta =
          total >= track.prev_io_bytes ? total - track.prev_io_bytes : 0;
      track.prev_io_bytes = total;
      track.prev_ms = now_ms;
      if (dt == 0) return false;
      out = static_cast<double>(delta) * 1000.0 / static_cast<double>(dt);
      return true;
    }
  }
  return false;
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
  core::log::info(p ? "rules paused by user" : "rules re-enabled");
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

void RuleEngine::on_process_snapshot(const std::vector<ProcessInfo>& procs) {
  if (!running_ || paused_) return;

  const auto store_rules = RuleStore::instance().all();
  if (store_rules.empty()) return;

  if (!safety_ok()) {
    if (!paused_.exchange(true)) {
      paused_reason_ = 1;
      core::log::warn("Safety pause: " +
                      std::to_string(kMaxActionsPerMinute) +
                      " end/suspend actions within a minute.");
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
  static std::unordered_map<uint32_t, PidTrack> tracks;

  const uint64_t now = GetTickCount64();

  // Forget pids that are gone, together with everything remembered about them.
  {
    std::lock_guard lock(applied_mutex);
    const auto alive = [&](uint32_t pid) {
      return std::any_of(procs.begin(), procs.end(),
                         [&](const ProcessInfo& p) { return p.pid == pid; });
    };
    applied.erase(std::remove_if(applied.begin(), applied.end(),
                                 [&](const auto& a) { return !alive(a.first); }),
                  applied.end());
    for (auto it = tracks.begin(); it != tracks.end();) {
      if (!alive(it->first))
        it = tracks.erase(it);
      else
        ++it;
    }
  }

  uint32_t matched = 0;
  for (const auto& proc : procs) {
    if (!proc.pid) continue;
    const auto& exe = proc.exe;

    for (const auto& rule : store_rules) {
      if (_wcsicmp(rule.exe_name.c_str(), exe.c_str()) != 0) continue;
      matched++;
      if (!rule.enabled) continue;
      if (name_is_protected(exe)) continue;
      if (is_critical_pid(proc.pid)) continue;

      bool first_time = false;
      {
        std::lock_guard lock(applied_mutex);
        if (std::none_of(applied.begin(), applied.end(),
                         [&](const auto& a) { return a.first == proc.pid; })) {
          applied.emplace_back(proc.pid, exe);
          first_time = true;
        }
      }

      // Start action and priority are applied once per instance. Both go
      // through helpers that request the rights the call actually needs:
      // opening the process without PROCESS_TERMINATE or
      // PROCESS_SUSPEND_RESUME used to make these silently do nothing.
      if (first_time) {
        if (rule.start_action == StartAction::EndIt) {
          if (rules::end_process(proc.pid)) {
            note_action();
            core::log::info("rule: ended " + narrow(exe));
          } else {
            core::log::warn("rule: could not end " + narrow(exe));
          }
        } else if (rule.start_action == StartAction::Suspend) {
          if (rules::suspend_process(proc.pid)) {
            note_action();
            core::log::info("rule: suspended " + narrow(exe));
          } else {
            core::log::warn("rule: could not suspend " + narrow(exe));
          }
        }

        if (rule.priority != PriorityAction::Unchanged) {
          if (HANDLE handle = OpenProcess(PROCESS_SET_INFORMATION, FALSE,
                                          proc.pid)) {
            const int pc = priority_class_for(rule.priority);
            if (pc) SetPriorityClass(handle, static_cast<DWORD>(pc));
            CloseHandle(handle);
          }
        }

        // Affinity is applied once per instance too: re-applying it every
        // snapshot would fight whatever the program does with its own threads.
        if (rule.affinity.enabled) {
          if (!apply_affinity(rule.affinity, proc.pid))
            core::log::warn("rule: affinity not applied for " + narrow(exe));
        }

        {
          std::lock_guard lock(status_mutex_);
          status_.applied_count++;
          status_.matched_rules = matched;
          status_.last_event = "applied rule for " + narrow(exe);
        }
      }

      // Monitors are re-evaluated on every snapshot instead: they describe a
      // condition, not an event.
      if (!rule.watchdogs.empty()) {
        auto& track = tracks[proc.pid];
        if (track.monitors.size() != rule.watchdogs.size())
          track.monitors.assign(rule.watchdogs.size(), WatchdogState{});
        for (size_t i = 0; i < rule.watchdogs.size(); ++i) {
          const auto& w = rule.watchdogs[i];
          double value = 0.0;
          if (!read_metric(w.metric, proc, track, now, value)) continue;
          if (!watchdog_should_fire(w, value, now, track.monitors[i])) continue;
          fire_watchdog(w, proc);
        }
      }

      break;  // one rule per exe (store enforces uniqueness)
    }
  }

  if (matched) {
    std::lock_guard lock(status_mutex_);
    status_.matched_rules = matched;
  }
}

void RuleEngine::fire_watchdog(const Watchdog& w, const ProcessInfo& p) {
  const std::string exe = narrow(p.exe);
  // The outcome without the program name: the journal keeps the two apart so
  // it can show them as columns, while the log line joins them back together.
  std::string outcome;
  bool counted = false;  // counts against the safety breaker

  switch (w.response) {
    case Watchdog::Response::Notify:
      outcome = "resource limit reached";
      break;

    case Watchdog::Response::LowerPriority: {
      if (HANDLE handle = OpenProcess(PROCESS_SET_INFORMATION, FALSE, p.pid)) {
        const BOOL ok = SetPriorityClass(handle, BELOW_NORMAL_PRIORITY_CLASS);
        CloseHandle(handle);
        if (ok) outcome = "priority lowered";
      }
      break;
    }

    case Watchdog::Response::Suspend:
      if (!safety_ok()) return;
      if (rules::suspend_process(p.pid)) {
        counted = true;
        outcome = "suspended";
      } else {
        outcome = "could not suspend";
      }
      break;

    case Watchdog::Response::End:
      if (!safety_ok()) return;
      if (rules::end_process(p.pid)) {
        counted = true;
        outcome = "ended";
      } else {
        outcome = "could not end";
      }
      break;

    case Watchdog::Response::DumpMini:
    case Watchdog::Response::DumpFull: {
      if (!safety_ok()) return;
      if (!claim_dump_slot()) {
        outcome = "dump skipped, hourly limit reached";
        break;
      }
      const bool full = (w.response == Watchdog::Response::DumpFull);
      std::wstring why = L"watchdog: " + p.exe;
      if (platform::write_process_dump(p.pid, full, why.c_str())) {
        counted = true;
        outcome = full ? "complete dump written" : "minidump written";
      } else {
        outcome = "dump failed, process not readable";
      }
      break;
    }

    case Watchdog::Response::RunCommand: {
      if (w.command.empty() || !safety_ok()) break;
      STARTUPINFOW si{};
      si.cb = sizeof(si);
      PROCESS_INFORMATION pi{};
      std::wstring cmd = w.command;
      if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0,
                         nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        counted = true;
        outcome = "launched " + narrow(w.command);
      } else {
        outcome = "could not launch " + narrow(w.command);
      }
      break;
    }
  }

  if (outcome.empty()) return;
  if (counted) note_action();

  const std::string event = exe + ": " + outcome;
  core::log::info("watchdog: " + event);
  {
    std::lock_guard lock(status_mutex_);
    status_.last_event = event;
  }
  record_event(exe, w.response, outcome);
}

void RuleEngine::record_event(const std::string& program,
                              Watchdog::Response response,
                              const std::string& outcome) {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char time[16];
  snprintf(time, sizeof(time), "%02u:%02u:%02u", st.wHour, st.wMinute,
           st.wSecond);
  events_.add(RuleEvent{time, program, outcome});

  const uint64_t now = GetTickCount64();
  const bool show =
      last_balloon_ms_ == 0 || now - last_balloon_ms_ >= kBalloonIntervalMs;
  if (!show) return;
  last_balloon_ms_ = now;

  // Anything beyond a plain alert changed something, so it warns rather than
  // informs. The message is the same line the journal shows.
  const bool acted = response != Watchdog::Response::Notify &&
                     response != Watchdog::Response::LowerPriority;
  const std::wstring body = widen(program + ": " + outcome);
  platform::tray_balloon(L"PoTaskojo", body.c_str(), acted);
}

}  // namespace rules
