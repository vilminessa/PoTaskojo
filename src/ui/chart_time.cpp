#include "ui/chart_time.h"

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

}  // namespace ui::chart_time
