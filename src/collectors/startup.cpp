#include "collectors/startup.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>

#include "core/log.h"

namespace collectors {
namespace {

std::wstring reg_read(HKEY root, const wchar_t* sub, const wchar_t* name) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(root, sub, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
    return {};
  wchar_t buf[2048]{};
  DWORD size = sizeof(buf) - sizeof(wchar_t);
  DWORD type = 0;
  const LSTATUS st =
      RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size);
  RegCloseKey(key);
  if (st != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return {};
  return buf;
}

bool reg_exists(HKEY root, const wchar_t* sub, const wchar_t* name) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(root, sub, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
    return false;
  DWORD type = 0;
  const LSTATUS st = RegQueryValueExW(key, name, nullptr, &type, nullptr, nullptr);
  RegCloseKey(key);
  return st == ERROR_SUCCESS;
}

// StartupApproved\Run stores a 12-byte blob; byte 0 == 2/6 => disabled.
bool approved_enabled(HKEY root, const wchar_t* sub, const wchar_t* name) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(root, sub, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
    return true;  // no entry = default enabled
  unsigned char buf[16]{};
  DWORD size = sizeof(buf);
  DWORD type = 0;
  const LSTATUS st =
      RegQueryValueExW(key, name, nullptr, &type, buf, &size);
  RegCloseKey(key);
  if (st != ERROR_SUCCESS || type != REG_BINARY || size < 1) return true;
  // 0 = enabled, 2 = disabled by user, 6 = disabled by app/unknown
  return buf[0] == 0;
}

void set_approved(HKEY root, const wchar_t* sub, const wchar_t* name, bool enabled) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(root, sub, 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY,
                      nullptr, &key, nullptr) != ERROR_SUCCESS)
    return;
  unsigned char blob[12]{};
  blob[0] = enabled ? 0 : 2;
  blob[4] = 0x02;  // marker as Explorer writes
  RegSetValueExW(key, name, 0, REG_BINARY, blob, sizeof(blob));
  RegCloseKey(key);
}

void collect_run_key(HKEY root, const wchar_t* sub, const wchar_t* location,
                     std::vector<StartupRow>& out) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(root, sub, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
    return;
  wchar_t name[512];
  DWORD index = 0;
  while (true) {
    DWORD name_len = 512;
    const LSTATUS st = RegEnumValueW(key, index++, name, &name_len, nullptr, nullptr,
                                     nullptr, nullptr);
    if (st != ERROR_SUCCESS) break;
    StartupRow r;
    r.name = name;
    r.command = reg_read(root, sub, name);
    r.location = location;
    r.enabled = approved_enabled(root,
        (std::wstring(location) == L"HKCU Run"
             ? L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"
             : L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run32"),
        name);
    if (!r.command.empty()) out.push_back(std::move(r));
  }
  RegCloseKey(key);
}

void collect_folder(const KNOWNFOLDERID& folder_id, const wchar_t* location,
                    std::vector<StartupRow>& out) {
  PWSTR path = nullptr;
  if (FAILED(SHGetKnownFolderPath(folder_id, 0, nullptr, &path))) return;
  std::wstring dir(path);
  CoTaskMemFree(path);

  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN))
      continue;
    if (_wcsicmp(fd.cFileName, L"desktop.ini") == 0) continue;
    StartupRow r;
    r.name = fd.cFileName;
    r.command = dir + L"\\" + fd.cFileName;
    r.location = location;
    r.enabled = true;  // folder items have no StartupApproved gate (simplified)
    out.push_back(std::move(r));
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}

}  // namespace

StartupCollector::~StartupCollector() { stop(); }

void StartupCollector::start(uint32_t interval_ms) {
  if (running_.exchange(true)) return;
  interval_ms_ = interval_ms ? interval_ms : 10000;
  thread_ = std::thread([this] { thread_main(); });
}

void StartupCollector::stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
}

void StartupCollector::refresh_now() { refresh_requested_ = true; }

std::shared_ptr<const StartupSnapshot> StartupCollector::latest() const {
  std::lock_guard lock(mutex_);
  return latest_;
}

void StartupCollector::thread_main() {
  core::log::info("startup collector started");
  while (running_) {
    auto snap = std::make_shared<StartupSnapshot>();
    collect(*snap);
    {
      std::lock_guard lock(mutex_);
      snap->seq = latest_ ? latest_->seq + 1 : 1;
      latest_ = std::move(snap);
    }
    refresh_requested_ = false;
    for (uint32_t s = 0; s < interval_ms_ && running_; s += 100) {
      Sleep(100);
      if (refresh_requested_) break;
    }
  }
  core::log::info("startup collector stopped");
}

void StartupCollector::collect(StartupSnapshot& out) {
  collect_run_key(HKEY_CURRENT_USER,
                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"HKCU Run",
                  out.rows);
  collect_run_key(HKEY_LOCAL_MACHINE,
                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"HKLM Run",
                  out.rows);
  collect_run_key(HKEY_LOCAL_MACHINE,
                  L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run",
                  L"HKLM Run (32-bit)", out.rows);
  collect_folder(FOLDERID_Startup, L"Startup folder (User)", out.rows);
  collect_folder(FOLDERID_CommonStartup, L"Startup folder (Common)", out.rows);

  out.enabled_count = static_cast<size_t>(
      std::count_if(out.rows.begin(), out.rows.end(),
                    [](const StartupRow& r) { return r.enabled; }));
}

bool StartupCollector::set_enabled(const StartupRow& row, bool enabled) {
  // Folder items: no-op for MVP (deleting/creating .lnk is a later step).
  if (row.location.find(L"folder") != std::wstring::npos) return false;

  const bool hkcu = row.location == L"HKCU Run";
  HKEY root = hkcu ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
  std::wstring sub = hkcu
                         ? L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
                         : (row.location.find(L"32-bit") != std::wstring::npos
                                ? L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run"
                                : L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
  std::wstring approved_sub = hkcu
      ? L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"
      : L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run32";

  set_approved(root, approved_sub.c_str(), row.name.c_str(), enabled);
  return true;
}

bool StartupCollector::remove(const StartupRow& row) {
  if (row.location.find(L"folder") != std::wstring::npos) {
    // Delete the shortcut/file.
    return DeleteFileW(row.command.c_str()) != FALSE;
  }
  const bool hkcu = row.location == L"HKCU Run";
  HKEY root = hkcu ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
  std::wstring sub = hkcu
                         ? L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
                         : (row.location.find(L"32-bit") != std::wstring::npos
                                ? L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run"
                                : L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
  HKEY key = nullptr;
  if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_WRITE | KEY_WOW64_64KEY, &key) !=
      ERROR_SUCCESS)
    return false;
  const LSTATUS st = RegDeleteValueW(key, row.name.c_str());
  RegCloseKey(key);
  return st == ERROR_SUCCESS;
}

}  // namespace collectors
