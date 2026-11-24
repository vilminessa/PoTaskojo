#pragma once
// Number formatting shared by the tables and the charts, so the same value
// never renders two different ways inside one window.

#include <cstdint>
#include <string>

namespace core::format {

// Binary units: "0 B", "512 B", "1.0 KB", "12.5 MB", "4.00 GB".
std::string format_bytes(uint64_t b);

// Rate with the unit family matching the requested one: binary bytes
// ("KB/s") or decimal bits ("kb/s" - SI kilo, lowercase k).
std::string format_rate(double bytes_per_second, bool bits);

// "12.3%"
std::string format_pct(double p);

// "12.3" - the column header already carries the percent sign.
std::string format_cpu(double pct);

}  // namespace core::format
