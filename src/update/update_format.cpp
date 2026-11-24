#include "update/update_format.h"

#include <cstdlib>
#include <sstream>
#include <utility>

namespace update {
namespace {

std::string trim(const std::string& s) {
  const auto begin = s.find_first_not_of(" \t\r");
  if (begin == std::string::npos) return {};
  const auto end = s.find_last_not_of(" \t\r");
  return s.substr(begin, end - begin + 1);
}

bool all_digits_or_dot(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s)
    if (!((c >= '0' && c <= '9') || c == '.')) return false;
  return true;
}

bool is_sha256_hex(const std::string& s) {
  if (s.size() != 64) return false;
  for (char c : s)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}

}  // namespace

bool parse_manifest(const std::string& text, Manifest& out) {
  std::istringstream in(text);
  std::string line;
  if (!std::getline(in, line)) return false;
  if (trim(line) != kDomain) return false;

  Manifest m;
  bool have_version = false;
  bool have_url = false;
  bool have_sha256 = false;
  while (std::getline(in, line)) {
    const std::string t = trim(line);
    if (t.empty()) continue;
    const auto sep = t.find(' ');
    if (sep == std::string::npos) return false;
    const std::string key = t.substr(0, sep);
    const std::string value = trim(t.substr(sep + 1));
    if (value.empty()) return false;
    if (key == "version") {
      m.version = value;
      have_version = true;
    } else if (key == "url") {
      m.url = value;
      have_url = true;
    } else if (key == "sha256") {
      m.sha256 = value;
      have_sha256 = true;
    } else {
      return false;
    }
  }

  if (!have_version || !have_url || !have_sha256) return false;
  if (!all_digits_or_dot(m.version)) return false;
  // Never follow a plain-http link: the signature protects the content, but
  // there is no reason to give up transport security as well.
  if (m.url.rfind("https://", 0) != 0) return false;
  if (!is_sha256_hex(m.sha256)) return false;

  out = std::move(m);
  return true;
}

bool is_newer(const std::string& current, const std::string& candidate) {
  std::istringstream a(current);
  std::istringstream b(candidate);
  std::string x;
  std::string y;
  for (;;) {
    const bool has_a = static_cast<bool>(std::getline(a, x, '.'));
    const bool has_b = static_cast<bool>(std::getline(b, y, '.'));
    if (!has_a && !has_b) return false;
    const int na = has_a ? std::atoi(x.c_str()) : 0;
    const int nb = has_b ? std::atoi(y.c_str()) : 0;
    if (nb != na) return nb > na;
  }
}

}  // namespace update
