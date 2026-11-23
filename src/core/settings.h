#pragma once
// Typed settings over an INI file (GetPrivateProfile*W under the hood), with
// typed accessors and defaults. Keys mirror the *categories* we discovered but
// use our own naming.

#include <cstdint>
#include <string>

namespace core::settings {

void load();   // reads paths::settings_file()
void save();   // flushes dirty keys (debounced by caller if needed)

bool get_bool(const wchar_t* section, const wchar_t* key, bool def);
int get_int(const wchar_t* section, const wchar_t* key, int def);
std::wstring get_string(const wchar_t* section, const wchar_t* key, const wchar_t* def);

void set_bool(const wchar_t* section, const wchar_t* key, bool value);
void set_int(const wchar_t* section, const wchar_t* key, int value);
void set_string(const wchar_t* section, const wchar_t* key, const std::wstring& value);

}  // namespace core::settings
