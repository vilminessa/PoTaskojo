// The charts' time axis and history window.
//
// Both of these were wrong once already: the axis assumed one stored entry was
// one second while the entries were actually added once per rendered frame, so
// a ten-minute window held about ten seconds and was labelled in minutes. The
// arithmetic is kept out of the ImGui file precisely so it can be pinned down
// here.

#include <string>
#include <vector>

#include "ui/chart_time.h"
#include "test_support.h"

using ui::chart_time::compute_time_ticks;
using ui::chart_time::history_cap;
using ui::chart_time::inside_window;
using ui::chart_time::kWindowMs;

TEST_CASE(chart_ticks_label_a_full_ten_minute_window) {
  // 599 seconds of history: the spacing that keeps six or fewer labels is
  // 120 s, so the axis reads in even minutes.
  const auto t = compute_time_ticks(599.0);
  CHECK_EQ(t.labels.size(), static_cast<size_t>(5));
  CHECK_EQ(t.labels[0], std::string("now"));
  CHECK_EQ(t.labels[1], std::string("-2m"));
  CHECK_EQ(t.labels[2], std::string("-4m"));
  CHECK_EQ(t.labels[3], std::string("-6m"));
  CHECK_EQ(t.labels[4], std::string("-8m"));
}

TEST_CASE(chart_ticks_fall_back_to_seconds_on_a_short_window) {
  // The spacing chosen is the smallest that still keeps six or fewer labels,
  // so thirty seconds of history is marked every ten seconds.
  const auto t = compute_time_ticks(30.0);
  CHECK_EQ(t.labels.size(), static_cast<size_t>(4));
  CHECK_EQ(t.labels[0], std::string("now"));
  CHECK_EQ(t.labels[1], std::string("-10s"));
  CHECK_EQ(t.labels[2], std::string("-20s"));
  CHECK_EQ(t.labels[3], std::string("-30s"));

  // Too short for a non-zero tick at all: only "now" survives.
  const auto tiny = compute_time_ticks(4.0);
  CHECK_EQ(tiny.labels.size(), static_cast<size_t>(1));
  CHECK_EQ(tiny.labels[0], std::string("now"));
}

TEST_CASE(chart_ticks_positions_run_backwards_to_the_span) {
  const auto t = compute_time_ticks(599.0);
  CHECK_EQ(t.pos.size(), t.labels.size());
  CHECK_EQ(t.pos[0], 0.0);
  CHECK_EQ(t.pos[1], -120.0);
  CHECK_EQ(t.pos.back(), -480.0);
  // Every position is inside the window and no label is orphaned from it.
  for (size_t i = 0; i < t.pos.size(); ++i) CHECK(t.pos[i] >= -599.0);
  // The pointers must be real, or ImPlot reads freed memory.
  CHECK_EQ(std::string(t.ptrs[0]), std::string("now"));
}

TEST_CASE(chart_ticks_survive_a_degenerate_span) {
  // Zero or negative must not produce an empty axis.
  CHECK_EQ(compute_time_ticks(0.0).labels.size(), static_cast<size_t>(1));
  CHECK_EQ(compute_time_ticks(-5.0).labels.size(), static_cast<size_t>(1));
}

TEST_CASE(chart_window_is_measured_by_age_not_by_count) {
  const long long now = 5'000'000LL;

  // Inside the window by a hair.
  CHECK(inside_window(now, now - kWindowMs));
  // Exactly on the boundary is still kept.
  CHECK(inside_window(now, now - kWindowMs + 1));
  // One millisecond past it is dropped.
  CHECK(!inside_window(now, now - kWindowMs - 1));
  // A brand-new sample is obviously inside.
  CHECK(inside_window(now, now));
}

TEST_CASE(history_cap_always_outruns_the_window) {
  // The count may never be what trims the history: at every plausible
  // interval the cap has to exceed the number of samples the window needs,
  // or raising the sampling rate silently shortens the window.
  for (long long interval : {1000LL, 500LL, 250LL, 100LL}) {
    const long long needed = kWindowMs / interval;
    const long long cap = history_cap(kWindowMs, interval);
    CHECK(cap >= needed);
    // ...but not unbounded: the spare half is the memory valve.
    CHECK(cap <= 2 * needed);
  }
  // Degenerate interval must not divide by zero.
  CHECK_EQ(history_cap(kWindowMs, 0), 0LL);
}
