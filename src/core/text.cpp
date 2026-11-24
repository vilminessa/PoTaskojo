#include "core/text.h"

#include <windows.h>

#include <cctype>

namespace core::text {
namespace {

std::string ascii_lower(std::string s) {
  for (auto& c : s)
    c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
  return s;
}

}  // namespace

std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(),
                                    static_cast<int>(w.size()), nullptr, 0,
                                    nullptr, nullptr);
  if (n <= 0) return {};
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                      s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                    static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(),
                      n);
  return w;
}

bool contains_ci(const std::string& value, const char* query) {
  if (!query || !*query) return true;
  return ascii_lower(value).find(ascii_lower(query)) != std::string::npos;
}

}  // namespace core::text
