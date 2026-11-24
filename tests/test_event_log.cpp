// The journal is what the user reads when a monitor fires hours after the
// fact, so two properties matter: the newest entry is first, and the list stops
// growing when a monitor fires in a loop.

#include <string>

#include "rules/event_log.h"
#include "test_support.h"

namespace {

rules::RuleEvent entry(const char* time, const char* program,
                       const char* outcome) {
  rules::RuleEvent e;
  e.time = time;
  e.program = program;
  e.outcome = outcome;
  return e;
}

}  // namespace

TEST_CASE(event_log_starts_empty) {
  rules::EventLog log;
  CHECK_EQ(log.size(), static_cast<size_t>(0));
  CHECK(log.all().empty());
}

TEST_CASE(event_log_lists_the_newest_entry_first) {
  rules::EventLog log;
  log.add(entry("10:00:01", "a.exe", "suspended"));
  log.add(entry("10:00:02", "b.exe", "ended"));
  log.add(entry("10:00:03", "c.exe", "minidump written"));

  const auto all = log.all();
  CHECK_EQ(all.size(), static_cast<size_t>(3));
  if (all.size() != 3) return;
  CHECK_EQ(all[0].time, std::string("10:00:03"));
  CHECK_EQ(all[0].program, std::string("c.exe"));
  CHECK_EQ(all[1].program, std::string("b.exe"));
  CHECK_EQ(all[2].program, std::string("a.exe"));
}

TEST_CASE(event_log_drops_the_oldest_beyond_its_capacity) {
  rules::EventLog log(3);
  for (int i = 1; i <= 10; ++i)
    log.add(entry("10:00:00", ("p" + std::to_string(i) + ".exe").c_str(),
                  "fired"));

  CHECK_EQ(log.size(), static_cast<size_t>(3));
  const auto all = log.all();
  if (all.size() != 3) return;
  CHECK_EQ(all[0].program, std::string("p10.exe"));  // newest survived
  CHECK_EQ(all[2].program, std::string("p8.exe"));   // p1..p7 are gone
}

TEST_CASE(event_log_clear_empties_it) {
  rules::EventLog log(2);
  log.add(entry("10:00:01", "a.exe", "suspended"));
  CHECK_EQ(log.size(), static_cast<size_t>(1));
  log.clear();
  CHECK_EQ(log.size(), static_cast<size_t>(0));
}
