#include "collectors/process_actions.h"

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace collectors {
namespace {

// NtSuspendProcess / NtResumeProcess: resolved dynamically (ntdll is not
// linked directly for these).
using NtOpFn = LONG(NTAPI*)(HANDLE);
NtOpFn nt_op(const char* name) {
  static NtOpFn suspend = nullptr, resume = nullptr;
  static bool init = false;
  if (!init) {
    init = true;
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
      suspend = reinterpret_cast<NtOpFn>(GetProcAddress(ntdll, "NtSuspendProcess"));
      resume = reinterpret_cast<NtOpFn>(GetProcAddress(ntdll, "NtResumeProcess"));
    }
  }
  return std::string(name) == "suspend" ? suspend : resume;
}

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

}  // namespace

bool is_protected_pid(uint32_t pid) {
  // 0 = System Idle, 4 = System; also refuse our own pid.
  if (pid == 0 || pid == 4) return true;
  if (pid == GetCurrentProcessId()) return true;
  // Session 0 kernel-ish processes: csrss/smss/lsass/wininit/winlogon/services
  // live in session 0 but user processes can be there too (services).
  // Use a name-based check at the call site; here only pid-level guards.
  return false;
}

ActionResult execute(const ProcessAction& action) {
  ActionResult r;

  if (action.kind != ProcessAction::CopyPath &&
      action.kind != ProcessAction::OpenLocation && is_protected_pid(action.pid)) {
    r.message = "Windows does not allow changes to this process";
    return r;
  }

  HANDLE proc = OpenProcess(
      PROCESS_TERMINATE | PROCESS_SUSPEND_RESUME | PROCESS_QUERY_LIMITED_INFORMATION,
      FALSE, action.pid);
  // SetPriorityClass needs PROCESS_SET_INFORMATION; retry with it.
  if (!proc)
    proc = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION,
                       FALSE, action.pid);

  switch (action.kind) {
    case ProcessAction::End: {
      if (!proc) { r.message = "Access denied"; return r; }
      if (TerminateProcess(proc, 0)) {
        r.ok = true;
        r.message = "Termination requested";
      } else {
        r.message = "Could not end process (protected or access denied)";
      }
      break;
    }
    case ProcessAction::Suspend:
    case ProcessAction::Resume: {
      if (!proc) { r.message = "Access denied"; return r; }
      auto fn = nt_op(action.kind == ProcessAction::Suspend ? "suspend" : "resume");
      if (!fn) { r.message = "NtSuspend/NtResume unavailable"; break; }
      const LONG st = fn(proc);
      if (st == 0) {
        r.ok = true;
        r.message = action.kind == ProcessAction::Suspend ? "Suspended" : "Resumed";
      } else {
        r.message = "Operation not allowed for this process";
      }
      break;
    }
    case ProcessAction::SetPriority: {
      if (!proc) { r.message = "Access denied (run as administrator?)"; return r; }
      if (SetPriorityClass(proc, static_cast<DWORD>(action.priority_class))) {
        r.ok = true;
        r.message = "Priority changed";
      } else {
        r.message = "Failed to change process priority";
      }
      break;
    }
    case ProcessAction::OpenLocation: {
      if (action.image_path.empty()) { r.message = "No file path"; return r; }
      std::wstring params = L"/select,\"" + action.image_path + L"\"";
      SHELLEXECUTEINFOW sei{};
      sei.cbSize = sizeof(sei);
      sei.fMask = SEE_MASK_FLAG_DDEWAIT;
      sei.lpVerb = L"open";
      sei.lpFile = L"explorer.exe";
      sei.lpParameters = params.c_str();
      sei.nShow = SW_SHOWNORMAL;
      r.ok = ShellExecuteExW(&sei) != FALSE;
      r.message = r.ok ? "Opened file location" : "Could not open location";
      break;
    }
    case ProcessAction::CopyPath: {
      if (action.image_path.empty()) { r.message = "No file path"; return r; }
      // Open clipboard with retry (clipboard can be busy).
      for (int i = 0; i < 10; ++i) {
        if (OpenClipboard(nullptr)) break;
        Sleep(20);
      }
      if (!OpenClipboard(nullptr)) { r.message = "Clipboard busy"; return r; }
      EmptyClipboard();
      const size_t bytes = (action.image_path.size() + 1) * sizeof(wchar_t);
      if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        void* p = GlobalLock(h);
        memcpy(p, action.image_path.c_str(), bytes);
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
        r.ok = true;
        r.message = "Copied the path to the clipboard";
      } else {
        r.message = "Out of memory";
      }
      CloseClipboard();
      break;
    }
  }

  if (proc) CloseHandle(proc);
  return r;
}

}  // namespace collectors
