#include "core/window_state.h"

#include "core/settings.h"

namespace core {

WindowState& window_state() {
  static WindowState s = [] {
    WindowState w;
    settings::load();
    w.width = settings::get_int(L"Window", L"ClientWidth", w.width);
    w.height = settings::get_int(L"Window", L"ClientHeight", w.height);
    w.always_on_top = settings::get_bool(L"Window", L"KeepOnTop", w.always_on_top);
    w.close_to_tray = settings::get_bool(L"Window", L"HideOnClose", w.close_to_tray);
    w.minimize_to_tray = settings::get_bool(L"Window", L"HideOnMinimize", w.minimize_to_tray);
    w.start_minimized = settings::get_bool(L"Startup", L"LaunchMinimized", w.start_minimized);
    w.start_with_windows = settings::get_bool(L"Startup", L"AutoStartOnLogon",
                                              w.start_with_windows);
    w.default_tab = settings::get_int(L"Window", L"StartupTab", w.default_tab);
    // Window/UIScalePct is owned by ui::scale (0 = follow DPI).
    if (w.width < 640) w.width = 640;
    if (w.height < 480) w.height = 480;
    return w;
  }();
  return s;
}

void save_window_state() {
  const WindowState& w = window_state();
  settings::set_int(L"Window", L"ClientWidth", w.width);
  settings::set_int(L"Window", L"ClientHeight", w.height);
  settings::set_bool(L"Window", L"KeepOnTop", w.always_on_top);
  settings::set_bool(L"Window", L"HideOnClose", w.close_to_tray);
  settings::set_bool(L"Window", L"HideOnMinimize", w.minimize_to_tray);
  settings::set_bool(L"Startup", L"LaunchMinimized", w.start_minimized);
  settings::set_bool(L"Startup", L"AutoStartOnLogon", w.start_with_windows);
  settings::set_int(L"Window", L"StartupTab", w.default_tab);
  settings::save();
}

}  // namespace core
