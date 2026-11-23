#include "collectors/performance.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <iphlpapi.h>

#include <cstdlib>

#include "core/log.h"

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace collectors {
namespace {

struct PdhState {
  PDH_HQUERY query = nullptr;
  PDH_HCOUNTER cpu_total = nullptr;
  PDH_HCOUNTER cpu_core = nullptr;      // wildcard \Processor(*)\% Processor Time
  PDH_HCOUNTER cpu_kernel = nullptr;    // \Processor(_Total)\% Privileged Time
  PDH_HCOUNTER mem_commit = nullptr;    // % Committed Bytes In Use
  PDH_HCOUNTER disk_bytes = nullptr;    // \PhysicalDisk(_Total)\Disk Bytes/sec
  PDH_HCOUNTER disk_read = nullptr;     // \PhysicalDisk(_Total)\Disk Read Bytes/sec
  PDH_HCOUNTER disk_write = nullptr;    // \PhysicalDisk(_Total)\Disk Write Bytes/sec
  PDH_HCOUNTER paging_pct = nullptr;    // \Paging File(_Total)\% Usage
  PDH_HCOUNTER net_send = nullptr;      // wildcard \Network Interface(*)\Bytes Sent/sec
  PDH_HCOUNTER net_recv = nullptr;      // wildcard \Network Interface(*)\Bytes Received/sec
  bool ok = false;
};

// Wildcard counters must be expanded into per-instance counters.
struct WildcardSet {
  std::vector<PDH_HCOUNTER> counters;
  bool valid = false;
};

bool fmt_double(PDH_HCOUNTER c, double& out) {
  PDH_FMT_COUNTERVALUE v{};
  const PDH_STATUS st = PdhGetFormattedCounterValue(c, PDH_FMT_DOUBLE, nullptr, &v);
  if (st == ERROR_SUCCESS &&
      (v.CStatus == ERROR_SUCCESS || v.CStatus == PDH_CSTATUS_VALID_DATA)) {
    out = v.doubleValue;
    return true;
  }
  return false;
}

// Sum of all instances of a wildcard counter (e.g. all NICs).
double sum_wildcard(PDH_HCOUNTER wild) {
  if (!wild) return 0.0;
  DWORD size = 0, count = 0;
  PDH_STATUS st =
      PdhGetFormattedCounterArrayW(wild, PDH_FMT_DOUBLE, &size, &count, nullptr);
  if (st != PDH_MORE_DATA && st != ERROR_SUCCESS) return 0.0;
  if (size == 0) return 0.0;
  std::vector<PDH_FMT_COUNTERVALUE_ITEM_W> items(size / sizeof(items[0]) + 4);
  DWORD size2 = static_cast<DWORD>(items.size() * sizeof(items[0]));
  DWORD count2 = 0;
  if (PdhGetFormattedCounterArrayW(wild, PDH_FMT_DOUBLE, &size2, &count2,
                                   items.data()) != ERROR_SUCCESS)
    return 0.0;
  double sum = 0.0;
  for (DWORD i = 0; i < count2; ++i)
    if (items[i].FmtValue.CStatus == ERROR_SUCCESS ||
        items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA)
      sum += items[i].FmtValue.doubleValue;
  return sum;
}

// Per-instance values of a wildcard counter (per-core CPU).
std::vector<double> per_instance(PDH_HCOUNTER wild) {
  std::vector<double> out;
  if (!wild) return out;
  DWORD size = 0, count = 0;
  PDH_STATUS st =
      PdhGetFormattedCounterArrayW(wild, PDH_FMT_DOUBLE, &size, &count, nullptr);
  if (st != PDH_MORE_DATA && st != ERROR_SUCCESS) return out;
  if (size == 0) return out;
  std::vector<PDH_FMT_COUNTERVALUE_ITEM_W> items(size / sizeof(items[0]) + 4);
  DWORD size2 = static_cast<DWORD>(items.size() * sizeof(items[0]));
  DWORD count2 = 0;
  if (PdhGetFormattedCounterArrayW(wild, PDH_FMT_DOUBLE, &size2, &count2,
                                   items.data()) != ERROR_SUCCESS)
    return out;
  out.reserve(count2);
  for (DWORD i = 0; i < count2; ++i) {
    double v = 0.0;
    if (items[i].FmtValue.CStatus == ERROR_SUCCESS ||
        items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA)
      v = items[i].FmtValue.doubleValue;
    out.push_back(v);
  }
  return out;
}

}  // namespace

PerfCollector::~PerfCollector() { stop(); }

void PerfCollector::start(uint32_t interval_ms) {
  if (running_.exchange(true)) return;
  interval_ms_ = interval_ms ? interval_ms : 1000;
  thread_ = std::thread([this] { thread_main(); });
}

void PerfCollector::stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
}

std::shared_ptr<const PerfSnapshot> PerfCollector::latest() const {
  std::lock_guard lock(mutex_);
  return latest_;
}

