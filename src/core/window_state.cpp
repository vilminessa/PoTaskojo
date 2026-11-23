#include "core/window_state.h"

#include "core/settings.h"

namespace core {

WindowState& window_state() {
  static WindowState s = [] {
    WindowState w;
    settings::load();
    w.width = settings::get_int(L"Window", L"Width", w.width);
    w.height = settings::get_int(L"Window", L"Height", w.height);
    w.maximized = settings::get_bool(L"Window", L"Maximized", w.maximized);
    w.always_on_top = settings::get_bool(L"Window", L"AlwaysOnTop", w.always_on_top);
    w.close_to_tray = settings::get_bool(L"Window", L"HideOnClose", w.close_to_tray);
    w.minimize_to_tray = settings::get_bool(L"Window", L"HideOnMinimize", w.minimize_to_tray);
    w.start_minimized = settings::get_bool(L"Startup", L"LaunchMinimized", w.start_minimized);
    w.start_with_windows = settings::get_bool(L"Startup", L"AutoStartOnLogon",
                                              w.start_with_windows);
    w.default_tab = settings::get_int(L"Window", L"DefaultTab", w.default_tab);
    w.ui_scale = static_cast<float>(
        settings::get_int(L"Window", L"UIScalePct", 100)) / 100.0f;
    if (w.width < 640) w.width = 640;
    if (w.height < 480) w.height = 480;
    if (w.ui_scale < 0.5f || w.ui_scale > 3.0f) w.ui_scale = 1.0f;
    return w;
  }();
  return s;
}

void save_window_state() {
  const WindowState& w = window_state();
  settings::set_int(L"Window", L"Width", w.width);
  settings::set_int(L"Window", L"Height", w.height);
  settings::set_bool(L"Window", L"Maximized", w.maximized);
  settings::set_bool(L"Window", L"AlwaysOnTop", w.always_on_top);
  settings::set_bool(L"Window", L"HideOnClose", w.close_to_tray);
  settings::set_bool(L"Window", L"HideOnMinimize", w.minimize_to_tray);
  settings::set_bool(L"Startup", L"LaunchMinimized", w.start_minimized);
  settings::set_bool(L"Startup", L"AutoStartOnLogon", w.start_with_windows);
  settings::set_int(L"Window", L"DefaultTab", w.default_tab);
  settings::set_int(L"Window", L"UIScalePct", static_cast<int>(w.ui_scale * 100.0f));
  settings::save();
}

}  // namespace core
