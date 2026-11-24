#pragma once
// Main application shell: Win32 window + ImGui frame + tab navigation.

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "app/renderer.h"

namespace app {

class App {
 public:
  static App& instance();

  int run(HINSTANCE instance, int cmd_show, bool open_settings = false);

  // Rebuilds the font atlas at the current ui::ui_scale() and re-applies the
  // scaled theme. Called at startup and from render_frame() when a scale change
  // is pending (never mid-frame: the font texture must not be replaced while
  // draw data referencing it exists).
  void apply_ui_scale();

  // Queues apply_ui_scale() for the start of the next frame. Safe to call from
  // inside the UI (settings dialog).
  void request_ui_scale_apply() { scale_dirty_ = true; }

  // Applies an explicit interface scale (0 = follow the display DPI), keeps the
  // window's logical size across the change and queues a font/theme rebuild.
  // Safe to call from inside the UI (settings dialog).
  void set_ui_scale_percent(int percent);

 private:
  App() = default;

  bool create_window(HINSTANCE instance, int cmd_show);
  bool init_imgui();
  void shutdown_imgui();
  bool load_font_bytes();          // caches TTF bytes for atlas rebuilds
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
  bool scale_dirty_ = false;
  ULONGLONG start_ticks_ = 0;
  std::vector<uint8_t> font_bytes_;  // cached TTF, rebuilt into atlas on scale
};

}  // namespace app
