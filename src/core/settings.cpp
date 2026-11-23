#include "core/settings.h"

#include <windows.h>

#include "core/paths.h"

namespace core::settings {
namespace {

std::wstring file_path() {
  static const std::wstring p = paths::settings_file().wstring();
  return p;
}

}  // namespace

bool get_bool(const wchar_t* section, const wchar_t* key, bool def) {
  return GetPrivateProfileIntW(section, key, def ? 1 : 0, file_path().c_str()) != 0;
}

int get_int(const wchar_t* section, const wchar_t* key, int def) {
  return static_cast<int>(
      GetPrivateProfileIntW(section, key, def, file_path().c_str()));
}

std::wstring get_string(const wchar_t* section, const wchar_t* key, const wchar_t* def) {
  wchar_t buf[1024];
  GetPrivateProfileStringW(section, key, def, buf, 1024, file_path().c_str());
  return buf;
}

void set_bool(const wchar_t* section, const wchar_t* key, bool value) {
  WritePrivateProfileStringW(section, key, value ? L"1" : L"0", file_path().c_str());
}

void set_int(const wchar_t* section, const wchar_t* key, int value) {
  wchar_t buf[32];
  swprintf(buf, 32, L"%d", value);
  WritePrivateProfileStringW(section, key, buf, file_path().c_str());
}

void set_string(const wchar_t* section, const wchar_t* key, const std::wstring& value) {
  WritePrivateProfileStringW(section, key, value.c_str(), file_path().c_str());
}

void load() {
  // INI is read lazily by GetPrivateProfile*; nothing to do eagerly.
}

void save() {
  // WritePrivateProfile* flushes immediately; kept for API symmetry / batching.
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, file_path().c_str());
}

}  // namespace core::settings
