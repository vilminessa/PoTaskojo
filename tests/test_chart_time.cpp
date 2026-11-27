// The charts' time axis and history window.
//
// Both of these were wrong once already: the axis assumed one stored entry was
// one second while the entries were actually added once per rendered frame, so
// a ten-minute window held about ten seconds and was labelled in minutes. The
// arithmetic is kept out of the ImGui file precisely so it can be pinned down
// here.

#include <cmath>
#include <string>
#include <vector>

#include "ui/chart_time.h"
#include "test_support.h"

using ui::chart_time::compute_time_ticks;
using ui::chart_time::history_cap;
using ui::chart_time::inside_window;
using ui::chart_time::kWindowMs;
using ui::chart_time::nice_ceiling;
using ui::chart_time::seconds_before;
using ui::chart_time::time_axis;

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

TEST_CASE(positions_are_measured_from_the_chosen_instant) {
  // The axis follows the clock so the plot glides instead of stepping once per
  // interval; the line still reaches the right-hand edge because the caller
  // appends a point holding the last value out to this instant.
  const long long instant = 9'000'000LL;
  CHECK_EQ(seconds_before(instant, instant), 0.0);
  CHECK_EQ(seconds_before(instant, instant - 250), -0.25);
  CHECK_EQ(seconds_before(instant, instant - 1000), -1.0);
  // Older samples sit strictly further left, never somewhere else.
  CHECK(seconds_before(instant, instant - 5000) <
        seconds_before(instant, instant - 1000));
}

TEST_CASE(y_ceiling_moves_only_when_the_data_crosses_a_rung) {
  // A new peak must not rescale the plot: peaks 12 % apart have to land on the
  // same rung, which is the whole point of quantising it.
  CHECK_EQ(nice_ceiling(102.6), nice_ceiling(115.0));

  // Every result sits on the ladder and clears what was asked for. The margin
  // is relative because rungs such as 1.2 are not exact in binary and can land
  // a hair below the demand - far below the 8 % headroom the caller adds, so a
  // line can still never be clipped.
  for (double raw : {0.5, 1.0, 1.05, 3.7, 42.0, 999.0, 1000.0, 12345.0, 0.0,
                     -4.0}) {
    const double c = nice_ceiling(raw);
    CHECK(c >= raw * (1.0 - 1e-9));
    const double magnitude = std::pow(10.0, std::floor(std::log10(c)));
    const double normalised = c / magnitude;
    CHECK(normalised <= 10.0 + 1e-6);
    // ...and it is a ladder value, not an arbitrary one.
    const double steps[] = {1.0, 1.2, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0};
    bool on_ladder = false;
    for (double s : steps)
      if (std::abs(normalised - s) < 1e-6) on_ladder = true;
    CHECK(on_ladder);
  }

  // Monotonic: a bigger demand never yields a smaller ceiling.
  double previous = 0.0;
  for (double raw = 1.0; raw < 5000.0; raw *= 1.15) {
    const double c = nice_ceiling(raw);
    CHECK(c >= previous);
    previous = c;
  }
}

TEST_CASE(the_plotted_axis_reaches_the_right_edge) {
  // The empty space this pins down: the line used to stop at the newest sample
  // while the axis ran on to "now", so a gap opened that only closed when the
  // next reading arrived. The held tail is what keeps the two together.
  const std::vector<long long> stamps = {1000, 1250, 1500, 1750};
  const auto x = time_axis(1800, stamps.data(), stamps.size());

  CHECK_EQ(x.size(), stamps.size() + 1);  // samples plus the held tail
  CHECK_EQ(x.back(), 0.0f);               // the tail sits on the edge
  // Samples run leftwards from it, older further left.
  CHECK(x[3] < 0.0f);
  CHECK(x[2] < x[3]);
  CHECK(x[1] < x[2]);
  CHECK(x[0] < x[1]);
}

TEST_CASE(the_axis_glides_between_samples) {
  // Two frames a hundred milliseconds apart with no new measurement in
  // between. Every interior point must have moved left - that is the smooth
  // motion - while the tail stays pinned to the edge, so the line neither
  // jumps nor leaves a gap.
  const std::vector<long long> stamps = {1000, 1250, 1500, 1750};
  const auto now = time_axis(1800, stamps.data(), stamps.size());
  const auto later = time_axis(1900, stamps.data(), stamps.size());

  CHECK_EQ(later.size(), now.size());
  for (size_t i = 0; i + 1 < now.size(); ++i) CHECK(later[i] < now[i]);
  CHECK_EQ(later.back(), 0.0f);
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
