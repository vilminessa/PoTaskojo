#pragma once
// When an automatic update check is worth running again.

#include <string>

namespace update::schedule {

// Interval between automatic attempts.
inline constexpr long long kIntervalSeconds = 24LL * 60 * 60;

// Pure gate, kept separate from the clock so it can be tested without waiting
// a day. A missing timestamp means "never checked"; a clock that ran backwards
// must not lock the check out until the difference is made up, because that
// would leave the program silently never checking again.
bool should_check(long long now_utc_seconds, long long last_utc_seconds);

// Seconds since the Unix epoch, from the system clock.
long long now_utc_seconds();

// Last attempt, persisted as decimal text. Text rather than a 32-bit integer
// because a raw epoch count stops fitting in 2038.
long long last_check_utc();
void mark_checked();

// Set when the process was started by a harness rather than by a person, so a
// scripted run cannot drop a dialog over a screenshot.
void set_scripted(bool scripted);
bool is_scripted();

}  // namespace update::schedule
