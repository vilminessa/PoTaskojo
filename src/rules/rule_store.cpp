#include "rules/rules.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "core/log.h"
#include "core/paths.h"

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

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                    static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                      w.data(), n);
  return w;
}

std::wstring lower(std::wstring s) {
  for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
  return s;
}

// --- Minimal JSON-ish serialization (no external deps) ---
// Format (line-oriented, versioned):
//   TSRULES v1
//   rule <enabled> <exe_name>
//   prio <n> io <n> start <n>
//   aff <enabled> <pcores_only> <mask-hex>
//   wd <metric> <threshold> <sustain> <response> <freq> <every_n> <cmd>
//   end

std::string escape(const std::wstring& w) {
  std::string s = narrow(w);
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '\n' || c == '\r' || c == '\\') { out += '\\'; out += (c == '\n' ? 'n' : c == '\r' ? 'r' : '\\'); }
    else out += c;
  }
  return out;
}

std::wstring unescape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char n = s[++i];
      out += (n == 'n') ? '\n' : (n == 'r') ? '\r' : n;
    } else out += s[i];
  }
  return widen(out);
}

std::string mask_to_hex(const std::vector<uint8_t>& m) {
  static const char* hex = "0123456789abcdef";
  std::string out;
  for (uint8_t b : m) { out += hex[b >> 4]; out += hex[b & 0xF]; }
  return out;
}

std::vector<uint8_t> mask_from_hex(const std::string& h) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i + 1 < h.size(); i += 2) {
    auto val = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return 0;
    };
    out.push_back(static_cast<uint8_t>((val(h[i]) << 4) | val(h[i + 1])));
  }
  return out;
}

std::filesystem::path rules_file() {
  return core::paths::get().data_dir / L"rules.tsv";
}

}  // namespace

// ---------------------------------------------------------------------------
// RuleStore
// ---------------------------------------------------------------------------

RuleStore& RuleStore::instance() {
  static RuleStore s;
  return s;
}

bool RuleStore::load() {
  std::lock_guard lock(mutex_);
  rules_.clear();
  newer_version_ = false;

  std::ifstream in(rules_file(), std::ios::binary);
  if (!in) return true;  // no file yet — fine

  std::string line;
  if (!std::getline(in, line)) return true;
  if (line.rfind("TSRULES ", 0) != 0) {
    core::log::warn("rules file: bad header");
    return false;
  }
  const int ver = atoi(line.c_str() + 8);
  if (ver > static_cast<int>(kFormatVersion)) {
    core::log::warn("rules file: newer version " + std::to_string(ver));
    newer_version_ = true;
    return false;
  }

  Rule cur;
  bool in_rule = false;
  auto flush = [&] {
    if (in_rule && !cur.exe_name.empty()) rules_.push_back(cur);
    cur = Rule{};
    in_rule = false;
  };

  while (std::getline(in, line)) {
    if (line.rfind("rule ", 0) == 0) {
      flush();
      in_rule = true;
      std::istringstream ss(line.substr(5));
      int en = 1;
      std::string exe;
      ss >> en >> exe;
      cur.enabled = en != 0;
      cur.exe_name = unescape(exe);
    } else if (line.rfind("prio ", 0) == 0) {
      std::istringstream ss(line);
      std::string tag;
      int p, io, st;
      ss >> tag >> p >> tag >> io >> tag >> st;
      cur.priority = static_cast<PriorityAction>(p);
      cur.io_priority = static_cast<IoPriorityAction>(io);
      cur.start_action = static_cast<StartAction>(st);
    } else if (line.rfind("aff ", 0) == 0) {
      std::istringstream ss(line);
      std::string tag, hex;
      int en, pc;
      ss >> tag >> en >> pc >> hex;
      cur.affinity.enabled = en != 0;
      cur.affinity.performance_cores_only = pc != 0;
      cur.affinity.mask = mask_from_hex(hex);
    } else if (line.rfind("wd ", 0) == 0) {
      std::istringstream ss(line);
      std::string tag;
      Watchdog w;
      int metric, resp, freq;
      ss >> tag >> metric >> w.threshold >> w.sustain_seconds >> resp >> freq >>
          w.every_n_minutes;
      std::string cmd;
      std::getline(ss, cmd);
      if (!cmd.empty() && cmd[0] == ' ') cmd.erase(0, 1);
      w.metric = static_cast<Watchdog::Metric>(metric);
      w.response = static_cast<Watchdog::Response>(resp);
      w.frequency = static_cast<Watchdog::Frequency>(freq);
      w.command = unescape(cmd);
      if (cur.watchdogs.size() < kMaxWatchdogsPerRule)
        cur.watchdogs.push_back(std::move(w));
    } else if (line == "end") {
      flush();
    }
  }
  flush();

  core::log::info("rules loaded: " + std::to_string(rules_.size()));
  return true;
}

