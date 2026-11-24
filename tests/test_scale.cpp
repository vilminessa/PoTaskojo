// Scaling has two independent inputs - the monitor DPI and the user's pinned
// percent - and only one of them may win. These cases pin that precedence and
// the accepted range, because a bad value here silently resizes every window.

#include "test_support.h"
#include "ui/scale.h"

TEST_CASE(scale_follows_the_monitor_dpi_when_auto) {
  ui::set_manual_percent(0);
  ui::set_dpi_scale(1.25f);
  CHECK_EQ(ui::ui_scale(), 1.25f);
  CHECK_EQ(ui::dpi_scale(), 1.25f);
  CHECK_EQ(ui::dp(100.0f), 125.0f);
}

TEST_CASE(scale_manual_override_wins_but_keeps_the_dpi_reading) {
  ui::set_dpi_scale(2.0f);
  ui::set_manual_percent(150);
  CHECK_EQ(ui::ui_scale(), 1.5f);
  // The DPI value itself is untouched, so the settings dialog can still show
  // what "Auto" would give on this monitor.
  CHECK_EQ(ui::dpi_scale(), 2.0f);
  CHECK_EQ(ui::manual_percent(), 150);
  CHECK_EQ(ui::dp(200.0f), 300.0f);

  ui::set_manual_percent(0);
  CHECK_EQ(ui::ui_scale(), 2.0f);
}

TEST_CASE(scale_rejects_percentages_outside_the_supported_range) {
  ui::set_manual_percent(30);   // below the floor
  CHECK_EQ(ui::manual_percent(), 0);
  ui::set_manual_percent(500);  // above the ceiling
  CHECK_EQ(ui::manual_percent(), 0);

  ui::set_manual_percent(50);
  CHECK_EQ(ui::manual_percent(), 50);
  ui::set_manual_percent(400);
  CHECK_EQ(ui::manual_percent(), 400);
  ui::set_manual_percent(0);
}

TEST_CASE(scale_rejects_nonsense_dpi_values) {
  ui::set_dpi_scale(0.0f);
  CHECK_EQ(ui::dpi_scale(), 1.0f);
  ui::set_dpi_scale(-3.0f);
  CHECK_EQ(ui::dpi_scale(), 1.0f);
  ui::set_dpi_scale(99.0f);  // far beyond any real display
  CHECK_EQ(ui::dpi_scale(), 1.0f);

  ui::set_dpi_scale(1.0f);
  ui::set_manual_percent(0);
}
