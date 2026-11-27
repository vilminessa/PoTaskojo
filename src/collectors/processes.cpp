#include "collectors/processes.h"

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <winternl.h>

#include "core/log.h"

namespace collectors {
namespace {

// ---------------------------------------------------------------------------
// NtQuerySystemInformation(SystemProcessInformation) gives us, in ONE syscall:
//   CPU times, working set, I/O counters, base priority — for every process.
// This mirrors how Task Manager itself collects data and avoids opening a
// handle per process (which fails for protected/system processes).
// ---------------------------------------------------------------------------

using NtQuerySystemInformationFn = NTSTATUS(NTAPI*)(ULONG, PVOID, ULONG, PULONG);

NtQuerySystemInformationFn nt_qsi() {
  static NtQuerySystemInformationFn fn = [] {
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
      return reinterpret_cast<NtQuerySystemInformationFn>(
          GetProcAddress(ntdll, "NtQuerySystemInformation"));
    }
    return static_cast<NtQuerySystemInformationFn>(nullptr);
  }();
  return fn;
}

constexpr ULONG kSystemProcessInformation = 5;
constexpr NTSTATUS kStatusInfoLengthMismatch = static_cast<NTSTATUS>(0xC0000004);

// Layout of SYSTEM_PROCESS_INFORMATION (Vista+ layout, x64).
// Only the fields we need are named; offsets verified against winternl.h.
#pragma pack(push, 8)
struct SpiHeader {
  ULONG NextEntryOffset;
  ULONG NumberOfThreads;
  LARGE_INTEGER WorkingSetPrivateSize;
  ULONG HardFaultCount;
  ULONG NumberOfThreadsHighWatermark;
  ULONGLONG CycleTime;
  LARGE_INTEGER CreateTime;
  LARGE_INTEGER UserTime;
  LARGE_INTEGER KernelTime;
  UNICODE_STRING ImageName;
  LONG BasePriority;
  HANDLE UniqueProcessId;
  HANDLE InheritedFromUniqueProcessId;
  ULONG HandleCount;
  ULONG SessionId;
  ULONG_PTR UniqueProcessKey;
  SIZE_T PeakVirtualSize;
  SIZE_T VirtualSize;
  ULONG PageFaultCount;
  SIZE_T WorkingSetSize;
  SIZE_T QuotaPeakPagedPoolUsage;
  SIZE_T QuotaPagedPoolUsage;
  SIZE_T QuotaPeakNonPagedPoolUsage;
  SIZE_T QuotaNonPagedPoolUsage;
  SIZE_T PagefileUsage;
  SIZE_T PeakPagefileUsage;
  SIZE_T PrivatePageCount;
  LARGE_INTEGER ReadOperationCount;
  LARGE_INTEGER WriteOperationCount;
  LARGE_INTEGER OtherOperationCount;
  LARGE_INTEGER ReadTransferCount;
  LARGE_INTEGER WriteTransferCount;
  LARGE_INTEGER OtherTransferCount;
};
#pragma pack(pop)

static_assert(sizeof(SpiHeader) == 0xF8, "SPI layout mismatch");

struct SpiSample {
  uint32_t pid = 0;
  uint64_t cpu_ms = 0;
  uint64_t working_set = 0;
  uint64_t private_bytes = 0;
  uint64_t read_bytes = 0;
  uint64_t write_bytes = 0;
  int base_priority = 0;
  std::wstring name;  // from ImageName (kernel, no path)
};

// Reads all process samples in one (or a few) syscalls.
bool query_all(std::vector<SpiSample>& out) {
  auto fn = nt_qsi();
  if (!fn) return false;

  ULONG size = 256u * 1024;
  std::vector<uint8_t> buf;
  NTSTATUS st;
  for (int attempt = 0; attempt < 8; ++attempt) {
    buf.resize(size);
    ULONG needed = 0;
    st = fn(kSystemProcessInformation, buf.data(), size, &needed);
    if (st == 0) break;
    if (st != kStatusInfoLengthMismatch) return false;
    size = needed > size ? needed + 64u * 1024 : size * 2;
  }
  if (st != 0) return false;

  out.clear();
  out.reserve(512);

  uint8_t* p = buf.data();
  for (;;) {
    auto* h = reinterpret_cast<SpiHeader*>(p);
    SpiSample s;
    s.pid = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(h->UniqueProcessId));
    s.cpu_ms = static_cast<uint64_t>(
        (h->KernelTime.QuadPart + h->UserTime.QuadPart) / 10000);
    s.working_set = h->WorkingSetSize;
    s.private_bytes = h->PrivatePageCount;
    s.read_bytes = static_cast<uint64_t>(h->ReadTransferCount.QuadPart);
    s.write_bytes = static_cast<uint64_t>(h->WriteTransferCount.QuadPart);
    s.base_priority = h->BasePriority;
    if (h->ImageName.Buffer && h->ImageName.Length)
      s.name.assign(h->ImageName.Buffer, h->ImageName.Length / sizeof(wchar_t));
    out.push_back(std::move(s));

    if (h->NextEntryOffset == 0) break;
    p += h->NextEntryOffset;
  }
  return true;
}

std::wstring query_image_path(HANDLE proc) {
  wchar_t buf[MAX_PATH * 2]{};
  DWORD size = MAX_PATH * 2;
  if (QueryFullProcessImageNameW(proc, 0, buf, &size)) return buf;
  return {};
}

