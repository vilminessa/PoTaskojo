#include "app/app.h"

#include <dwmapi.h>
#include <shellapi.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <implot.h>

#include "core/log.h"
#include "core/paths.h"
#include "core/window_state.h"
#include "core/settings.h"
#include "platform/tray.h"
#include "rules/rules.h"
#include "ui/processes_tab.h"
#include "ui/scale.h"
#include "ui/settings_dialog.h"
#include "ui/tabs.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

namespace app {
namespace {
constexpr wchar_t kWindowClass[] = L"PoTaskojoMainWindow";
constexpr wchar_t kWindowTitle[] = L"PoTaskojo";

// Work area of the monitor the window lives on (falls back to the primary).
RECT work_area_for(HWND hwnd) {
  RECT wa{};
  if (hwnd) {
    if (HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)) {
      MONITORINFO mi{};
      mi.cbSize = sizeof(mi);
      if (GetMonitorInfoW(mon, &mi)) return mi.rcWork;
    }
  }
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  return wa;
}

// Window rect for a client of the given size (adds the non-client frame).
RECT window_rect_for_client(HWND hwnd, DWORD style, int w, int h) {
  UINT dpi = 96;
  if (hwnd) {
    using GetDpiFn = UINT(WINAPI*)(HWND);
    if (HMODULE u = GetModuleHandleW(L"user32.dll"))
      if (auto fn = reinterpret_cast<GetDpiFn>(GetProcAddress(u, "GetDpiForWindow")))
        dpi = fn(hwnd);
  }
  RECT r{0, 0, w, h};
  bool adjusted = false;
  if (HMODULE u = GetModuleHandleW(L"user32.dll")) {
    using AdjustDpiFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    if (auto fn = reinterpret_cast<AdjustDpiFn>(
            GetProcAddress(u, "AdjustWindowRectExForDpi")))
      adjusted = fn(&r, style, FALSE, 0, dpi) != FALSE;
  }
  if (!adjusted) AdjustWindowRectEx(&r, style, FALSE, 0);
  return r;
}

// Shrinks a client size so the whole window (frame included) fits |wa|.
// Clamping the client alone would push the frame off-screen.
void fit_client_to_work(HWND hwnd, DWORD style, int& w, int& h, const RECT& wa) {
  const RECT r = window_rect_for_client(hwnd, style, w, h);
  const int over_w = (r.right - r.left) - (wa.right - wa.left);
  const int over_h = (r.bottom - r.top) - (wa.bottom - wa.top);
  if (over_w > 0) w -= over_w;
  if (over_h > 0) h -= over_h;
}

// Smallest usable client: navigation rail + the process table's fixed columns
// + a readable Name column, never larger than the monitor's work area.
void min_window_size(HWND hwnd, LONG& out_w, LONG& out_h) {
  const RECT wa = work_area_for(hwnd);
  int w = static_cast<int>(ui::dp(960.0f));
  int h = static_cast<int>(ui::dp(480.0f));
  DWORD style = WS_OVERLAPPEDWINDOW;
  if (hwnd) style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
  fit_client_to_work(hwnd, style, w, h, wa);
  out_w = w;
  out_h = h;
}

// WM_GETMINMAXINFO tracks the window rect, so the floor has to include the
// frame as well.
void min_track_size(HWND hwnd, LONG& out_w, LONG& out_h) {
  LONG cw = 0, ch = 0;
  min_window_size(hwnd, cw, ch);
  DWORD style = WS_OVERLAPPEDWINDOW;
  if (hwnd) style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
  const RECT r = window_rect_for_client(hwnd, style, static_cast<int>(cw),
                                        static_cast<int>(ch));
  out_w = r.right - r.left;
  out_h = r.bottom - r.top;
}
}  // namespace

App& App::instance() {
  static App app;
  return app;
}

LRESULT CALLBACK App::wnd_proc_static(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return true;
  return GetWindowLongPtrW(h, GWLP_USERDATA)
             ? reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA))
                   ->wnd_proc(h, m, w, l)
             : DefWindowProcW(h, m, w, l);
}

