#pragma once
// Window size / position / tray behaviour settings (own naming).

#include <cstdint>

namespace core {

struct WindowState {
  // Client area, in logical units: multiplied by the UI scale on the way in so
  // the window keeps its apparent size when the scale changes.
  int width = 1280;
  int height = 800;
  // Window's outer top-left on the screen, also in logical units. -1 means
  // "never placed", which falls back to centring on the work area.
  int pos_x = -1;
  int pos_y = -1;
  bool always_on_top = false;
  bool close_to_tray = true;
  bool minimize_to_tray = false;
  bool start_minimized = false;
  bool start_with_windows = false;
  int default_tab = 0;
};

WindowState& window_state();  // global, loaded in app init
void save_window_state();     // persists current state to settings

}  // namespace core
