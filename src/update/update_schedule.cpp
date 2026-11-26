#include "update/update_schedule.h"

#include <windows.h>

#include <atomic>
#include <cstdlib>

#include "core/settings.h"

namespace update::schedule {
namespace {

// Written once per process: the frame loop asks every frame, and the answer
// must not change underneath it.
std::atomic<bool> g_scripted{false};

}  // namespace

bool should_check(long long now_utc_seconds, long long last_utc_seconds) {
  if (last_utc_seconds <= 0) return true;   // never checked
  // Strictly less than: equal means the check just happened, while a value
  // from the future means the clock ran backwards. The second case must not
  // wait out the difference, or the program would never check again.
  if (now_utc_seconds < last_utc_seconds) return true;
  return now_utc_seconds - last_utc_seconds >= kIntervalSeconds;
}

long long now_utc_seconds() {
  FILETIME ft{};
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER u{};
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  // FILETIME counts from 1601; the Unix epoch is 11644473600 seconds later.
  constexpr ULONGLONG kEpochDiff = 11644473600ULL;
  return static_cast<long long>(u.QuadPart / 10000000ULL) -
         static_cast<long long>(kEpochDiff);
}

long long last_check_utc() {
  const std::wstring text =
      core::settings::get_string(L"Update", L"LastCheckUtc", L"");
  if (text.empty()) return 0;
  return _wtoi64(text.c_str());
}

void mark_checked() {
  const long long now = now_utc_seconds();
  core::settings::set_string(L"Update", L"LastCheckUtc",
                             std::to_wstring(now));
}

void set_scripted(bool scripted) { g_scripted.store(scripted); }

bool is_scripted() { return g_scripted.load(); }

}  // namespace update::schedule