bool RuleStore::save() const {
  std::lock_guard lock(mutex_);
  std::ofstream out(rules_file(), std::ios::binary | std::ios::trunc);
  if (!out) {
    core::log::error("rules save failed");
    return false;
  }
  out << "TSRULES " << kFormatVersion << "\n";
  for (const auto& r : rules_) {
    out << "rule " << (r.enabled ? 1 : 0) << " " << escape(r.exe_name) << "\n";
    out << "prio " << static_cast<int>(r.priority) << " io "
        << static_cast<int>(r.io_priority) << " start "
        << static_cast<int>(r.start_action) << "\n";
    out << "aff " << (r.affinity.enabled ? 1 : 0) << " "
        << (r.affinity.performance_cores_only ? 1 : 0) << " "
        << mask_to_hex(r.affinity.mask) << "\n";
    for (const auto& w : r.watchdogs) {
      out << "wd " << static_cast<int>(w.metric) << " " << w.threshold << " "
          << w.sustain_seconds << " " << static_cast<int>(w.response) << " "
          << static_cast<int>(w.frequency) << " " << w.every_n_minutes << " "
          << escape(w.command) << "\n";
    }
    out << "end\n";
  }
  return true;
}

std::vector<Rule> RuleStore::all() const {
  std::lock_guard lock(mutex_);
  return rules_;
}

size_t RuleStore::count() const {
  std::lock_guard lock(mutex_);
  return rules_.size();
}

bool RuleStore::has(const std::wstring& exe_name) const {
  std::lock_guard lock(mutex_);
  const auto l = lower(exe_name);
  return std::any_of(rules_.begin(), rules_.end(), [&](const Rule& r) {
    return lower(r.exe_name) == l;
  });
}

bool RuleStore::add(const Rule& r, std::string* error) {
  std::lock_guard lock(mutex_);
  if (rules_.size() >= kMaxRules) {
    if (error) *error = "Rule limit reached (128).";
    return false;
  }
  const auto l = lower(r.exe_name);
  if (std::any_of(rules_.begin(), rules_.end(), [&](const Rule& x) {
        return lower(x.exe_name) == l;
      })) {
    if (error) *error = "A rule already exists for that program.";
    return false;
  }
  if (r.watchdogs.size() > kMaxWatchdogsPerRule) {
    if (error) *error = "A rule allows at most four monitors.";
    return false;
  }
  rules_.push_back(r);
  return true;
}

bool RuleStore::update(const std::wstring& exe_name, const Rule& r,
                       std::string* error) {
  std::lock_guard lock(mutex_);
  const auto l = lower(exe_name);
  auto it = std::find_if(rules_.begin(), rules_.end(), [&](const Rule& x) {
    return lower(x.exe_name) == l;
  });
  if (it == rules_.end()) {
    if (error) *error = "No rule exists for that program.";
    return false;
  }
  if (r.watchdogs.size() > kMaxWatchdogsPerRule) {
    if (error) *error = "A rule allows at most four monitors.";
    return false;
  }
  *it = r;
  return true;
}

bool RuleStore::remove(const std::wstring& exe_name) {
  std::lock_guard lock(mutex_);
  const auto l = lower(exe_name);
  auto it = std::find_if(rules_.begin(), rules_.end(), [&](const Rule& x) {
    return lower(x.exe_name) == l;
  });
  if (it == rules_.end()) return false;
  rules_.erase(it);
  return true;
}

}  // namespace rules
