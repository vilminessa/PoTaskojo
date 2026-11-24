// The rule store is the only part of the rules feature with real state, so it
// gets the most attention: limits, case-insensitive identity, and a round trip
// through the file, which is where silent data loss would hide.

#include <filesystem>
#include <fstream>
#include <string>

#include "core/paths.h"
#include "rules/rules.h"
#include "test_support.h"

namespace {

rules::RuleStore& store() { return rules::RuleStore::instance(); }

std::filesystem::path rules_path() {
  return core::paths::get().data_dir / L"rules.tsv";
}

// The store is a process-wide singleton; drop its file and reload to get a
// clean slate without needing a production-only "clear" API.
void start_empty() {
  std::error_code ec;
  std::filesystem::remove(rules_path(), ec);
  CHECK(store().load());
}

rules::Watchdog make_watchdog() {
  rules::Watchdog w;
  w.metric = rules::Watchdog::Metric::MemoryBytes;
  w.threshold = 90.5;
  w.sustain_seconds = 45;
  w.response = rules::Watchdog::Response::DumpMini;
  w.frequency = rules::Watchdog::Frequency::EveryNMinutes;
  w.every_n_minutes = 7;
  w.command = L"C:\\Tools\\app.exe --verbose";
  return w;
}

}  // namespace

TEST_CASE(rule_store_treats_a_missing_file_as_empty) {
  start_empty();
  CHECK_EQ(store().count(), static_cast<size_t>(0));
  CHECK(!store().has(L"anything.exe"));
}

TEST_CASE(rule_store_rejects_an_unrecognised_header) {
  std::ofstream out(rules_path(), std::ios::binary | std::ios::trunc);
  out << "SOMETHING ELSE\n";
  out.close();
  CHECK(!store().load());
  CHECK(!store().format_from_newer_version());
  CHECK_EQ(store().count(), static_cast<size_t>(0));
}

TEST_CASE(rule_store_refuses_a_file_written_by_a_newer_version) {
  std::ofstream out(rules_path(), std::ios::binary | std::ios::trunc);
  out << "TSRULES 999\n";
  out.close();
  CHECK(!store().load());
  CHECK(store().format_from_newer_version());
  // Reloading a normal file clears the flag again.
  start_empty();
  CHECK(!store().format_from_newer_version());
}

TEST_CASE(rule_store_still_reads_a_file_written_by_version_one) {
  // The record layout is unchanged, so a file from an earlier release must
  // keep loading after the format version moves on.
  std::ofstream out(rules_path(), std::ios::binary | std::ios::trunc);
  // Exactly what v1 produced: backslashes escaped, spaces left alone.
  out << "TSRULES 1\n"
         "rule 1 legacy.exe\n"
         "prio 5 io 1 start 0\n"
         "aff 0 0 \n"
         "wd 1 90.5 45 2 1 7 C:\\\\Tools\\\\app.exe --verbose\n"
         "end\n";
  out.close();
  CHECK(store().load());
  CHECK(!store().format_from_newer_version());
  const auto all = store().all();
  CHECK_EQ(all.size(), static_cast<size_t>(1));
  if (all.empty()) return;
  CHECK_EQ(all[0].exe_name, std::wstring(L"legacy.exe"));
  CHECK_EQ(static_cast<int>(all[0].priority),
           static_cast<int>(rules::PriorityAction::High));
  CHECK_EQ(all[0].watchdogs.size(), static_cast<size_t>(1));
  if (all[0].watchdogs.size() == 1)
    CHECK_EQ(all[0].watchdogs[0].command,
             std::wstring(L"C:\\Tools\\app.exe --verbose"));
}

TEST_CASE(rule_store_identifies_rules_case_insensitively) {
  start_empty();
  rules::Rule r;
  r.exe_name = L"Demo.EXE";
  std::string err;
  CHECK(store().add(r, &err));
  CHECK_EQ(store().count(), static_cast<size_t>(1));
  CHECK(store().has(L"demo.exe"));
  CHECK(store().has(L"DEMO.EXE"));

  rules::Rule duplicate = r;
  duplicate.exe_name = L"demo.exe";
  CHECK(!store().add(duplicate, &err));
  CHECK(!err.empty());
  CHECK_EQ(store().count(), static_cast<size_t>(1));
}

