#pragma once
// Time axis and history window for the performance charts.
//
// Kept clear of ImGui and ImPlot so the arithmetic can be tested directly.
// These are the two things that lie silently when they are wrong: a tick
// spacing computed from the wrong notion of "one sample", and a window
// bounded by a count instead of by age.

#include <string>
#include <vector>

namespace ui::chart_time {

// How much history the charts keep: ten minutes of real time. Decided by the
// age of the samples, not by how many of them there are, so the window stays
// ten minutes whatever the collector's interval turns out to be.
inline constexpr long long kWindowMs = 10LL * 60 * 1000;

// A sample stays while it is no older than the window. The subtraction is safe
// because the timestamps come from GetTickCount64, which never runs backwards.
inline bool inside_window(long long now_ms, long long sample_ms) {
  return now_ms - sample_ms <= kWindowMs;
}

struct TimeTicks {
  std::vector<double> pos;        // axis positions, seconds relative to now
  std::vector<std::string> labels;
  std::vector<const char*> ptrs;  // c_str() of labels, filled last
};

// Picks a spacing that keeps the axis readable - at most six labels across the
// span - and labels each tick by how long ago it was. The rightmost tick is the
// present.
TimeTicks compute_time_ticks(double span_seconds);

}  // namespace ui::chart_time
