// Settings are a thin wrapper over the Windows INI API, which is exactly the
// kind of code that looks obviously correct and still loses values: the API
// reads the file lazily, caches it, and picks the file encoding itself.

#include <string>

#include "core/settings.h"
#include "test_support.h"

namespace {
constexpr const wchar_t* kSection = L"UnitTests";
}  // namespace

TEST_CASE(settings_returns_the_default_when_the_key_is_missing) {
  CHECK_EQ(core::settings::get_int(kSection, L"AbsentInt", 42), 42);
  CHECK(core::settings::get_bool(kSection, L"AbsentYes", true));
  CHECK(!core::settings::get_bool(kSection, L"AbsentNo", false));
  CHECK_EQ(core::settings::get_string(kSection, L"AbsentStr", L"dflt"),
           std::wstring(L"dflt"));
}

TEST_CASE(settings_round_trips_integers) {
  core::settings::set_int(kSection, L"Positive", 4242);
  CHECK_EQ(core::settings::get_int(kSection, L"Positive", 0), 4242);
  core::settings::set_int(kSection, L"Negative", -7);
  CHECK_EQ(core::settings::get_int(kSection, L"Negative", 0), -7);
  core::settings::set_int(kSection, L"Zero", 0);
  // 0 must not be confused with "missing", so ask for a different default.
  CHECK_EQ(core::settings::get_int(kSection, L"Zero", 99), 0);
}

TEST_CASE(settings_round_trips_booleans) {
  core::settings::set_bool(kSection, L"On", true);
  CHECK(core::settings::get_bool(kSection, L"On", false));
  core::settings::set_bool(kSection, L"Off", false);
  CHECK(!core::settings::get_bool(kSection, L"Off", true));
}

TEST_CASE(settings_round_trips_strings) {
  const std::wstring plain = L"C:\\Program Files\\Some App\\app.exe";
  core::settings::set_string(kSection, L"Plain", plain);
  CHECK_EQ(core::settings::get_string(kSection, L"Plain", L""), plain);

  // Non-ASCII must survive: rule and window values are user-supplied text.
  const std::wstring wide = L"Значение с пробелами и ё";
  core::settings::set_string(kSection, L"Wide", wide);
  CHECK_EQ(core::settings::get_string(kSection, L"Wide", L""), wide);
}

TEST_CASE(settings_values_survive_a_flush) {
  core::settings::set_int(kSection, L"Flushed", 777);
  core::settings::save();
  CHECK_EQ(core::settings::get_int(kSection, L"Flushed", 0), 777);
}
