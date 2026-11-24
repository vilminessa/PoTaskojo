// The pieces around the engine are covered elsewhere - what is not covered is
// the engine itself: matching a rule by name, applying it exactly once per
// process instance, and turning the snapshot into the actions it promises.
// Those broke silently before, so they are checked against a live process.

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

#include "core/paths.h"
#include "rules/cpu_affinity.h"
#include "rules/rules.h"
#include "test_process.h"
#include "test_support.h"

namespace {

rules::RuleStore& store() { return rules::RuleStore::instance(); }

std::filesystem::path rules_path() {
  return core::paths::get().data_dir / L"rules.tsv";
}

void reset_store() {
  std::error_code ec;
  std::filesystem::remove(rules_path(), ec);
  CHECK(store().load());
}

// The engine is a singleton whose start() re-reads the store, so every case
// starts from a known state: empty file, fresh rule, engine restarted.
void restart_engine_with(const rules::Rule& rule) {
  reset_store();
  std::string err;
  CHECK(store().add(rule, &err));
  CHECK(store().save());

  auto& engine = rules::RuleEngine::instance();
  engine.stop();
  engine.start();
  engine.set_paused(false);
}

std::vector<rules::ProcessInfo> snapshot_for(uint32_t pid) {
  rules::ProcessInfo p;
  p.exe = L"ping.exe";
  p.pid = pid;
  return {std::move(p)};
}

}  // namespace

TEST_CASE(rule_engine_applies_a_matched_rule_once_per_process) {
  Child child;
  CHECK(child.start());
  if (!child.handle()) return;

  const int cores = rules::system_core_count();
  rules::Rule r;
  r.exe_name = L"ping.exe";
  r.priority = rules::PriorityAction::High;
  r.affinity.enabled = true;
  r.affinity.mask.assign(static_cast<size_t>(cores), 0);
  r.affinity.mask[0] = 1;  // pin to processor 0
  restart_engine_with(r);

  auto& engine = rules::RuleEngine::instance();
  const auto procs = snapshot_for(child.pid());
  engine.on_process_snapshot(procs);

  HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SET_INFORMATION,
                         FALSE, child.pid());
  CHECK(h != nullptr);
  if (!h) {
    engine.stop();
    return;
  }
  CHECK_EQ(GetPriorityClass(h), static_cast<DWORD>(HIGH_PRIORITY_CLASS));
  ULONG_PTR got = 0;
  ULONG_PTR system = 0;
  CHECK(GetProcessAffinityMask(h, &got, &system));
  CHECK_EQ(got, static_cast<ULONG_PTR>(1));
  CloseHandle(h);

  // Change both behind the engine's back and take a second snapshot: the rule
  // is applied once per instance, so neither value may come back.
  h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SET_INFORMATION, FALSE,
                  child.pid());
  CHECK(h != nullptr);
  if (h) {
    SetPriorityClass(h, IDLE_PRIORITY_CLASS);
    SetProcessAffinityMask(h, static_cast<ULONG_PTR>(system));
    CloseHandle(h);
  }
  engine.on_process_snapshot(procs);

  h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, child.pid());
  CHECK(h != nullptr);
  if (h) {
    CHECK_EQ(GetPriorityClass(h), static_cast<DWORD>(IDLE_PRIORITY_CLASS));
    CHECK(GetProcessAffinityMask(h, &got, &system));
    CHECK_EQ(got, system);  // untouched by the second snapshot
    CloseHandle(h);
  }
  engine.stop();
}

TEST_CASE(rule_engine_applies_a_start_action) {
  Child child;
  CHECK(child.start());
  if (!child.handle()) return;

  rules::Rule r;
  r.exe_name = L"ping.exe";
  r.start_action = rules::StartAction::EndIt;
  restart_engine_with(r);

  auto& engine = rules::RuleEngine::instance();
  engine.on_process_snapshot(snapshot_for(child.pid()));

  // The action opens the process with PROCESS_TERMINATE; without it the child
  // would keep running and this would time out.
  CHECK_EQ(WaitForSingleObject(child.handle(), 5000),
           static_cast<DWORD>(WAIT_OBJECT_0));
  engine.stop();
}

TEST_CASE(rule_engine_ignores_a_rule_for_a_program_that_is_not_running) {
  Child child;
  CHECK(child.start());
  if (!child.handle()) return;

  rules::Rule r;
  r.exe_name = L"something-else.exe";
  r.priority = rules::PriorityAction::High;
  r.start_action = rules::StartAction::EndIt;
  restart_engine_with(r);

  auto& engine = rules::RuleEngine::instance();
  engine.on_process_snapshot(snapshot_for(child.pid()));

  // Nothing matched: the child is alive and its priority is untouched.
  CHECK_EQ(WaitForSingleObject(child.handle(), 0),
           static_cast<DWORD>(WAIT_TIMEOUT));
  HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, child.pid());
  CHECK(h != nullptr);
  if (h) {
    CHECK(GetPriorityClass(h) != static_cast<DWORD>(HIGH_PRIORITY_CLASS));
    CloseHandle(h);
  }
  engine.stop();
}
