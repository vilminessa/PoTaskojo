#include "ui/scale.h"

#include <algorithm>

#include "core/settings.h"

namespace ui {
namespace {

float g_dpi_scale = 1.0f;  // monitor DPI / 96
int g_manual_pct = 0;      // 0 = follow DPI

}  // namespace

float ui_scale() {
  if (g_manual_pct > 0) return g_manual_pct / 100.0f;
  return g_dpi_scale;
}

void set_dpi_scale(float dpi_scale) {
  if (dpi_scale <= 0.0f || dpi_scale > 8.0f) dpi_scale = 1.0f;
  g_dpi_scale = dpi_scale;
}

float dpi_scale() { return g_dpi_scale; }

void set_manual_percent(int percent) {
  if (percent != 0 && (percent < 50 || percent > 400)) percent = 0;
  g_manual_pct = percent;
  core::settings::set_int(L"Window", L"UIScalePct", percent);
}

int manual_percent() { return g_manual_pct; }

void load_from_settings() {
  g_manual_pct = core::settings::get_int(L"Window", L"UIScalePct", 0);
}

float dp(float v) { return v * ui_scale(); }

}  // namespace ui
