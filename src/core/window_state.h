#pragma once
// Window size / position / tray behaviour settings (own naming).

#include <cstdint>

namespace core {

struct WindowState {
  int width = 1280;
  int height = 800;
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
