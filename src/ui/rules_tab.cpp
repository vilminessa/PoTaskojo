#include "ui/rules_tab.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include <imgui.h>

#include "rules/rules.h"
#include "ui/scale.h"
#include "ui/widgets.h"

namespace ui {
namespace {

std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(),
                                    static_cast<int>(w.size()),
                                    nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                      s.data(), n, nullptr, nullptr);
  return s;
}

const char* prio_name(rules::PriorityAction p) {
  using P = rules::PriorityAction;
  switch (p) {
    case P::Unchanged: return "(unchanged)";
    case P::Idle: return "Idle";
    case P::BelowNormal: return "Below normal";
    case P::Normal: return "Normal";
    case P::AboveNormal: return "Above normal";
    case P::High: return "High";
    case P::Realtime: return "Realtime";
    case P::EfficiencyMode: return "Efficiency mode";
  }
  return "?";
}

const char* start_name(rules::StartAction a) {
  using S = rules::StartAction;
  switch (a) {
    case S::None: return "Do nothing";
    case S::EndIt: return "Terminate on launch";
    case S::Suspend: return "Suspend on launch";
  }
  return "?";
}

const char* wd_metric_name(rules::Watchdog::Metric m) {
  using M = rules::Watchdog::Metric;
  switch (m) {
    case M::CpuPercent: return "CPU %";
    case M::MemoryBytes: return "Memory";
    case M::DiskBytesPerSec: return "Disk B/s";
    case M::NetBytesPerSec: return "Net B/s";
  }
  return "?";
}

const char* wd_response_name(rules::Watchdog::Response r) {
  using R = rules::Watchdog::Response;
  switch (r) {
    case R::Notify: return "Alert";
    case R::DumpMini: return "Small dump";
    case R::DumpFull: return "Complete dump";
    case R::LowerPriority: return "Lower priority";
    case R::Suspend: return "Suspend";
    case R::End: return "End";
    case R::RunCommand: return "Launch program";
  }
  return "?";
}

char g_new_exe[264] = "";
std::string g_status;

}  // namespace

void render_rules_tab() {
  using namespace rules;

  const bool newer = RuleStore::instance().format_from_newer_version();
  const auto st = RuleEngine::instance().status();
  const bool paused = RuleEngine::instance().paused();

  // --- Header: pause toggle + status ---
  if (paused) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.3f, 1.0f));
    ImGui::TextUnformatted(st.paused_reason == 1
                               ? "PAUSED by safety breaker (rate limit: too many actions in one minute)"
                               : "PAUSED by user");
    ImGui::PopStyleColor();
    if (ImGui::SmallButton("Resume rules")) {
      RuleEngine::instance().set_paused(false);
      g_status = "Rules re-enabled";
    }
  } else {
    ImGui::TextDisabled("Rules active | applied %u | last: %s",
                        st.applied_count, st.last_event.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Suspend rules")) {
      RuleEngine::instance().set_paused(true);
      g_status = "Rules paused; affected programs restored to prior values.";
    }
  }

  if (newer) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
    ImGui::TextWrapped(
        "These rules were written by a newer version and cannot be edited here.");
    ImGui::PopStyleColor();
  }
  if (!g_status.empty()) {
    ImGui::TextDisabled("%s", g_status.c_str());
  }

  ImGui::Separator();

  // --- Create rule ---
  ImGui::SetNextItemWidth(dp(260.0f));
  ImGui::InputTextWithHint("##new_exe", "Program, e.g. chrome.exe", g_new_exe,
                           sizeof(g_new_exe));
  ImGui::SameLine();
  const bool can_add = *g_new_exe &&
                       RuleStore::instance().count() < kMaxRules &&
                       !RuleStore::instance().has(
                           std::wstring(g_new_exe, g_new_exe + strlen(g_new_exe)));
  if (!can_add) ImGui::BeginDisabled();
  if (ImGui::SmallButton("Create rule") && can_add) {
    Rule r;
    r.exe_name = std::wstring(g_new_exe, g_new_exe + strlen(g_new_exe));
    r.priority = PriorityAction::Normal;
    std::string err;
    if (RuleStore::instance().add(r, &err)) {
      RuleStore::instance().save();
      g_status = "Rule created: " + narrow(r.exe_name);
      g_new_exe[0] = 0;
    } else {
      g_status = err;
    }
  }
  if (!can_add) ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled("%zu / %zu rules", RuleStore::instance().count(), kMaxRules);

  ImGui::Separator();

  // --- Rule list ---
  const auto rules_list = RuleStore::instance().all();
  if (rules_list.empty()) {
    ImGui::TextDisabled("No rules defined.");
    Hint("Set how a matching program runs: priority, launch behaviour, or "
         "monitors.");
    return;
  }

  const ImGuiTableFlags flags =
      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable;
  if (ImGui::BeginTable("##rules", 5, flags)) {
    ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, dp(45.0f));
    ImGui::TableSetupColumn("Program", ImGuiTableColumnFlags_WidthFixed, dp(190.0f));
    ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, dp(130.0f));
    ImGui::TableSetupColumn("On start", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Monitors", ImGuiTableColumnFlags_WidthFixed, dp(90.0f));
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < rules_list.size(); ++i) {
      Rule r = rules_list[i];
      ImGui::PushID(static_cast<int>(i));
      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      bool en = r.enabled;
      if (ImGui::Checkbox("##en", &en)) {
        r.enabled = en;
        RuleStore::instance().update(r.exe_name, r);
        RuleStore::instance().save();
        g_status = en ? "Rule enabled" : "Rule disabled";
      }

      ImGui::TableNextColumn();
      ImGui::TextUnformatted(narrow(r.exe_name).c_str());

      ImGui::TableNextColumn();
      // Inline priority combo.
      ImGui::SetNextItemWidth(-1);
      std::string label = prio_name(r.priority);
      if (ImGui::BeginCombo("##prio", label.c_str())) {
        for (int p = 0; p <= 7; ++p) {
          const auto pa = static_cast<PriorityAction>(p);
          const bool sel = (r.priority == pa);
          if (ImGui::MenuItem(prio_name(pa), nullptr, sel)) {
            r.priority = pa;
            RuleStore::instance().update(r.exe_name, r);
            RuleStore::instance().save();
          }
        }
        ImGui::EndCombo();
      }

      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", start_name(r.start_action));

      ImGui::TableNextColumn();
      ImGui::TextDisabled("%zu", r.watchdogs.size());

      // Right-click: full editor dialog.
      if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Remove rule")) {
          RuleStore::instance().remove(r.exe_name);
          RuleStore::instance().save();
          g_status = narrow(r.exe_name) + ": rule removed";
          ImGui::EndPopup();
          ImGui::PopID();
          continue;
        }
        ImGui::EndPopup();
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  ImGui::Spacing();
  Hint("Rules apply only while PoTaskojo is running. Enable auto-start + tray "
       "so they keep working after sign-in.");
}

}  // namespace ui
