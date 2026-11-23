#include "app/app.h"

#include <dwmapi.h>
#include <shellapi.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "core/log.h"
#include "core/paths.h"
#include "core/window_state.h"
#include "core/settings.h"
#include "platform/tray.h"
#include "rules/rules.h"
#include "ui/processes_tab.h"
#include "ui/settings_dialog.h"
#include "ui/tabs.h"
#include "ui/theme.h"

#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

namespace app {
namespace {
constexpr wchar_t kWindowClass[] = L"PoTaskojoMainWindow";
constexpr wchar_t kWindowTitle[] = L"PoTaskojo";
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
      }
      return 0;
    case WM_GETMINMAXINFO: {
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lparam);
      mmi->ptMinTrackSize.x = 640;
      mmi->ptMinTrackSize.y = 480;
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

bool App::create_window(HINSTANCE instance, int cmd_show) {  WNDCLASSEXW wc{};
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
  width_ = ws.width;
  height_ = ws.height;

  DWORD style = WS_OVERLAPPEDWINDOW;
  if (ws.always_on_top) style |= WS_POPUP;  // handled below too

  RECT r{0, 0, width_, height_};
  AdjustWindowRect(&r, style, FALSE);

  hwnd_ = CreateWindowExW(0, kWindowClass, kWindowTitle, style,
                          CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                          r.bottom - r.top, nullptr, nullptr, instance, this);
  if (!hwnd_) {
    core::log::error("CreateWindowExW failed");
    return false;
  }
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
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigWindowsMoveFromTitleBarOnly = true;

  // Load a Windows font with Cyrillic glyphs (default ImGui font is Latin-only).
  {
    const wchar_t* candidates[] = {
        L"C:\\Windows\\Fonts\\consola.ttf",   // Consolas (monospace)
        L"C:\\Windows\\Fonts\\segoeui.ttf",   // Segoe UI
        L"C:\\Windows\\Fonts\\arial.ttf",
    };
    bool loaded = false;
    for (const wchar_t* path : candidates) {
      if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) continue;
      // AddFontFromFileTTF() takes a narrow path; converting a wide path would
      // be lossy for non-ASCII users, so read the bytes ourselves and hand them
      // to ImGui (it takes ownership and frees with its allocator).
      HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (h == INVALID_HANDLE_VALUE) continue;
      LARGE_INTEGER li{};
      void* data = nullptr;
      int bytes = 0;
      if (GetFileSizeEx(h, &li) && li.QuadPart > 0 && li.QuadPart <= 64 * 1024 * 1024) {
        bytes = static_cast<int>(li.QuadPart);
        data = malloc(bytes);
        if (data) {
          DWORD got = 0;
          if (!ReadFile(h, data, static_cast<DWORD>(bytes), &got, nullptr) ||
              got != static_cast<DWORD>(bytes)) {
            free(data);
            data = nullptr;
          }
        }
      }
      CloseHandle(h);
      if (!data) continue;

      ImFontConfig cfg;
      cfg.OversampleH = 2;
      cfg.OversampleV = 1;
      cfg.PixelSnapH = true;
      io.Fonts->AddFontFromMemoryTTF(data, bytes, 15.0f, &cfg,
                                     io.Fonts->GetGlyphRangesCyrillic());
      loaded = true;
      core::log::info("font loaded");
      break;
    }
    if (!loaded) core::log::warn("no TTF font found, using ImGui default");
  }

  const auto& ws = core::window_state();
  io.FontGlobalScale = ws.ui_scale;

  ui::init_theme();  // dark/light from settings

  if (!ImGui_ImplWin32_Init(hwnd_)) return false;
  if (!ImGui_ImplDX11_Init(renderer_.device(), renderer_.context())) return false;
  imgui_ready_ = true;
  core::log::info("ImGui initialized (docking)");
  return true;
}

void App::shutdown_imgui() {
  if (!imgui_ready_) return;
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
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
  ImGui::BeginChild("##nav", ImVec2(180, 0), ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoScrollbar);
  ImGui::Dummy(ImVec2(8, 8));
  ImGui::TextDisabled("PoTaskojo");
  ImGui::Separator();
  for (int i = 0; i < static_cast<int>(ui::Tab::Count); ++i) {
    const bool selected = (active_tab_ == i);
    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.35f, 0.55f, 1.0f));
    if (ImGui::Button(ui::kTabNames[i].data(),
                      ImVec2(ImGui::GetContentRegionAvail().x, 32))) {
      active_tab_ = i;
    }
    if (selected) ImGui::PopStyleColor();
  }

  // Settings at the bottom of the rail.
  const float bottom_y = ImGui::GetWindowHeight() - 44.0f;
  ImGui::SetCursorPosY(bottom_y);
  if (ImGui::Button("Settings",
                    ImVec2(ImGui::GetContentRegionAvail().x, 32))) {
    settings_open_ = true;
  }
  ImGui::EndChild();
  ImGui::SameLine();

  // Content area.
  ImGui::BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoScrollbar);
  ImGui::Dummy(ImVec2(8, 8));
  render_tab_content();
  ImGui::EndChild();

  // Persist tab selection.
  if (active_tab_ != core::window_state().default_tab) {
    core::window_state().default_tab = active_tab_;
    core::settings::set_int(L"Window", L"DefaultTab", active_tab_);
  }

  ImGui::End();

  // Settings modal (rendered outside the shell window).
  ui::render_settings_dialog(&settings_open_);
}

void App::render_tab_content() {
  ui::render_tab(static_cast<ui::Tab>(active_tab_));
}

void App::render_frame() {
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
      std::vector<std::wstring> exes;
      std::vector<uint32_t> pids;
      exes.reserve(snap->rows.size());
      pids.reserve(snap->rows.size());
      for (const auto& r : snap->rows) {
        exes.push_back(r.name);
        pids.push_back(r.pid);
      }
      rules::RuleEngine::instance().on_process_snapshot(exes, pids);
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
