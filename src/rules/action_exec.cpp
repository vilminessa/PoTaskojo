#include "rules/action_exec.h"

namespace rules {
namespace {

// Resolve lazily: ntdll is always loaded, but the exports are not declared in
// the public SDK.
using NtSuspendResumeFn = LONG(NTAPI*)(HANDLE);

NtSuspendResumeFn nt_fn(const char* name) {
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (!ntdll) return nullptr;
  return reinterpret_cast<NtSuspendResumeFn>(GetProcAddress(ntdll, name));
}

// Opens the process with every right an action may need. A handle that cannot
// be obtained at all is reported as failure instead of being downgraded to a
// weaker one that the action then quietly fails to use.
HANDLE open_for_action(uint32_t pid) {
  if (!pid) return nullptr;
  return OpenProcess(kProcessActionAccess, FALSE, pid);
}

bool suspend_resume(uint32_t pid, const char* export_name) {
  HANDLE proc = open_for_action(pid);
  if (!proc) return false;
  auto fn = nt_fn(export_name);
  const LONG st = fn ? fn(proc) : static_cast<LONG>(0xC0000002L);  // STATUS_NOT_IMPLEMENTED
  CloseHandle(proc);
  return fn && st == 0;
}

}  // namespace

bool end_process(uint32_t pid) {
  HANDLE proc = open_for_action(pid);
  if (!proc) return false;
  const BOOL ok = TerminateProcess(proc, 0);
  CloseHandle(proc);
  return ok != FALSE;
}

bool suspend_process(uint32_t pid) { return suspend_resume(pid, "NtSuspendProcess"); }

bool resume_process(uint32_t pid) { return suspend_resume(pid, "NtResumeProcess"); }

}  // namespace rules
