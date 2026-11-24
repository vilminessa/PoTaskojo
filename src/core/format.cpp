#include "core/format.h"

#include <cstdio>

namespace core::format {

std::string format_bytes(uint64_t b) {
  char buf[64];
  if (b < 1024ull) snprintf(buf, sizeof(buf), "%llu B", b);
  else if (b < 1024ull * 1024) snprintf(buf, sizeof(buf), "%.1f KB", b / 1024.0);
  else if (b < 1024ull * 1024 * 1024)
    snprintf(buf, sizeof(buf), "%.1f MB", b / (1024.0 * 1024));
  else
    snprintf(buf, sizeof(buf), "%.2f GB", b / (1024.0 * 1024 * 1024));
  return buf;
}

std::string format_rate(double bytes_per_second, bool bits) {
  char buf[64];
  const double v = bits ? bytes_per_second * 8.0 : bytes_per_second;
  const double base = bits ? 1000.0 : 1024.0;
  if (v < base) snprintf(buf, sizeof(buf), "%.0f %s", v, bits ? "b/s" : "B/s");
  else if (v < base * base)
    snprintf(buf, sizeof(buf), "%.1f %s", v / base, bits ? "kb/s" : "KB/s");
  else if (v < base * base * base)
    snprintf(buf, sizeof(buf), "%.1f %s", v / (base * base), bits ? "Mb/s" : "MB/s");
  else
    snprintf(buf, sizeof(buf), "%.2f %s", v / (base * base * base),
             bits ? "Gb/s" : "GB/s");
  return buf;
}

std::string format_pct(double p) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f%%", p);
  return buf;
}

std::string format_cpu(double pct) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f", pct);
  return buf;
}

}  // namespace core::format
