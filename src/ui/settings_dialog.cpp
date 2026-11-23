#include "ui/settings_dialog.h"

#include <windows.h>
#include <shellapi.h>

#include <imgui.h>

#include "core/settings.h"
#include "core/window_state.h"
#include "platform/autorun.h"
#include "platform/ifeo.h"
#include "ui/theme.h"

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

  ImGui::SetNextWindowSize(ImVec2(430, 0), ImGuiCond_Appearing);
  if (!ImGui::Begin("Settings", open,
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking)) {
    ImGui::End();
    return;
  }

  auto& ws = core::window_state();

  ImGui::TextDisabled("Window");
  ImGui::Separator();
  ImGui::Checkbox("Close to tray", &ws.close_to_tray);
  ImGui::Checkbox("Minimize to tray", &ws.minimize_to_tray);
  ImGui::Checkbox("Always on top", &ws.always_on_top);
  ImGui::Checkbox("Start minimized", &ws.start_minimized);

  ImGui::Spacing();
  ImGui::TextDisabled("Start with Windows");
  ImGui::Separator();
  bool autorun = platform::autorun_is_enabled();
  if (ImGui::Checkbox("Start PoTaskojo with Windows (in tray)", &autorun)) {
    platform::autorun_set(autorun);
    ws.start_with_windows = autorun;
    core::settings::set_bool(L"Startup", L"StartWithWindows", autorun);
  }
  ImGui::TextDisabled(
      "Rules and watchers keep working after you sign in.");

  ImGui::Spacing();
  ImGui::TextDisabled("Appearance");
  ImGui::Separator();
  static bool light = false;
  if (ImGui::Checkbox("Light theme", &light)) {
    apply_theme(light);
    core::settings::set_bool(L"Theme", L"LightTheme", light);
  }

  ImGui::Spacing();
  ImGui::TextDisabled("Task Manager replacement");
  ImGui::Separator();
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
    if (ImGui::Checkbox("Replace Task Manager (IFEO)", &ifeo)) {
      ifeo_error.clear();
      ifeo_set(ifeo, &ifeo_error);
      ifeo = ifeo_is_enabled();  // re-read (may have failed)
    }
    ImGui::EndDisabled();
    if (!is_user_admin())
      ImGui::TextDisabled("Requires PoTaskojo to run as administrator");
    else if (!ifeo_error.empty())
      ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s",
                         ifeo_error.c_str());
    ImGui::TextDisabled(
        "Windows+X → Task Manager will open PoTaskojo; the original stays "
        "reachable.");
  }

  ImGui::Spacing();
  ImGui::Separator();
  if (ImGui::Button("Close", ImVec2(120, 0))) *open = false;

  ImGui::End();
}

}  // namespace ui
