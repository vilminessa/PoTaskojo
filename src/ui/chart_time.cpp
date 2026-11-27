#include "ui/chart_time.h"

#include <cmath>
#include <cstdio>

namespace ui::chart_time {

TimeTicks compute_time_ticks(double span_seconds) {
  TimeTicks out;
  // A span of zero would otherwise produce a single "now" tick and nothing to
  // judge the spacing from.
  if (!(span_seconds > 0.0)) span_seconds = 1.0;

  static const double kSteps[] = {10, 15, 30, 60, 120, 300, 600, 1800, 3600};
  const size_t kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);
  double step = kSteps[kStepCount - 1];
  for (size_t i = 0; i < kStepCount; ++i) {
    if (span_seconds / kSteps[i] <= 6.0) {
      step = kSteps[i];
      break;
    }
  }

  for (double t = 0.0; t >= -span_seconds - 1e-9; t -= step) {
    out.pos.push_back(t);
    char buffer[32];
    if (t >= -1e-9) {
      snprintf(buffer, sizeof(buffer), "now");
    } else {
      const int sec = static_cast<int>(-t + 0.5);
      if (sec % 60 == 0)
        snprintf(buffer, sizeof(buffer), "-%dm", sec / 60);
      else
        snprintf(buffer, sizeof(buffer), "-%ds", sec);
    }
    out.labels.push_back(buffer);
  }

  // c_str() only after every push, so growth cannot invalidate what is already
  // stored.
  out.ptrs.reserve(out.labels.size());
  for (const auto& s : out.labels) out.ptrs.push_back(s.c_str());
  return out;
}

double nice_ceiling(double raw) {
  if (!(raw > 0.0)) return 1.0;

  static const double kRungs[] = {1.0, 1.2, 1.5, 2.0, 2.5,
                                  3.0, 4.0, 5.0, 6.0, 8.0, 10.0};
  const size_t kRungCount = sizeof(kRungs) / sizeof(kRungs[0]);

  double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
  double normalised = raw / magnitude;
  // log10 can round onto the wrong side of a power of ten. Without this a
  // demand of exactly 1000 could read as 100 and land a decade too low.
  if (normalised < 1.0) {
    magnitude /= 10.0;
    normalised = raw / magnitude;
  } else if (normalised > 10.0) {
    magnitude *= 10.0;
    normalised = raw / magnitude;
  }

  for (size_t i = 0; i < kRungCount; ++i) {
    const double candidate = kRungs[i] * magnitude;
    // Relative tolerance: rungs such as 1.2 are not exact in binary, and a
    // candidate a hair below the demand would otherwise be passed over in
    // favour of the next rung up, jumping a whole step for no reason.
    if (candidate >= raw * (1.0 - 1e-9)) return candidate;
  }
  return 10.0 * magnitude;
}

std::vector<float> time_axis(long long instant_ms, const long long* sample_ms,
                             size_t count) {
  std::vector<float> x;
  x.reserve(count + 1);
  for (size_t i = 0; i < count; ++i)
    x.push_back(static_cast<float>(seconds_before(instant_ms, sample_ms[i])));
  // The held tail, pinned to the edge regardless of how long ago the last
  // sample was taken.
  x.push_back(0.0f);
  return x;
}

}  // namespace ui::chart_time
