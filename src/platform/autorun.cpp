#include "platform/autorun.h"

#include <windows.h>

#include <string>

namespace platform {
namespace {

constexpr const wchar_t* kRunKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kApprovedKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
constexpr const wchar_t* kValueName = L"PoTaskojo";

std::wstring exe_path() {
  wchar_t buf[MAX_PATH]{};
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return buf;
}

// StartupApproved blob: byte0 0=enabled, 2=disabled-by-user.
bool approved_enabled() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kApprovedKey, 0, KEY_READ, &key) !=
      ERROR_SUCCESS)
    return true;  // no entry = default enabled
  unsigned char blob[12]{};
  DWORD size = sizeof(blob);
  DWORD type = 0;
  const LSTATUS st =
      RegQueryValueExW(key, kValueName, nullptr, &type, blob, &size);
  RegCloseKey(key);
  if (st != ERROR_SUCCESS || type != REG_BINARY || size < 1) return true;
  return blob[0] == 0;
}

}  // namespace

bool autorun_is_enabled() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) !=
      ERROR_SUCCESS)
    return false;
  const bool has_value =
      RegQueryValueExW(key, kValueName, nullptr, nullptr, nullptr, nullptr) ==
      ERROR_SUCCESS;
  RegCloseKey(key);
  return has_value && approved_enabled();
}

bool autorun_set(bool enable) {
  if (enable) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_WRITE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS)
      return false;
    // No extra flags: launch behaviour comes from the settings, not argv.
    const std::wstring cmd = L"\"" + exe_path() + L"\"";
    const LSTATUS st = RegSetValueExW(
        key, kValueName, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(cmd.c_str()),
        static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (st != ERROR_SUCCESS) return false;

    // Ensure StartupApproved marks it enabled.
    HKEY ap = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kApprovedKey, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &ap, nullptr) == ERROR_SUCCESS) {
      unsigned char blob[12]{};
      blob[0] = 0;
      blob[4] = 0x02;
      RegSetValueExW(ap, kValueName, 0, REG_BINARY, blob, sizeof(blob));
      RegCloseKey(ap);
    }
    return true;
  }

  // Disable: remove Run value (simplest, reversible).
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_WRITE, &key) !=
      ERROR_SUCCESS)
    return true;
  RegDeleteValueW(key, kValueName);
  RegCloseKey(key);

  HKEY ap = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kApprovedKey, 0, KEY_WRITE, &ap) ==
      ERROR_SUCCESS) {
    RegDeleteValueW(ap, kValueName);
    RegCloseKey(ap);
  }
  return true;
}

}  // namespace platform
