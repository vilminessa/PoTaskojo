#include "ui/settings_dialog.h"

#include <windows.h>
#include <shellapi.h>

#include <imgui.h>

#include <atomic>
#include <mutex>
#include <thread>

#include "core/settings.h"
#include "core/window_state.h"
#include "platform/autorun.h"
#include "platform/ifeo.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/scale.h"
#include "app/app.h"
#include "update/update_check.h"
#include "update/update_install.h"

namespace ui {
namespace {

bool is_user_admin() {
  BOOL admin = FALSE;
  SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
  PSID group = nullptr;
  if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
                               DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                               &group)) {
    CheckTokenMembership(nullptr, group, &admin);
    FreeSid(group);
  }
  return admin != FALSE;
}

// State of the update check.
//
// The work happens on a worker thread because a request can take seconds, and
// the frame loop must keep running. The object is deliberately never freed: the
// thread is detached, so releasing it during static destruction would race with
// a write from a thread that is still going.
enum class UpdatePhase {
  Idle,
  Checking,
  Ready,       // result filled in, dialog to be shown
  Downloading,
  Installed,   // staged; the frame loop quits and the shell restarts us
};

struct UpdateState {
  std::atomic<UpdatePhase> phase{UpdatePhase::Idle};
  std::mutex mutex;
  update::CheckResult result;
  std::string error;
  std::atomic<bool> show_dialog{false};
};

UpdateState& update_state() {
  static UpdateState* state = new UpdateState();
  return *state;
}

}  // namespace