LRESULT App::wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case platform::kTrayCallbackMsg: {
      // LOWORD(lparam) = mouse/event message (NIN_* / WM_*).
      const UINT ev = LOWORD(lparam);
      if (ev == WM_LBUTTONUP || ev == NIN_SELECT || ev == NIN_KEYSELECT) {
        show_from_tray();
      } else if (ev == WM_RBUTTONUP) {
        const UINT cmd = platform::tray_show_menu(hwnd);
        if (cmd == platform::TrayShow) show_from_tray();
        else if (cmd == platform::TrayExit) {
          core::window_state().close_to_tray = false;  // force real exit
          running_ = false;
          PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
      }
      return 0;
    }
    case WM_SIZE:
      if (wparam != SIZE_MINIMIZED) {
        size_dirty_ = true;
        pending_w_ = LOWORD(lparam);
        pending_h_ = HIWORD(lparam);
        // Remember the size in logical units so it survives a UI scale change.
        // Skip while maximized: that size must not become the normal size.
        if (!IsZoomed(hwnd)) {
          const float s = ui::ui_scale();
          auto& ws = core::window_state();
          ws.width = static_cast<int>(pending_w_ / s + 0.5f);
          ws.height = static_cast<int>(pending_h_ / s + 0.5f);
        }
      }
      return 0;
    case WM_GETMINMAXINFO: {
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lparam);
      LONG min_w = 0, min_h = 0;
      min_track_size(hwnd, min_w, min_h);
      mmi->ptMinTrackSize.x = min_w;
      mmi->ptMinTrackSize.y = min_h;
      return 0;
    }
    case WM_DPICHANGED: {
      // Moving to a monitor with a different DPI: adopt the rectangle Windows
      // suggests and apply the new scale at the start of the next frame (the
      // font texture must not be replaced mid-frame).
      ui::set_dpi_scale(static_cast<float>(HIWORD(wparam)) / 96.0f);
      scale_dirty_ = true;
      const auto* suggested = reinterpret_cast<const RECT*>(lparam);
      if (suggested) {
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      return 0;
    }
    case WM_CLOSE:
      if (core::window_state().close_to_tray) {
        // Hide instead of closing; rules/watchers keep running.
        ShowWindow(hwnd, SW_HIDE);
        return 0;
      }
      running_ = false;
      return 0;
    case WM_DESTROY:
      platform::tray_remove();
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void App::show_from_tray() {
  if (!hwnd_) return;
  ShowWindow(hwnd_, SW_SHOW);
  if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
  SetForegroundWindow(hwnd_);
}

bool App::create_window(HINSTANCE instance, int cmd_show) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_CLASSDC;
  wc.lpfnWndProc = &App::wnd_proc_static;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kWindowClass;
  if (!RegisterClassExW(&wc)) {
    core::log::error("RegisterClassExW failed");
    return false;
  }

  const auto& ws = core::window_state();

  // Initial scale from the system DPI; WM_DPICHANGED corrects it if the
  // window lands on a monitor with a different scale.
  UINT dpi = 96;
  if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
    using GetDpiFn = UINT(WINAPI*)();
    if (auto fn = reinterpret_cast<GetDpiFn>(GetProcAddress(user32, "GetDpiForSystem")))
      dpi = fn();
  }
  ui::set_dpi_scale(static_cast<float>(dpi) / 96.0f);
  ui::load_from_settings();  // explicit override, needed before sizing below

  // Window/Width and Window/Height are stored in logical (96-DPI) units, so the
  // window keeps its apparent size when the interface scale changes.
  width_ = static_cast<int>(ws.width * ui::ui_scale() + 0.5f);
  height_ = static_cast<int>(ws.height * ui::ui_scale() + 0.5f);

  LONG min_w = 0, min_h = 0;
  min_window_size(nullptr, min_w, min_h);  // never smaller than the layout needs
  if (width_ < min_w) width_ = min_w;
  if (height_ < min_h) height_ = min_h;

  DWORD style = WS_OVERLAPPEDWINDOW;
  if (ws.always_on_top) style |= WS_POPUP;  // handled below too

  // The whole window (frame included) has to fit the work area: a client as
  // wide as the display would push the window's edges off-screen.
  {
    const RECT wa = work_area_for(nullptr);
    fit_client_to_work(nullptr, style, width_, height_, wa);
    if (width_ < min_w) width_ = min_w;
    if (height_ < min_h) height_ = min_h;
  }

  RECT r{0, 0, width_, height_};
  using AdjustDpiFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
  static auto adjust_for_dpi = []() -> AdjustDpiFn {
    if (HMODULE u = GetModuleHandleW(L"user32.dll"))
      return reinterpret_cast<AdjustDpiFn>(
          GetProcAddress(u, "AdjustWindowRectExForDpi"));
    return nullptr;
  }();
  if (adjust_for_dpi)
    adjust_for_dpi(&r, style, FALSE, 0, dpi);  // dwExStyle = 0 (CreateWindowExW)
  else
    AdjustWindowRect(&r, style, FALSE);

  // Place the window inside the work area: CW_USEDEFAULT can leave a window
  // that is as tall as the work area hanging over the taskbar.
  int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
  {
    const RECT wa = work_area_for(nullptr);
    const int win_w = r.right - r.left;
    const int win_h = r.bottom - r.top;
    x = wa.left + ((wa.right - wa.left) - win_w) / 2;
    y = wa.top + ((wa.bottom - wa.top) - win_h) / 2;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;
  }

  hwnd_ = CreateWindowExW(0, kWindowClass, kWindowTitle, style, x, y,
                          r.right - r.left, r.bottom - r.top, nullptr, nullptr,
                          instance, this);
  if (!hwnd_) {
    core::log::error("CreateWindowExW failed");
    return false;
  }
  // Trust the real client size: the OS may have clamped our request.
  {
    RECT rc{};
    if (GetClientRect(hwnd_, &rc)) {
      width_ = rc.right;
      height_ = rc.bottom;
    }
  }
  core::log::info("window client = " + std::to_string(width_) + "x" +
                  std::to_string(height_) + " at scale " +
                  std::to_string(ui::ui_scale()));
  if (ws.always_on_top) SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                                     SWP_NOMOVE | SWP_NOSIZE);

  ShowWindow(hwnd_, ws.start_minimized ? SW_SHOWMINNOACTIVE : cmd_show);
  UpdateWindow(hwnd_);

  // Tray icon (close-to-tray keeps the app alive in background).
  platform::tray_add(hwnd_);
  return true;
}

