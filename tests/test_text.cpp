// The rule editor takes UTF-8 labels from the UI and stores UTF-16 in the
// rules file, so a round trip that loses a character is data corruption the
// user only sees after saving and reloading.

#include <string>

#include "core/text.h"
#include "test_support.h"

TEST_CASE(text_round_trips_ascii) {
  const std::wstring in = L"C:\\Program Files\\App\\app.exe --flag";
  CHECK_EQ(core::text::widen(core::text::narrow(in)), in);
}

TEST_CASE(text_round_trips_non_ascii) {
  const std::wstring in = L"Значение с пробелами и ё — да";
  CHECK_EQ(core::text::narrow(in).size() > 0, true);
  CHECK_EQ(core::text::widen(core::text::narrow(in)), in);
}

TEST_CASE(text_keeps_empty_input_empty) {
  const std::wstring empty_w;
  const std::string empty_s;
  CHECK(core::text::narrow(empty_w).empty());
  CHECK(core::text::widen(empty_s).empty());
}