void render_settings_dialog(bool* open) {
  // Runs before the open check: a staged update has to quit the program even
  // if the dialog was closed while the download was in flight.
  if (update_state().phase.load() == UpdatePhase::Installed) {
    update_state().phase.store(UpdatePhase::Idle);
    PostQuitMessage(0);
  }

  if (!*open) return;

  // Size the dialog to its content, capped by the work area. The body scrolls
  // under a sticky footer, so the cap only guards against tiny screens (Close
  // stays reachable at any scale).
  {
    const float max_h = ImGui::GetMainViewport()->WorkSize.y - dp(24.0f);
    float h = dp(680.0f);
    if (h > max_h) h = max_h;
    if (h < dp(240.0f)) h = dp(240.0f);
    ImGui::SetNextWindowSize(ImVec2(dp(430.0f), h), ImGuiCond_Appearing);
  }
  if (!ImGui::Begin("Settings", open,
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking)) {
    ImGui::End();
    return;
  }

  // Footer pattern: body scrolls, Close stays visible at any scale.
  const float footer_h = ImGui::GetFrameHeightWithSpacing();
  ImGui::BeginChild("##settings_body", ImVec2(0, -footer_h),
                    ImGuiChildFlags_None, ImGuiWindowFlags_None);

  auto& ws = core::window_state();

  SectionHeader("Window");
  ImGui::Checkbox("Close hides to tray", &ws.close_to_tray);
  ImGui::Checkbox("Minimize hides to tray", &ws.minimize_to_tray);
  ImGui::Checkbox("Always on top", &ws.always_on_top);
  ImGui::Checkbox("Launch minimized", &ws.start_minimized);

  ImGui::Spacing();
  SectionHeader("Auto-start");
  bool autorun = platform::autorun_is_enabled();
  if (ImGui::Checkbox("Start PoTaskojo at logon (in tray)", &autorun)) {
    platform::autorun_set(autorun);
    ws.start_with_windows = autorun;
    core::settings::set_bool(L"Startup", L"AutoStartOnLogon", autorun);
  }
  Hint("Rules and watchers keep running after sign-in.");

  ImGui::Spacing();
  SectionHeader("Appearance");
  static bool light = false;
  if (ImGui::Checkbox("Use light theme", &light)) {
    apply_theme(light);
    core::settings::set_bool(L"Theme", L"LightMode", light);
  }

  // Interface scale: follow the Windows DPI, or pin an explicit percent.
  {
    const int manual = ui::manual_percent();
    char preview[64];
    if (manual > 0) {
      snprintf(preview, sizeof(preview), "%d%%", manual);
    } else {
      const int pct = static_cast<int>(ui::dpi_scale() * 100.0f + 0.5f);
      snprintf(preview, sizeof(preview), "Auto (%d%%)", pct);
    }
    ImGui::TextDisabled("Interface scale");
    ImGui::SetNextItemWidth(dp(220.0f));
    if (ImGui::BeginCombo("##uiscale", preview)) {
      auto choose = [](int pct) {
        // Persists Window/UIScalePct, keeps the window's apparent size and
        // queues the font/theme rebuild for the next frame.
        app::App::instance().set_ui_scale_percent(pct);
      };
      if (ImGui::MenuItem("Auto (follow Windows)", nullptr, manual == 0))
        choose(0);
      for (int pct : {100, 125, 150, 175, 200}) {
        char lbl[16];
        snprintf(lbl, sizeof(lbl), "%d%%", pct);
        if (ImGui::MenuItem(lbl, nullptr, manual == pct)) choose(pct);
      }
      ImGui::EndCombo();
    }
    Hint("Auto follows the display scale of the current monitor.");
  }

  ImGui::Spacing();
  SectionHeader("Task Manager integration");
  {
    using namespace platform;
    static bool ifeo = ifeo_is_enabled();
    static std::string ifeo_error;
    static bool ifeo_checked = false;
    if (!ifeo_checked) {
      ifeo_checked = true;
      ifeo = ifeo_is_enabled();
    }
    ImGui::BeginDisabled(!is_user_admin());
    if (ImGui::Checkbox("Use PoTaskojo instead of Task Manager", &ifeo)) {
      ifeo_error.clear();
      ifeo_set(ifeo, &ifeo_error);
      ifeo = ifeo_is_enabled();  // re-read (may have failed)
    }
    ImGui::EndDisabled();
    if (!is_user_admin())
      ImGui::TextDisabled("Only available from an administrator session");
    else if (!ifeo_error.empty())
      ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s",
                         ifeo_error.c_str());
      Hint("Windows+X → Task Manager will open PoTaskojo; the original stays "
           "reachable.");
  }

  ImGui::Spacing();
  SectionHeader("About");
  ImGui::Text("PoTaskojo %s", POTASKOJO_VERSION);
  ImGui::TextDisabled("Required Notice: Copyright 2026 vilminessa");

  ImGui::Spacing();
  SectionHeader("Updates");
  {
    UpdateState& st = update_state();
    const UpdatePhase phase = st.phase.load();

    if (phase == UpdatePhase::Checking) {
      ImGui::TextDisabled("Checking for a newer build...");
    } else if (phase == UpdatePhase::Downloading) {
      ImGui::TextDisabled("Fetching the update...");
    } else if (phase == UpdatePhase::Ready || phase == UpdatePhase::Installed) {
      ImGui::TextDisabled("An update is ready.");
    } else if (ImGui::Button("Check for updates")) {
      {
        std::lock_guard<std::mutex> lock(st.mutex);
        st.error.clear();
      }
      st.phase.store(UpdatePhase::Checking);
      std::thread([] {
        update::CheckResult found = update::check_for_updates(POTASKOJO_VERSION);
        UpdateState& s = update_state();
        {
          std::lock_guard<std::mutex> lock(s.mutex);
          s.result = std::move(found);
        }
        s.show_dialog.store(true);
        s.phase.store(UpdatePhase::Ready);
      }).detach();
    }

    if (st.show_dialog.exchange(false)) ImGui::OpenPopup("Update");

    if (ImGui::BeginPopupModal("Update", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      // Read under the lock: the worker writes these, and the frame loop must
      // not observe a half-copied string.
      update::CheckResult found;
      std::string install_error;
      {
        std::lock_guard<std::mutex> lock(st.mutex);
        found = st.result;
        install_error = st.error;
      }

      const std::string& problem =
          !found.ok ? found.error : install_error;
      if (!problem.empty()) {
        ImGui::TextWrapped("%s", problem.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(dp(120.0f), 0))) {
          st.phase.store(UpdatePhase::Idle);
          ImGui::CloseCurrentPopup();
        }
      } else if (!found.newer) {
        ImGui::Text("PoTaskojo %s is the newest build published.",
                    POTASKOJO_VERSION);
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(dp(120.0f), 0))) {
          st.phase.store(UpdatePhase::Idle);
          ImGui::CloseCurrentPopup();
        }
      } else {
        ImGui::Text("PoTaskojo %s is available; you have %s.",
                    found.manifest.version.c_str(), POTASKOJO_VERSION);
        ImGui::Spacing();
        if (ImGui::Button("Download and restart", ImVec2(dp(180.0f), 0))) {
          const std::string url = found.manifest.url;
          const std::string hash = found.manifest.sha256;
          st.phase.store(UpdatePhase::Downloading);
          ImGui::CloseCurrentPopup();
          std::thread([url, hash] {
            const update::install::Result done =
                update::install::stage(url, hash);
            UpdateState& s = update_state();
            {
              std::lock_guard<std::mutex> lock(s.mutex);
              s.error = done.error;
            }
            s.phase.store(done.ok ? UpdatePhase::Installed
                                  : UpdatePhase::Ready);
            if (!done.ok) s.show_dialog.store(true);
          }).detach();
        }
        ImGui::SameLine();
        if (ImGui::Button("Not now", ImVec2(dp(120.0f), 0))) {
          st.phase.store(UpdatePhase::Idle);
          ImGui::CloseCurrentPopup();
        }
      }
      ImGui::EndPopup();
    }
  }

  ImGui::EndChild();  // ##settings_body — footer below stays visible

  ImGui::Spacing();
  ImGui::Separator();
  if (ImGui::Button("Close", ImVec2(dp(120.0f), 0))) *open = false;

  ImGui::End();
}

}  // namespace ui