bool App::init_imgui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();  // required before any BeginPlot()
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigWindowsMoveFromTitleBarOnly = true;

  if (!ImGui_ImplWin32_Init(hwnd_)) return false;

  // Scale: an explicit user override wins, otherwise follow the monitor.
  ui::load_from_settings();
  ui::set_dpi_scale(ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd_));

  if (!load_font_bytes()) core::log::warn("no TTF font found, using ImGui default");
  apply_ui_scale();

  if (!ImGui_ImplDX11_Init(renderer_.device(), renderer_.context())) return false;
  imgui_ready_ = true;
  core::log::info("ImGui initialized (docking)");
  return true;
}

bool App::load_font_bytes() {
  const wchar_t* candidates[] = {
      L"C:\\Windows\\Fonts\\consola.ttf",   // Consolas (monospace)
      L"C:\\Windows\\Fonts\\segoeui.ttf",   // Segoe UI
      L"C:\\Windows\\Fonts\\arial.ttf",
  };
  for (const wchar_t* path : candidates) {
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) continue;
    // AddFontFromFileTTF() takes a narrow path; a wide path would be lossy for
    // non-ASCII users, so cache the bytes and hand ImGui a copy it can own.
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) continue;
    LARGE_INTEGER li{};
    bool ok = false;
    if (GetFileSizeEx(h, &li) && li.QuadPart > 0 && li.QuadPart <= 64 * 1024 * 1024) {
      font_bytes_.assign(static_cast<size_t>(li.QuadPart), 0);
      DWORD got = 0;
      ok = ReadFile(h, font_bytes_.data(), static_cast<DWORD>(font_bytes_.size()),
                    &got, nullptr) &&
           got == font_bytes_.size();
    }
    CloseHandle(h);
    if (ok) {
      core::log::info("font cached (" + std::to_string(font_bytes_.size()) + " bytes)");
      return true;
    }
    font_bytes_.clear();
  }
  return false;
}

