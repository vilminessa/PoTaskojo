#pragma once
// Main application shell: Win32 window + ImGui frame + tab navigation.

#include <windows.h>

#include <string>

#include "app/renderer.h"

namespace app {

class App {
 public:
  static App& instance();

  int run(HINSTANCE instance, int cmd_show, bool open_settings = false);

 private:
  App() = default;

  bool create_window(HINSTANCE instance, int cmd_show);
  bool init_imgui();
  void shutdown_imgui();
  void render_frame();
  void render_nav();
  void render_tab_content();
  LRESULT wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

  static LRESULT CALLBACK wnd_proc_static(HWND, UINT, WPARAM, LPARAM);

  void show_from_tray();  // restore window from tray

  HWND hwnd_ = nullptr;
  HINSTANCE instance_ = nullptr;
  Renderer renderer_;
  bool imgui_ready_ = false;
  bool running_ = true;
  bool size_dirty_ = false;
  int pending_w_ = 0, pending_h_ = 0;
  int width_ = 0, height_ = 0;
  int active_tab_ = 0;
  bool settings_open_ = false;
  ULONGLONG start_ticks_ = 0;
};

}  // namespace app