bool PerfCollector::init_pdh() {
  auto* s = new PdhState();
  if (PdhOpenQueryW(nullptr, 0, &s->query) != ERROR_SUCCESS) {
    delete s;
    core::log::warn("PDH: PdhOpenQuery failed");
    return false;
  }
  bool any = false;
  if (PdhAddEnglishCounterW(s->query, L"\\Processor(_Total)\\% Processor Time",
                            0, &s->cpu_total) == ERROR_SUCCESS) any = true;
  if (PdhAddEnglishCounterW(s->query, L"\\Processor(*)\\% Processor Time",
                            0, &s->cpu_core) == ERROR_SUCCESS) any = true;
  if (PdhAddEnglishCounterW(s->query, L"\\Processor(_Total)\\% Privileged Time",
                            0, &s->cpu_kernel) == ERROR_SUCCESS) any = true;
  if (PdhAddEnglishCounterW(s->query, L"\\Memory\\% Committed Bytes In Use",
                            0, &s->mem_commit) == ERROR_SUCCESS) any = true;
  if (PdhAddEnglishCounterW(s->query, L"\\PhysicalDisk(_Total)\\Disk Bytes/sec",
                            0, &s->disk_bytes) == ERROR_SUCCESS) any = true;
  if (PdhAddEnglishCounterW(s->query, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec",
                            0, &s->disk_read) == ERROR_SUCCESS) any = true;
  if (PdhAddEnglishCounterW(s->query, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec",
                            0, &s->disk_write) == ERROR_SUCCESS) any = true;
  if (PdhAddEnglishCounterW(s->query, L"\\Paging File(_Total)\\% Usage",
                            0, &s->paging_pct) == ERROR_SUCCESS) any = true;
  if (PdhAddEnglishCounterW(s->query, L"\\Network Interface(*)\\Bytes Sent/sec",
                            0, &s->net_send) == ERROR_SUCCESS) any = true;
  if (PdhAddEnglishCounterW(s->query, L"\\Network Interface(*)\\Bytes Received/sec",
                            0, &s->net_recv) == ERROR_SUCCESS) any = true;

  s->ok = any;
  pdh_state_ = s;
  PdhCollectQueryData(s->query);  // baseline (first delta needs two samples)
  core::log::info(any ? "PDH initialized" : "PDH: no counters available");
  return any;
}

void PerfCollector::close_pdh() {
  if (auto* s = static_cast<PdhState*>(pdh_state_)) {
    if (s->query) PdhCloseQuery(s->query);
    delete s;
    pdh_state_ = nullptr;
  }
  pdh_ready_ = false;
}

void PerfCollector::thread_main() {
  core::log::info("perf collector started");
  pdh_ready_ = init_pdh();

  // Give PDH one extra interval so the first CPU% delta is meaningful.
  Sleep(200);

  while (running_) {
    const auto t0 = GetTickCount64();
    auto snap = std::make_shared<PerfSnapshot>();
    sample(*snap);
    {
      std::lock_guard lock(mutex_);
      snap->seq = latest_ ? latest_->seq + 1 : 1;
      latest_ = std::move(snap);
    }
    const auto elapsed = GetTickCount64() - t0;
    const auto sleep = elapsed >= interval_ms_ ? 1 : interval_ms_ - elapsed;
    for (uint32_t s = 0; s < sleep && running_; s += 20) Sleep(20);
  }
  close_pdh();
  core::log::info("perf collector stopped");
}

void PerfCollector::sample(PerfSnapshot& out) {
  out.taken_ms = GetTickCount64();

  // --- CPU + disk + net + paging (PDH) ---
  auto* s = static_cast<PdhState*>(pdh_state_);
  double paging_pct = 0.0;
  if (s && s->ok) {
    PdhCollectQueryData(s->query);

    if (s->cpu_total) fmt_double(s->cpu_total, out.cpu.total_percent);
    if (s->cpu_kernel) fmt_double(s->cpu_kernel, out.cpu.kernel_percent);
    out.cpu.per_core = per_instance(s->cpu_core);

    if (s->disk_bytes) {
      double bps = 0.0;
      if (fmt_double(s->disk_bytes, bps))
        out.disk.bytes_per_sec = static_cast<uint64_t>(bps);  // combined r+w
    }
    if (s->disk_read) {
      double v = 0.0;
      if (fmt_double(s->disk_read, v))
        out.disk.read_bytes_s = static_cast<uint64_t>(v);
    }
    if (s->disk_write) {
      double v = 0.0;
      if (fmt_double(s->disk_write, v))
        out.disk.write_bytes_s = static_cast<uint64_t>(v);
    }
    if (s->net_send) out.net.bytes_sent_s = static_cast<uint64_t>(sum_wildcard(s->net_send));
    if (s->net_recv) out.net.bytes_recv_s = static_cast<uint64_t>(sum_wildcard(s->net_recv));
    if (s->paging_pct) fmt_double(s->paging_pct, paging_pct);
  }

  // --- Memory (sysinfo, no PDH) ---
  MEMORYSTATUSEX ms{};
  ms.dwLength = sizeof(ms);
  if (GlobalMemoryStatusEx(&ms)) {
    out.memory.total_bytes = ms.ullTotalPhys;
    out.memory.used_bytes = ms.ullTotalPhys - ms.ullAvailPhys;
    out.memory.commit_limit = ms.ullTotalPageFile;
    out.memory.committed_bytes =
        out.memory.commit_limit - ms.ullAvailPageFile;
    // Pagefile size = commit limit - physical RAM; usage from PDH "% Usage".
    out.memory.swap_total_bytes =
        ms.ullTotalPageFile > ms.ullTotalPhys
            ? ms.ullTotalPageFile - ms.ullTotalPhys
            : 0;
    if (paging_pct < 0.0) paging_pct = 0.0;
    if (paging_pct > 100.0) paging_pct = 100.0;
    out.memory.swap_used_bytes = static_cast<uint64_t>(
        paging_pct / 100.0 * static_cast<double>(out.memory.swap_total_bytes));
  }
}

}  // namespace collectors
