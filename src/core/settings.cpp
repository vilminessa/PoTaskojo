#include "core/settings.h"

#include <windows.h>

#include "core/paths.h"

namespace core::settings {
namespace {

std::wstring file_path() {
  static const std::wstring p = paths::settings_file().wstring();
  return p;
}

// The INI API writes in the ANSI code page unless the file already exists as
// Unicode, which silently replaces every character the active code page cannot
// represent with '?'. That is invisible on a machine whose code page happens to
// cover the text (Cyrillic under CP1251) and corrupts it everywhere else.
// Creating the file as UTF-16 before the first write makes the W functions use
// Unicode for its whole lifetime, on any locale. An already existing file is
// left alone: it can only contain what this build wrote, which is ASCII.
void ensure_unicode_ini() {
  static const bool once = [] {
    const std::wstring p = file_path();
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return true;  // already there
    const unsigned char bom[2] = {0xFF, 0xFE};
    DWORD written = 0;
    WriteFile(h, bom, sizeof(bom), &written, nullptr);
    CloseHandle(h);
    return true;
  }();
  (void)once;
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
  ensure_unicode_ini();
  WritePrivateProfileStringW(section, key, value ? L"1" : L"0", file_path().c_str());
}

void set_int(const wchar_t* section, const wchar_t* key, int value) {
  ensure_unicode_ini();
  wchar_t buf[32];
  swprintf(buf, 32, L"%d", value);
  WritePrivateProfileStringW(section, key, buf, file_path().c_str());
}

void set_string(const wchar_t* section, const wchar_t* key, const std::wstring& value) {
  ensure_unicode_ini();
  WritePrivateProfileStringW(section, key, value.c_str(), file_path().c_str());
}

void load() {
  // INI is read lazily by GetPrivateProfile*; the only eager part is making
  // sure the file exists in a form that survives every locale.
  ensure_unicode_ini();
}

void save() {
  // WritePrivateProfile* flushes immediately; kept for API symmetry / batching.
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, file_path().c_str());
}

}  // namespace core::settings
