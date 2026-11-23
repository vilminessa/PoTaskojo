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
  const auto path = core::paths::get().logs_dir / L"crash.log";
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

  // Show a message like the original, then terminate.
  MessageBoxW(nullptr,
              L"PoTaskojo crashed. Check logs\\crash.log and the dumps folder "
              L"for details.",
              L"PoTaskojo", MB_OK | MB_ICONERROR);
  return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

bool write_minidump(const wchar_t* reason) {
  const auto dumps = core::paths::get().dumps_dir;
  CreateDirectoryW(dumps.c_str(), nullptr);

  std::wstring name = L"PoTaskojo_Crash_";
  name += timestamp_string();
  name += L"_Mini.dmp";
  const auto path = dumps / name;

  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;

  const BOOL ok = MiniDumpWriteDump(
      GetCurrentProcess(), GetCurrentProcessId(), h,
      MiniDumpWithIndirectlyReferencedMemory, nullptr, nullptr, nullptr);
  CloseHandle(h);

  if (!ok) {
    append_crash_log("MiniDumpWriteDump failed\n");
    return false;
  }

  char log[256];
  snprintf(log, sizeof(log), "minidump saved: %ls (%ls)\n",
           name.c_str(), reason ? reason : L"");
  append_crash_log(log);
  return true;
}

void install_crash_handler() {
  SetUnhandledExceptionFilter(unhandled_exception);
}

}  // namespace platform
