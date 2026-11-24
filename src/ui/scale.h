#pragma once
// Central UI scale state.
//
// scale = 1.0 means 100%. Effective scale is either the Windows DPI scale of
// the current monitor (auto) or an explicit user override in percent.
// All pixel constants in the UI go through dp() so they follow the scale.

namespace ui {

// Effective scale (>= 0.5, <= 4.0).
float ui_scale();

// Monitor DPI scale (dpi / 96). Updated on WM_DPICHANGED.
void set_dpi_scale(float dpi_scale);
float dpi_scale();

// Manual override: 0 = follow DPI, otherwise a percent (50..400).
// Persists to settings (Window/UIScalePct).
void set_manual_percent(int percent);
int manual_percent();

// Loads Window/UIScalePct (0 = auto) from settings at startup.
void load_from_settings();

// Design-pixel -> actual pixel.
float dp(float v);

}  // namespace ui
