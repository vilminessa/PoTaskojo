#include "platform/ifeo.h"

#include <windows.h>
#include <shellapi.h>

#include "core/log.h"

namespace platform {
namespace {

constexpr const wchar_t* kIfeoKey =
    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution "
    L"Options\\taskmgr.exe";
constexpr const wchar_t* kValueName = L"Debugger";

std::wstring our_exe_path() {
  wchar_t buf[MAX_PATH]{};
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return buf;
}

}  // namespace

std::wstring original_taskmgr_path() {
  wchar_t sys[MAX_PATH]{};
  GetSystemDirectoryW(sys, MAX_PATH);
  return std::wstring(sys) + L"\\taskmgr.exe";
}

bool ifeo_is_enabled() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kIfeoKey, 0, KEY_READ, &key) !=
      ERROR_SUCCESS)
    return false;
  wchar_t buf[MAX_PATH * 2]{};
  DWORD size = sizeof(buf) - sizeof(wchar_t);
  DWORD type = 0;
  const LSTATUS st = RegQueryValueExW(key, kValueName, nullptr, &type,
                                      reinterpret_cast<LPBYTE>(buf), &size);
  RegCloseKey(key);
  if (st != ERROR_SUCCESS || type != REG_SZ) return false;

  // Enabled only if the Debugger points at OUR executable.
  const std::wstring ours = our_exe_path();
  return _wcsicmp(buf, ours.c_str()) == 0;
}

bool ifeo_set(bool enable, std::string* error) {
  if (enable) {
    HKEY key = nullptr;
    LSTATUS st = RegCreateKeyExW(HKEY_LOCAL_MACHINE, kIfeoKey, 0, nullptr, 0,
                                 KEY_WRITE, nullptr, &key, nullptr);
    if (st != ERROR_SUCCESS) {
      if (error)
        *error = (st == ERROR_ACCESS_DENIED)
                     ? "Access denied (run PoTaskojo as administrator)"
                     : "Could not open IFEO registry key";
      return false;
    }
    const std::wstring ours = our_exe_path();
    st = RegSetValueExW(key, kValueName, 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(ours.c_str()),
                        static_cast<DWORD>((ours.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (st != ERROR_SUCCESS) {
      if (error)
        *error = (st == ERROR_ACCESS_DENIED)
                     ? "Access denied (run PoTaskojo as administrator)"
                     : "Could not write IFEO Debugger value";
      return false;
    }
    core::log::info("IFEO: Task Manager replacement enabled");
    return true;
  }

  // Disable: delete the value (keep the key — harmless).
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kIfeoKey, 0, KEY_WRITE, &key) !=
      ERROR_SUCCESS) {
    if (error) *error = "Could not open IFEO registry key";
    return false;
  }
  const LSTATUS st = RegDeleteValueW(key, kValueName);
  RegCloseKey(key);
  if (st != ERROR_SUCCESS && st != ERROR_FILE_NOT_FOUND) {
    if (error)
      *error = (st == ERROR_ACCESS_DENIED)
                   ? "Access denied (run PoTaskojo as administrator)"
                   : "Could not clear IFEO Debugger value";
    return false;
  }
  core::log::info("IFEO: Task Manager replacement disabled");
  return true;
}

bool launched_as_taskmgr_replacement(const wchar_t* cmd_line) {
  if (!cmd_line || !*cmd_line) return false;
  // IFEO passes the original image path as the first argument.
  return wcsstr(cmd_line, L"taskmgr.exe") != nullptr;
}

bool launch_original_task_manager() {
  const std::wstring path = original_taskmgr_path();
  SHELLEXECUTEINFOW sei{};
  sei.cbSize = sizeof(sei);
  sei.fMask = SEE_MASK_FLAG_NO_UI;
  sei.lpVerb = L"open";
  sei.lpFile = path.c_str();
  sei.nShow = SW_SHOWNORMAL;
  const BOOL ok = ShellExecuteExW(&sei);
  if (!ok) core::log::error("failed to launch original taskmgr");
  return ok != FALSE;
}

}  // namespace platform
