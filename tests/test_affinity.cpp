// Affinity has two halves: deciding which cores to pin to - pure, and the half
// where reading EfficiencyClass backwards would pin a process to the slow cores
// instead of the fast ones - and handing the result to the OS, which can only
// be proved against a live process.

#include <windows.h>

#include <vector>

#include "rules/cpu_affinity.h"
#include "test_process.h"
#include "test_support.h"

namespace {

rules::CoreInfo core(uint8_t cls, uint64_t processors) {
  rules::CoreInfo c;
  c.efficiency_class = cls;
  c.processors = processors;
  return c;
}

}  // namespace

TEST_CASE(affinity_treats_a_homogeneous_cpu_as_every_core) {
  // Windows reports a non-zero EfficiencyClass only on heterogeneous systems,
  // so a machine with no split reports 0 everywhere. Every core then belongs to
  // the fastest class, and applying that must not narrow anything.
  std::vector<rules::CoreInfo> cores;
  for (uint64_t i = 0; i < 6; ++i) cores.push_back(core(0, 1ull << i));
  CHECK_EQ(rules::fast_core_mask(cores), static_cast<uint64_t>(0x3F));
}

TEST_CASE(affinity_picks_the_fastest_class_only) {
  // Direction per Microsoft: a higher EfficiencyClass means greater performance.
  std::vector<rules::CoreInfo> cores;
  cores.push_back(core(1, 0x0F));  // processors 0..3
  cores.push_back(core(0, 0xF0));  // processors 4..7
  CHECK_EQ(rules::fast_core_mask(cores), static_cast<uint64_t>(0x0F));

  // A third, even faster class wins over the middle one.
  cores.push_back(core(7, 0x100));
  CHECK_EQ(rules::fast_core_mask(cores), static_cast<uint64_t>(0x100));
}

TEST_CASE(affinity_mask_is_zero_when_the_query_fails) {
  CHECK_EQ(rules::fast_core_mask({}), static_cast<uint64_t>(0));
}

TEST_CASE(affinity_mask_keeps_only_cores_this_machine_has) {
  // A rule written on a 12-core machine with processors 0..5 selected.
  std::vector<uint8_t> on_twelve(12, 0);
  for (size_t i = 0; i < 6; ++i) on_twelve[i] = 1;

  CHECK_EQ(rules::usable_mask(on_twelve.data(), on_twelve.size(), 12),
           static_cast<uint64_t>(0x3F));
  // Read on a 4-core machine: 4 and 5 do not exist there, 0..3 still pin.
  CHECK_EQ(rules::usable_mask(on_twelve.data(), on_twelve.size(), 4),
           static_cast<uint64_t>(0x0F));

  std::vector<uint8_t> nothing(4, 0);
  CHECK_EQ(rules::usable_mask(nothing.data(), nothing.size(), 4),
           static_cast<uint64_t>(0));
}

TEST_CASE(affinity_mask_survives_a_full_group) {
  // 64 processors is a legal mask, and shifting a 64-bit value by 64 is not.
  std::vector<uint8_t> all(64, 1);
  CHECK_EQ(rules::usable_mask(all.data(), all.size(), 64),
           ~static_cast<uint64_t>(0));

  // More bytes than the machine can address must not run past the group.
  std::vector<uint8_t> too_many(96, 1);
  CHECK_EQ(rules::usable_mask(too_many.data(), too_many.size(), 96),
           ~static_cast<uint64_t>(0));
}

TEST_CASE(affinity_is_a_no_op_when_disabled_or_the_pid_is_zero) {
  rules::CpuAffinity a;
  a.enabled = false;
  a.mask.assign(4, 1);
  CHECK(!rules::apply_affinity(a, 1234));

  a.enabled = true;
  CHECK(!rules::apply_affinity(a, 0));
}

TEST_CASE(system_cores_are_readable_and_stay_in_range) {
  const auto cores = rules::query_cores();
  CHECK(!cores.empty());
  if (cores.empty()) return;

  uint64_t covered = 0;
  for (const auto& c : cores) covered |= c.processors;
  CHECK(covered != 0);
  CHECK(rules::fast_core_mask(cores) != 0);

  // Nothing outside the range this build can address may come back, or the
  // mask handed to SetProcessAffinityMask would be rejected outright.
  const int count = rules::system_core_count();
  CHECK(count >= 1);
  const uint64_t addressable =
      count >= 64 ? ~static_cast<uint64_t>(0) : ((1ull << count) - 1);
  CHECK_EQ(covered & ~addressable, static_cast<uint64_t>(0));
}

TEST_CASE(affinity_fast_cores_path_leaves_a_valid_mask) {
  Child child;
  CHECK(child.start());
  if (!child.handle()) return;

  rules::CpuAffinity a;
  a.enabled = true;
  a.performance_cores_only = true;
  CHECK(rules::apply_affinity(a, child.pid()));

  HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, child.pid());
  CHECK(h != nullptr);
  if (!h) return;
  ULONG_PTR got = 0;
  ULONG_PTR system = 0;
  CHECK(GetProcessAffinityMask(h, &got, &system));
  // Whatever the machine reports: a non-empty set that only uses processors
  // which actually exist.
  CHECK(got != 0);
  CHECK_EQ(got & ~system, static_cast<ULONG_PTR>(0));
  CloseHandle(h);
}

TEST_CASE(affinity_pins_a_live_process_to_the_requested_cores) {
  Child child;
  CHECK(child.start());
  if (!child.handle()) return;

  const int cores = rules::system_core_count();
  CHECK(cores >= 1);
  std::vector<uint8_t> mask(static_cast<size_t>(cores), 0);
  mask[0] = 1;  // processor 0 only

  rules::CpuAffinity a;
  a.enabled = true;
  a.mask = mask;
  CHECK(rules::apply_affinity(a, child.pid()));

  HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, child.pid());
  CHECK(h != nullptr);
  if (!h) return;
  ULONG_PTR got = 0;
  ULONG_PTR system = 0;
  CHECK(GetProcessAffinityMask(h, &got, &system));
  CHECK_EQ(got, static_cast<ULONG_PTR>(1));
  CloseHandle(h);
}
