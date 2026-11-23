#pragma once
namespace ui {

// Applies a built-in theme to ImGui. light=false → dark (default).
void apply_theme(bool light);

// Loads persisted theme choice from settings at startup.
void init_theme();

}  // namespace ui