void App::apply_ui_scale() {
  ImGuiIO& io = ImGui::GetIO();
  const float s = ui::ui_scale();

  // Rebuild the font atlas at the new pixel size so glyphs stay crisp;
  // FontGlobalScale would only stretch the rasterized texture.
  const bool device_ready = imgui_ready_;
  if (device_ready) ImGui_ImplDX11_InvalidateDeviceObjects();
  io.Fonts->Clear();
  if (!font_bytes_.empty()) {
    // ImGui takes ownership and frees with its allocator -> hand it a copy.
    void* copy = malloc(font_bytes_.size());
    if (copy) {
      memcpy(copy, font_bytes_.data(), font_bytes_.size());
      ImFontConfig cfg;
      cfg.OversampleH = 2;
      cfg.OversampleV = 1;
      cfg.PixelSnapH = true;
      cfg.FontDataOwnedByAtlas = true;
      io.Fonts->AddFontFromMemoryTTF(copy, static_cast<int>(font_bytes_.size()),
                                     15.0f * s, &cfg,
                                     io.Fonts->GetGlyphRangesCyrillic());
    }
  }
  if (device_ready) ImGui_ImplDX11_CreateDeviceObjects();

  // Theme first (pristine), then ScaleAllSizes inside apply_theme().
  ui::init_theme();
  core::log::info("ui scale = " + std::to_string(s) +
                  " (dpi " + std::to_string(ui::dpi_scale()) + ")");
}

void App::set_ui_scale_percent(int percent) {
  // Capture the window's logical size at the scale currently in effect.
  RECT rc{};
  if (hwnd_) GetClientRect(hwnd_, &rc);
  const float old = ui::ui_scale();
  const float logical_w = (old > 0.0f) ? static_cast<float>(rc.right) / old : 0.0f;
  const float logical_h = (old > 0.0f) ? static_cast<float>(rc.bottom) / old : 0.0f;

  ui::set_manual_percent(percent);  // persists Window/UIScalePct

  if (hwnd_ && logical_w > 0.0f) {
    // Same apparent size at the new scale, floored at the layout minimum and
    // shrunk so the window (frame included) still fits the work area.
    const float s = ui::ui_scale();
    LONG min_w = 0, min_h = 0;
    min_window_size(hwnd_, min_w, min_h);
    int w = static_cast<int>(logical_w * s + 0.5f);
    int h = static_cast<int>(logical_h * s + 0.5f);
    if (w < min_w) w = min_w;
    if (h < min_h) h = min_h;
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
    fit_client_to_work(hwnd_, style, w, h, work_area_for(hwnd_));
    if (w < min_w) w = min_w;
    if (h < min_h) h = min_h;
    SetWindowPos(hwnd_, nullptr, 0, 0, w, h,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }

  scale_dirty_ = true;  // rebuild font + theme at the start of the next frame
}

void App::shutdown_imgui() {
  if (!imgui_ready_) return;
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImPlot::DestroyContext();   // must precede ImGui::DestroyContext
  ImGui::DestroyContext();
  imgui_ready_ = false;
}

void App::render_nav() {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking |
      ImGuiWindowFlags_NoSavedSettings;
  ImGui::Begin("##shell", nullptr, flags);
  ImGui::PopStyleVar(3);

  // Left navigation rail.
  ImGui::BeginChild("##nav", ImVec2(ui::dp(180.0f), 0), ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoScrollbar);
  ImGui::Dummy(ImVec2(ui::dp(8.0f), ui::dp(8.0f)));
  ImGui::TextDisabled("PoTaskojo");
  ImGui::Separator();
  const float nav_h = ui::dp(32.0f);
  for (int i = 0; i < static_cast<int>(ui::Tab::Count); ++i) {
    const bool selected = (active_tab_ == i);
    if (selected) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.35f, 0.55f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImVec4(0.26f, 0.42f, 0.64f, 1.0f));
    }
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    if (ImGui::Button(ui::kTabNames[i].data(),
                      ImVec2(ImGui::GetContentRegionAvail().x, nav_h))) {
      active_tab_ = i;
    }
    if (selected) {
      ImGui::PopStyleColor();
      ImGui::PopStyleColor();
      // Accent bar marking the active item.
      ImGui::GetWindowDrawList()->AddRectFilled(
          ImVec2(pos.x, pos.y),
          ImVec2(pos.x + ui::dp(3.0f), pos.y + nav_h),
          ImGui::GetColorU32(ui::section_color()));
    }
  }

  // Settings at the bottom of the rail.
  const float bottom_y = ImGui::GetWindowHeight() - ui::dp(44.0f);
  ImGui::SetCursorPosY(bottom_y);
  if (ImGui::Button("Settings",
                    ImVec2(ImGui::GetContentRegionAvail().x, nav_h))) {
    settings_open_ = true;
  }
  ImGui::EndChild();
  ImGui::SameLine();

  // Content area.
  ImGui::BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoScrollbar);
  ImGui::Dummy(ImVec2(ui::dp(8.0f), ui::dp(8.0f)));
  render_tab_content();
  ImGui::EndChild();

  // Persist tab selection.
  if (active_tab_ != core::window_state().default_tab) {
    core::window_state().default_tab = active_tab_;
    core::settings::set_int(L"Window", L"StartupTab", active_tab_);
  }

  ImGui::End();

  // Settings modal (rendered outside the shell window).
  ui::render_settings_dialog(&settings_open_);
}