TEST_CASE(rule_store_updates_and_removes_by_name) {
  start_empty();
  rules::Rule r;
  r.exe_name = L"a.exe";
  std::string err;
  CHECK(store().add(r, &err));

  rules::Rule changed = r;
  changed.enabled = false;
  CHECK(store().update(L"A.EXE", changed, &err));
  const auto after_update = store().all();
  CHECK_EQ(after_update.size(), static_cast<size_t>(1));
  CHECK(!after_update.empty() && !after_update[0].enabled);

  std::string missing_err;
  CHECK(!store().update(L"absent.exe", changed, &missing_err));
  CHECK(!missing_err.empty());

  CHECK(store().remove(L"a.exe"));
  CHECK(!store().remove(L"a.exe"));
  CHECK_EQ(store().count(), static_cast<size_t>(0));
}

TEST_CASE(rule_store_enforces_the_rule_limit) {
  start_empty();
  std::string err;
  for (size_t i = 0; i < rules::kMaxRules; ++i) {
    rules::Rule r;
    r.exe_name = L"p" + std::to_wstring(i) + L".exe";
    if (!store().add(r, &err)) break;
  }
  CHECK_EQ(store().count(), rules::kMaxRules);

  rules::Rule one_too_many;
  one_too_many.exe_name = L"one-too-many.exe";
  CHECK(!store().add(one_too_many, &err));
  CHECK(!err.empty());
  CHECK_EQ(store().count(), rules::kMaxRules);
}

TEST_CASE(rule_store_enforces_the_monitor_limit) {
  start_empty();
  rules::Rule ok;
  ok.exe_name = L"m.exe";
  for (size_t i = 0; i < rules::kMaxWatchdogsPerRule; ++i)
    ok.watchdogs.push_back(make_watchdog());
  std::string err;
  CHECK(store().add(ok, &err));

  rules::Rule too_many = ok;
  too_many.exe_name = L"m2.exe";
  too_many.watchdogs.push_back(make_watchdog());
  CHECK(!store().add(too_many, &err));
  CHECK(!err.empty());

  too_many.exe_name = L"m.exe";  // updating the existing rule must fail too
  CHECK(!store().update(L"m.exe", too_many, &err));
  CHECK_EQ(store().all()[0].watchdogs.size(), rules::kMaxWatchdogsPerRule);
}

TEST_CASE(rule_store_round_trips_every_field_through_the_file) {
  start_empty();
  rules::Rule r;
  r.exe_name = L"round trip.exe";  // a space: the record is whitespace-split
  r.enabled = false;
  r.priority = rules::PriorityAction::High;
  r.start_action = rules::StartAction::Suspend;
  r.affinity.enabled = true;
  r.affinity.performance_cores_only = true;
  r.affinity.mask = {1, 0, 1, 1};
  r.watchdogs.push_back(make_watchdog());

  std::string err;
  CHECK(store().add(r, &err));
  CHECK(store().save());
  CHECK(store().load());

  const auto all = store().all();
  CHECK_EQ(all.size(), static_cast<size_t>(1));
  if (all.empty()) return;

  const auto& g = all[0];
  CHECK_EQ(g.exe_name, std::wstring(L"round trip.exe"));
  CHECK_EQ(g.enabled, false);
  CHECK_EQ(static_cast<int>(g.priority),
           static_cast<int>(rules::PriorityAction::High));
  CHECK_EQ(static_cast<int>(g.start_action),
           static_cast<int>(rules::StartAction::Suspend));
  CHECK_EQ(g.affinity.enabled, true);
  CHECK_EQ(g.affinity.performance_cores_only, true);
  CHECK_EQ(g.affinity.mask.size(), static_cast<size_t>(4));
  CHECK_EQ(g.affinity.mask.size() == 4 ? g.affinity.mask[0] : 0, 1);
  CHECK_EQ(g.affinity.mask.size() == 4 ? g.affinity.mask[1] : 1, 0);

  CHECK_EQ(g.watchdogs.size(), static_cast<size_t>(1));
  if (g.watchdogs.size() != 1) return;
  const auto& w = g.watchdogs[0];
  CHECK_EQ(static_cast<int>(w.metric),
           static_cast<int>(rules::Watchdog::Metric::MemoryBytes));
  CHECK_EQ(w.threshold, 90.5);
  CHECK_EQ(w.sustain_seconds, 45u);
  CHECK_EQ(static_cast<int>(w.response),
           static_cast<int>(rules::Watchdog::Response::DumpMini));
  CHECK_EQ(static_cast<int>(w.frequency),
           static_cast<int>(rules::Watchdog::Frequency::EveryNMinutes));
  CHECK_EQ(w.every_n_minutes, 7u);
  CHECK_EQ(w.command, std::wstring(L"C:\\Tools\\app.exe --verbose"));
}
