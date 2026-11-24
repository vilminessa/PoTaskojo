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

// No I/O priority here on purpose: Windows exposes no documented way to set it
// for a whole process, so a field would only ever read "Unchanged". The rules
// file keeps its slot, so existing files still load.
//
// Likewise no network metric: per-process byte counts need ETW, which has not
// been wired up yet. Append new metrics as fresh values so old files keep
// their meaning.
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
  enum class Metric : uint8_t { CpuPercent = 0, MemoryBytes, DiskBytesPerSec };
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
  CpuAffinity affinity;
  StartAction start_action = StartAction::None;

  std::vector<Watchdog> watchdogs;

  // Runtime state (not serialized as behavior, but kept for restore).
  // Filled by the engine: pids this rule currently "owns".
};

// One process as the engine sees it. A name and a pid are enough to apply a
// rule on start, but watchdogs compare per-process metrics against thresholds,
// so the snapshot carries them along.
struct ProcessInfo {
  std::wstring exe;
  uint32_t pid = 0;
  double cpu_percent = 0.0;   // since the previous sample, all cores = 100
  uint64_t memory_bytes = 0;  // working set
  uint64_t read_bytes = 0;    // cumulative I/O
  uint64_t write_bytes = 0;
};

// Counts derived from the last enumerator of each set. The editor's combo
// boxes iterate to these, so appending a value extends them automatically;
// inserting one in the middle silently puts the new value on the shelf.
inline constexpr int kPriorityActionCount =
    static_cast<int>(PriorityAction::EfficiencyMode) + 1;
inline constexpr int kStartActionCount = static_cast<int>(StartAction::Suspend) + 1;
inline constexpr int kMetricCount =
    static_cast<int>(Watchdog::Metric::DiskBytesPerSec) + 1;
inline constexpr int kResponseCount =
    static_cast<int>(Watchdog::Response::RunCommand) + 1;
inline constexpr int kFrequencyCount =
    static_cast<int>(Watchdog::Frequency::Immediately) + 1;

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

  // Called by the UI after each process snapshot.
  void on_process_snapshot(const std::vector<ProcessInfo>& procs);

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
  // Carries out one watchdog response. Ending, suspending, dumping or
  // launching something counts against the safety breaker.
  void fire_watchdog(const Watchdog& w, const ProcessInfo& p);

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
