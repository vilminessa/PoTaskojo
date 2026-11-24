#pragma once
// Rules engine: match a program by exe name → apply process settings on start,
// plus watchdogs that react to resource limits.
//
// Safety breakers (from spec §5.2 — all mandatory):
//  1. auto-pause if > N end/suspend actions within a minute
//  2. per-rule action throttling (no re-apply loop)
//  3. refuse Windows-critical / protected processes
//  4. rules only run while the app is running
//  5. pause restores previous settings of affected programs

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rules {

// --- Data model (versioned serialization, spec §5.3) ---

// Data format of rules.tsv. v2 escapes spaces inside the program name, which
// is the only field that is read token-by-token; files written as v1 still load.
inline constexpr uint32_t kFormatVersion = 2;
inline constexpr size_t kMaxRules = 128;
inline constexpr size_t kMaxWatchdogsPerRule = 4;

enum class PriorityAction : uint8_t {
  Unchanged = 0,
  Idle,
  BelowNormal,
  Normal,
  AboveNormal,
  High,
  Realtime,
  EfficiencyMode,  // EcoQoS (Win11) / low priority fallback
};

enum class IoPriorityAction : uint8_t { Unchanged = 0, VeryLow, Low, Normal };

enum class StartAction : uint8_t {
  None = 0,
  EndIt,    // terminate within ~1s of launch
  Suspend,  // suspend on start
};

struct CpuAffinity {
  bool enabled = false;
  bool performance_cores_only = false;  // P-cores (hybrid CPUs)
  std::vector<uint8_t> mask;            // per-CPU byte (0/1), size = cpu count
};

struct Watchdog {
  enum class Metric : uint8_t { CpuPercent = 0, MemoryBytes, DiskBytesPerSec, NetBytesPerSec };
  enum class Response : uint8_t {
    Notify = 0,
    DumpMini,
    DumpFull,
    LowerPriority,
    Suspend,
    End,
    RunCommand,
  };
  enum class Frequency : uint8_t { OncePerProcess = 0, EveryNMinutes, Immediately };

  Metric metric = Metric::CpuPercent;
  double threshold = 90.0;
  uint32_t sustain_seconds = 30;   // "above X for Y"
  Response response = Response::Notify;
  Frequency frequency = Frequency::OncePerProcess;
  uint32_t every_n_minutes = 5;
  std::wstring command;            // for RunCommand
};

struct Rule {
  std::wstring exe_name;           // "chrome.exe" (match, case-insensitive)
  bool enabled = true;
  uint32_t version = kFormatVersion;

  PriorityAction priority = PriorityAction::Unchanged;
  IoPriorityAction io_priority = IoPriorityAction::Unchanged;
  CpuAffinity affinity;
  StartAction start_action = StartAction::None;

  std::vector<Watchdog> watchdogs;

  // Runtime state (not serialized as behavior, but kept for restore).
  // Filled by the engine: pids this rule currently "owns".
};

// --- Storage: versioned JSON-ish file in data dir (own format) ---

class RuleStore {
 public:
  static RuleStore& instance();

  bool load();   // returns false on version mismatch (newer format)
  bool save() const;

  std::vector<Rule> all() const;
  bool add(const Rule& r, std::string* error = nullptr);
  bool update(const std::wstring& exe_name, const Rule& r, std::string* error = nullptr);
  bool remove(const std::wstring& exe_name);
  bool has(const std::wstring& exe_name) const;
  size_t count() const;

  bool format_from_newer_version() const { return newer_version_; }

 private:
  RuleStore() = default;
  mutable std::mutex mutex_;
  std::vector<Rule> rules_;
  bool newer_version_ = false;
};

// --- Engine ---

class RuleEngine {
 public:
  static RuleEngine& instance();

  void start();
  void stop();

  bool paused() const { return paused_.load(); }
  void set_paused(bool p);

  // Called by collectors/UI after each process snapshot.
  void on_process_snapshot(const std::vector<std::wstring>& running_exes,
                           const std::vector<uint32_t>& running_pids);

  // Status for UI.
  struct Status {
    uint32_t applied_count = 0;
    uint32_t matched_rules = 0;
    uint32_t paused_reason = 0;  // 0 = running, 1 = safety breaker, 2 = user
    std::string last_event;
  };
  Status status() const;

 private:
  RuleEngine() = default;
  void note_action();       // safety breaker bookkeeping
  bool safety_ok() const;

  std::atomic<bool> running_{false};
  std::atomic<bool> paused_{false};
  std::atomic<uint32_t> paused_reason_{0};
  std::thread thread_;

  mutable std::mutex status_mutex_;
  Status status_;

  // Safety breaker: actions in the last rolling minute.
  mutable std::mutex breaker_mutex_;
  std::vector<uint64_t> action_timestamps_ms_;
};

}  // namespace rules
