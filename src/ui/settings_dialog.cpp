#include "ui/settings_dialog.h"

#include <windows.h>
#include <shellapi.h>

#include <imgui.h>

#include "core/settings.h"
#include "core/window_state.h"
#include "platform/autorun.h"
#include "platform/ifeo.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/scale.h"
#include "app/app.h"

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

}  // namespace

void render_settings_dialog(bool* open) {
  if (!*open) return;

  // Size the dialog to its content, capped by the work area. The body scrolls
  // under a sticky footer, so the cap only guards against tiny screens (Close
  // stays reachable at any scale).
  {
    const float max_h = ImGui::GetMainViewport()->WorkSize.y - dp(24.0f);
    float h = dp(600.0f);
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

  ImGui::EndChild();  // ##settings_body — footer below stays visible

  ImGui::Spacing();
  ImGui::Separator();
  if (ImGui::Button("Close", ImVec2(dp(120.0f), 0))) *open = false;

  ImGui::End();
}

}  // namespace ui
