#pragma once
namespace ui {
// Modal settings dialog (opened from the nav rail). Returns true when it
// wants to be kept open; call each frame while open.
void render_settings_dialog(bool* open);
}  // namespace ui
