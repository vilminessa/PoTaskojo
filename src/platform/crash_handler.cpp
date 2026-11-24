#include "platform/crash_handler.h"

#include <windows.h>
#include <dbghelp.h>
#include <shlobj.h>

#include <cstdio>
#include <ctime>
#include <string>

#include "core/paths.h"

#pragma comment(lib, "dbghelp.lib")

namespace platform {
namespace {

std::wstring timestamp_string() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t buf[64];
  swprintf(buf, 64, L"%04u%02u%02u_%02u%02u%02u", st.wYear, st.wMonth, st.wDay,
           st.wHour, st.wMinute, st.wSecond);
  return buf;
}

void append_crash_log(const char* text) {
  const auto path = core::paths::get().logs_dir / L"crash-report.log";
  HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                         nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(h, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
  CloseHandle(h);
}

LONG WINAPI unhandled_exception(EXCEPTION_POINTERS* info) {
  // Best-effort: write crash log first (async-signal-ish safety: no heap).
  char buf[512];
  const DWORD code =
      info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
  snprintf(buf, sizeof(buf),
           "\r\n[%llu] CRASH: exception=0x%08lX\r\n",
           static_cast<unsigned long long>(GetTickCount64()), code);
  append_crash_log(buf);

  write_minidump(L"unhandled exception");

  // Surface the failure to the user, then terminate.
  MessageBoxW(nullptr,
              L"PoTaskojo stopped after an unexpected error. Details are in "
              L"logs\\crash-report.log and in the dumps directory.",
              L"PoTaskojo", MB_OK | MB_ICONERROR);
  return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

namespace {
// Shared by every dump path: pick the file, choose the dump kind, report.
bool write_dump_file(HANDLE process, DWORD pid, bool full,
                     const wchar_t* reason) {
  const auto dumps = core::paths::get().dumps_dir;
  CreateDirectoryW(dumps.c_str(), nullptr);

  std::wstring name = full ? L"PoTaskojo_Full_" : L"PoTaskojo_Fault_";
  name += timestamp_string();
  name += L".dmp";
  const auto path = dumps / name;

  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;

  const MINIDUMP_TYPE kind = full ? MiniDumpWithFullMemory
                                  : MiniDumpWithIndirectlyReferencedMemory;
  const BOOL ok =
      MiniDumpWriteDump(process, pid, h, kind, nullptr, nullptr, nullptr);
  CloseHandle(h);

  if (!ok) {
    append_crash_log("MiniDumpWriteDump failed\n");
    return false;
  }

  char log[256];
  snprintf(log, sizeof(log), "%s saved: %ls (%ls)\n",
           full ? "complete dump" : "minidump", name.c_str(),
           reason ? reason : L"");
  append_crash_log(log);
  return true;
}
}  // namespace

bool write_minidump(const wchar_t* reason) {
  return write_dump_file(GetCurrentProcess(), GetCurrentProcessId(), false,
                         reason);
}

bool write_process_dump(uint32_t pid, bool full, const wchar_t* reason) {
  if (!pid) return false;
  if (pid == GetCurrentProcessId())
    return write_dump_file(GetCurrentProcess(), pid, full, reason);

  // A watchdog response has to capture the program it fired on, and
  // MiniDumpWriteDump needs to read that process's address space.
  HANDLE proc =
      OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
  if (!proc) return false;
  const bool ok = write_dump_file(proc, pid, full, reason);
  CloseHandle(proc);
  return ok;
}

void install_crash_handler() {
  SetUnhandledExceptionFilter(unhandled_exception);
}

}  // namespace platform