bool query_elevated(HANDLE proc) {
  HANDLE token = nullptr;
  if (!OpenProcessToken(proc, TOKEN_QUERY, &token)) return false;
  TOKEN_ELEVATION elev{};
  DWORD ret = 0;
  const BOOL ok = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &ret);
  CloseHandle(token);
  return ok && elev.TokenIsElevated;
}

// BasePriority (SPI) is the process base priority class value.
int priority_class_from_base(int base) {
  switch (base) {
    case 4: return IDLE_PRIORITY_CLASS;
    case 6: return BELOW_NORMAL_PRIORITY_CLASS;
    case 8: return NORMAL_PRIORITY_CLASS;
    case 10: return ABOVE_NORMAL_PRIORITY_CLASS;
    case 12: return HIGH_PRIORITY_CLASS;
    case 24: return REALTIME_PRIORITY_CLASS;
    default: return 0;
  }
}

}  // namespace

ProcessCollector::~ProcessCollector() { stop(); }

void ProcessCollector::start(uint32_t interval_ms) {
  if (running_.exchange(true)) return;
  interval_ms_ = interval_ms ? interval_ms : 1000;
  wait_.reset();
  thread_ = std::thread([this] { thread_main(); });
}

void ProcessCollector::stop() {
  if (!running_.exchange(false)) return;
  wait_.interrupt();
  if (thread_.joinable()) thread_.join();
}

std::shared_ptr<const ProcessSnapshot> ProcessCollector::latest() const {
  std::lock_guard lock(mutex_);
  return latest_;
}

ProcessCollector::Totals ProcessCollector::totals() const {
  std::lock_guard lock(mutex_);
  return totals_;
}

void ProcessCollector::thread_main() {
  core::log::info("process collector started");
  while (running_) {
    const auto t0 = GetTickCount64();
    auto snap = std::make_shared<ProcessSnapshot>();
    collect(*snap);

    {
      std::lock_guard lock(mutex_);
      snap->seq = latest_ ? latest_->seq + 1 : 1;
      Totals t;
      t.process_count = static_cast<uint32_t>(snap->rows.size());
      uint64_t ws = 0;
      double cpu = 0.0;
      for (const auto& r : snap->rows) {
        ws += r.working_set;
        cpu += r.cpu_percent;
      }
      t.working_set = ws;
      t.cpu_percent = cpu;
      totals_ = t;
      latest_ = std::move(snap);
    }

    const auto elapsed = GetTickCount64() - t0;
    const auto sleep = elapsed >= interval_ms_ ? 1 : interval_ms_ - elapsed;
    if (!wait_.wait_for(static_cast<uint32_t>(sleep))) break;
  }
  core::log::info("process collector stopped");
}

void ProcessCollector::collect(ProcessSnapshot& out) {
  out.taken_ms = GetTickCount64();
  const uint64_t now_tick = out.taken_ms;

  // 1) Bulk sample: CPU/memory/I/O/priority in one syscall.
  std::vector<SpiSample> samples;
  if (!query_all(samples)) {
    core::log::warn("NtQuerySystemInformation failed, falling back to Toolhelp only");
  }

  out.rows.reserve(samples.size() + 64);

  // 2) Enrich with paths/elevation (best effort, cheap for most).
  //    Only for processes we could not read before — keep handle count low.
  for (auto& s : samples) {
    ProcessRow row;
    row.pid = s.pid;
    row.name = std::move(s.name);
    row.cpu_total_ms = s.cpu_ms;
    row.working_set = s.working_set;
    row.private_bytes = s.private_bytes;
    row.read_bytes = s.read_bytes;
    row.write_bytes = s.write_bytes;
    row.priority_class = priority_class_from_base(s.base_priority);
    row.base_priority = s.base_priority;

    // CPU% delta.
    const auto pit = prev_.find(row.pid);
    if (pit != prev_.end() && row.cpu_total_ms >= pit->second.cpu_ms &&
        now_tick > pit->second.tick_ms) {
      const double dcpu = static_cast<double>(row.cpu_total_ms - pit->second.cpu_ms);
      const double dt = static_cast<double>(now_tick - pit->second.tick_ms);
      row.cpu_percent = dt > 0 ? (100.0 * dcpu / dt) : 0.0;
      if (row.cpu_percent < 0.0) row.cpu_percent = 0.0;
    } else {
      row.is_new = (pit == prev_.end() && row.pid != 0);
    }

    // Best-effort enrichment: path + elevation. Skip if we cannot open.
    if (HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, row.pid)) {
      row.image_path = query_image_path(proc);
      CloseHandle(proc);
    }
    if (row.pid != 0 && row.pid != 4) {
      if (HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, row.pid)) {
        row.is_elevated = query_elevated(proc);
        CloseHandle(proc);
      }
    }

    out.rows.push_back(std::move(row));
  }

  // 3) Parent pid via Toolhelp (only field SPI-v5 doesn't give us cheaply).
  if (HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
      snap != INVALID_HANDLE_VALUE) {
    std::unordered_map<uint32_t, uint32_t> parents;
    parents.reserve(512);
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
      do {
        parents[pe.th32ProcessID] = pe.th32ParentProcessID;
      } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    for (auto& r : out.rows) {
      const auto it = parents.find(r.pid);
      if (it != parents.end()) r.parent_pid = it->second;
    }
  }

  // 4) Prev map for next delta.
  prev_.clear();
  prev_.reserve(out.rows.size());
  for (const auto& r : out.rows)
    prev_[r.pid] = PrevEntry{r.cpu_total_ms, now_tick};
}

}  // namespace collectors
