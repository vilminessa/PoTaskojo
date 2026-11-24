// Formatting is what the user actually reads, so its unit boundaries are worth
// pinning down: a value that silently changes family (KB -> MB) is a bug that
// only shows up on one machine.

#include <string>

#include "core/format.h"
#include "test_support.h"

using core::format::format_bytes;
using core::format::format_cpu;
using core::format::format_pct;
using core::format::format_rate;

TEST_CASE(format_bytes_uses_binary_units) {
  CHECK_EQ(format_bytes(0), std::string("0 B"));
  CHECK_EQ(format_bytes(512), std::string("512 B"));
  CHECK_EQ(format_bytes(1023), std::string("1023 B"));
  CHECK_EQ(format_bytes(1024), std::string("1.0 KB"));
  CHECK_EQ(format_bytes(1536), std::string("1.5 KB"));
  CHECK_EQ(format_bytes(1048576ull), std::string("1.0 MB"));
  CHECK_EQ(format_bytes(1610612736ull), std::string("1.50 GB"));
  CHECK_EQ(format_bytes(5368709120ull), std::string("5.00 GB"));
}

TEST_CASE(format_rate_picks_units_from_the_value) {
  CHECK_EQ(format_rate(0.0, false), std::string("0 B/s"));
  CHECK_EQ(format_rate(512.0, false), std::string("512 B/s"));
  CHECK_EQ(format_rate(2048.0, false), std::string("2.0 KB/s"));
  CHECK_EQ(format_rate(5242880.0, false), std::string("5.0 MB/s"));
  CHECK_EQ(format_rate(3221225472.0, false), std::string("3.00 GB/s"));
}

TEST_CASE(format_rate_uses_decimal_units_for_bits) {
  // Bits are SI: base 1000 and a lowercase k, so 1600 b/s is "1.6 kb/s" and
  // not "1.6 KB/s".
  CHECK_EQ(format_rate(100.0, true), std::string("800 b/s"));
  CHECK_EQ(format_rate(200.0, true), std::string("1.6 kb/s"));
  CHECK_EQ(format_rate(200000.0, true), std::string("1.6 Mb/s"));
  CHECK_EQ(format_rate(200000000.0, true), std::string("1.60 Gb/s"));
}

TEST_CASE(format_pct_and_cpu_round_to_one_decimal) {
  CHECK_EQ(format_pct(12.34), std::string("12.3%"));
  CHECK_EQ(format_pct(0.0), std::string("0.0%"));
  CHECK_EQ(format_pct(100.0), std::string("100.0%"));
  // The column header already says "CPU %", so this one drops the sign.
  CHECK_EQ(format_cpu(12.34), std::string("12.3"));
  CHECK_EQ(format_cpu(0.0), std::string("0.0"));
}