void App::render_tab_content() {
  ui::render_tab(static_cast<ui::Tab>(active_tab_));
}

void App::render_frame() {
  // Apply a pending scale change before any draw data exists this frame.
  if (scale_dirty_) {
    scale_dirty_ = false;
    apply_ui_scale();
  }

  if (size_dirty_) {
    size_dirty_ = false;
    renderer_.resize(pending_w_, pending_h_);
    if (imgui_ready_) ImGui_ImplDX11_CreateDeviceObjects();
  }

  // Feed the rules engine every frame (independent of active tab).
  {
    static uint64_t last_seq = 0;
    auto snap = ui::process_collector().latest();
    if (snap && snap->seq != last_seq) {
      last_seq = snap->seq;
      std::vector<rules::ProcessInfo> procs;
      procs.reserve(snap->rows.size());
      for (const auto& r : snap->rows) {
        rules::ProcessInfo p;
        p.exe = r.name;
        p.pid = r.pid;
        p.cpu_percent = r.cpu_percent;
        p.memory_bytes = r.working_set;
        p.read_bytes = r.read_bytes;
        p.write_bytes = r.write_bytes;
        procs.push_back(std::move(p));
      }
      rules::RuleEngine::instance().on_process_snapshot(procs);
    }
  }

  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  render_nav();

  ImGui::Render();
  const float clear[4] = {0.06f, 0.07f, 0.09f, 1.0f};
  renderer_.begin_frame(clear[0], clear[1], clear[2], clear[3]);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  renderer_.end_frame();
}

int App::run(HINSTANCE instance, int cmd_show, bool open_settings) {
  instance_ = instance;
  start_ticks_ = GetTickCount64();
  settings_open_ = open_settings;

  if (!create_window(instance, cmd_show)) return 1;
  active_tab_ = core::window_state().default_tab;
  if (active_tab_ < 0 || active_tab_ >= static_cast<int>(ui::Tab::Count))
    active_tab_ = 0;
  core::log::info("default tab = " + std::to_string(active_tab_));
  {
    const auto p = core::paths::settings_file();
    core::log::info("settings file: " + p.string());
  }
  if (!renderer_.init(hwnd_, width_, height_)) return 1;
  if (!init_imgui()) return 1;

  const ULONGLONG startup_ms = GetTickCount64() - start_ticks_;
  core::log::info("first frame ready after " + std::to_string(startup_ms) + " ms");

  rules::RuleEngine::instance().start();

  MSG msg{};
  while (running_) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      if (msg.message == WM_QUIT) running_ = false;
    }
    if (!running_) break;
    render_frame();
  }

  core::save_window_state();
  rules::RuleEngine::instance().stop();
  shutdown_imgui();
  renderer_.shutdown();
  DestroyWindow(hwnd_);
  UnregisterClassW(kWindowClass, instance);
  core::log::info("app exited");
  core::log::shutdown();
  return static_cast<int>(msg.wParam);
}

}  // namespace app
