#pragma once
// Turning a rule's affinity into a mask the OS will accept.
//
// The two paths differ: an explicit mask comes from the rule, while "fast
// cores only" has to be derived from what the system reports. Both end up as a
// single-group KAFFINITY, so both live here rather than being open-coded in the
// engine.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rules/rules.h"

namespace rules {

// One core as the system reports it: its efficiency class and the logical
// processors it owns. Only group 0 is represented - see system_core_count().
struct CoreInfo {
  uint8_t efficiency_class = 0;
  uint64_t processors = 0;
};

// Logical processors of the fastest class. Microsoft documents EfficiencyClass
// as "a core with a higher value has intrinsically greater performance and
// less efficiency", and says it is only non-zero on heterogeneous systems - so
// on a machine with no performance/efficiency split every core reports the same
// class, every core is included, and applying the result changes nothing.
// Returns 0 when there is nothing to report.
uint64_t fast_core_mask(const std::vector<CoreInfo>& cores);

// A mask that may have been written on another machine: keeps the cores this
// one has and drops the rest, so a rule never pins a process to processors
// that do not exist. 0 when nothing is left to pin to.
uint64_t usable_mask(const uint8_t* bytes, size_t count, size_t core_count);

// Logical processors this build can address. SetProcessAffinityMask works
// within one processor group, so anything past 64 is out of reach and the
// count is capped there. Returns 1 if the query fails.
int system_core_count();

// Reads the system's cores. Empty when the query fails.
std::vector<CoreInfo> query_cores();

// Applies a rule's affinity to a live process. False when there is nothing to
// apply (disabled, no usable mask, process not openable) or the OS refused it.
bool apply_affinity(const CpuAffinity& affinity, uint32_t pid);

}  // namespace rules
