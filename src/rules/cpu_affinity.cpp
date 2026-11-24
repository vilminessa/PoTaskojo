#include "rules/cpu_affinity.h"

#include <windows.h>

namespace rules {
namespace {

constexpr size_t kMaxAddressableCores = 64;

}  // namespace

uint64_t fast_core_mask(const std::vector<CoreInfo>& cores) {
  if (cores.empty()) return 0;
  uint8_t fastest = 0;
  for (const auto& c : cores)
    if (c.efficiency_class > fastest) fastest = c.efficiency_class;
  uint64_t mask = 0;
  for (const auto& c : cores)
    if (c.efficiency_class == fastest) mask |= c.processors;
  return mask;
}

uint64_t usable_mask(const uint8_t* bytes, size_t count, size_t core_count) {
  if (!bytes || count == 0 || core_count == 0) return 0;
  // Never shift by 64 or more: a hand-edited file can claim more cores than
  // exist, and a full group is a legitimate value, not an overflow.
  const size_t n =
      count < core_count ? count : core_count;
  const size_t limit = n < kMaxAddressableCores ? n : kMaxAddressableCores;
  uint64_t mask = 0;
  for (size_t i = 0; i < limit; ++i)
    if (bytes[i]) mask |= (1ull << i);
  return mask;
}

int system_core_count() {
  DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
  if (n == 0) return 1;
  if (n > kMaxAddressableCores) n = static_cast<DWORD>(kMaxAddressableCores);
  return static_cast<int>(n);
}

std::vector<CoreInfo> query_cores() {
  DWORD len = 0;
  GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || len == 0) return {};
  std::vector<uint8_t> buffer(len);
  auto* first = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
      buffer.data());
  if (!GetLogicalProcessorInformationEx(RelationProcessorCore, first, &len))
    return {};

  const uint8_t* const end = buffer.data() + len;
  std::vector<CoreInfo> out;
  for (const uint8_t* p = buffer.data(); p < end;) {
    const auto* info =
        reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(p);
    if (info->Size < sizeof(DWORD) || p + info->Size > end) break;
    if (info->Relationship == RelationProcessorCore) {
      const auto& rel = info->Processor;
      CoreInfo core;
      core.efficiency_class = rel.EfficiencyClass;
      for (WORD g = 0; g < rel.GroupCount; ++g) {
        if (rel.GroupMask[g].Group == 0)
          core.processors |= static_cast<uint64_t>(rel.GroupMask[g].Mask);
      }
      out.push_back(core);
    }
    p += info->Size;
  }
  return out;
}

bool apply_affinity(const CpuAffinity& affinity, uint32_t pid) {
  if (!affinity.enabled || pid == 0) return false;

  uint64_t mask = 0;
  if (affinity.performance_cores_only) {
    mask = fast_core_mask(query_cores());
  } else {
    mask = usable_mask(affinity.mask.data(), affinity.mask.size(),
                       static_cast<size_t>(system_core_count()));
  }
  if (mask == 0) return false;

  HANDLE proc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
  if (!proc) return false;
  const BOOL ok = SetProcessAffinityMask(proc, static_cast<ULONG_PTR>(mask));
  CloseHandle(proc);
  return ok != FALSE;
}

}  // namespace rules
