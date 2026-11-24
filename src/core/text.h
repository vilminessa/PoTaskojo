#pragma once
// UTF-8 <-> wide string conversion for the app's own text.
//
// Windows hands us UTF-16 wherever an API takes wchar_t and expects bytes
// wherever a file, a log line or an ImGui label does. Both directions live
// here instead of being re-derived in every file that needs them.

#include <string>

namespace core::text {

// Empty input stays empty; the returned string is UTF-8.
std::string narrow(const std::wstring& w);
std::wstring widen(const std::string& s);

}  // namespace core::text
