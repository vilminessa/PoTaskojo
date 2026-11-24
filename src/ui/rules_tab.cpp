#include "ui/rules_tab.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include <imgui.h>

#include "core/format.h"
#include "core/text.h"
#include "rules/cpu_affinity.h"
#include "rules/rules.h"
#include "ui/scale.h"
#include "ui/widgets.h"

namespace ui {
namespace {

using core::text::narrow;
using core::text::widen;

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

const char* wd_frequency_name(rules::Watchdog::Frequency f) {
  using F = rules::Watchdog::Frequency;
  switch (f) {
    case F::OncePerProcess: return "Once for this process";
    case F::EveryNMinutes: return "Every N minutes";
    case F::Immediately: return "Immediately";
  }
  return "?";
}

char g_new_exe[264] = "";
std::string g_status;

// --- Rule editor --------------------------------------------------------

// How many cores can be offered lives with the affinity module so the editor
// and the engine cannot disagree about the addressable range.

// Byte thresholds read better as whole numbers; a percentage keeps decimals.
const char* threshold_format(rules::Watchdog::Metric m) {
  return m == rules::Watchdog::Metric::CpuPercent ? "%.6g" : "%.0f";
}

// What the threshold means for the metric it is compared against: the same
// number reads as a percentage, a size or a rate.
void threshold_hint(rules::Watchdog::Metric m, double value) {
  if (m == rules::Watchdog::Metric::MemoryBytes) {
    const uint64_t bytes = value <= 0 ? 0 : static_cast<uint64_t>(value);
    ImGui::TextDisabled("(= %s)", core::format::format_bytes(bytes).c_str());
  } else if (m == rules::Watchdog::Metric::DiskBytesPerSec) {
    ImGui::TextDisabled("(= %s)",
                        core::format::format_rate(value, false).c_str());
  } else {
    ImGui::TextDisabled("(= %s)", core::format::format_pct(value).c_str());
  }
}

// The rule under edit is a copy: Cancel and the window X throw it away and
// Save is the only thing that reaches the store. The program name is the
// store's identity, so it is shown rather than edited - renaming a rule means
// removing one and creating another, which would need its own duplicate check.
std::wstring g_edit_key;
rules::Rule g_edit_rule;
std::string g_edit_error;
// Commands stay parallel to the monitors: ImGui edits narrow text while the
// model stores wide, and both lists are rebuilt together when a monitor is
// added or cut, so an index can never point at the wrong command.
using CommandBuf = std::array<char, 512>;
std::vector<CommandBuf> g_edit_cmds;
bool g_edit_open = false;
// Set by the internal screenshot flag; consumed on the next frame.
bool g_editor_pending = false;

void open_editor(const rules::Rule& r) {
  g_edit_key = r.exe_name;
  g_edit_rule = r;
  g_edit_error.clear();
  g_edit_cmds.clear();
  g_edit_cmds.resize(r.watchdogs.size());  // zero-filled
  for (size_t i = 0; i < r.watchdogs.size(); ++i) {
    const std::string cmd = narrow(r.watchdogs[i].command);
    CommandBuf& buf = g_edit_cmds[i];
    for (size_t k = 0; k < cmd.size() && k + 1 < buf.size(); ++k)
      buf[k] = cmd[k];
  }
  g_edit_open = true;
}

void render_editor() {
  using namespace rules;
  if (!g_edit_open) return;

  // Explicit height with a sticky footer, the same shape as the settings
  // dialog: Save and Cancel stay reachable however tall the rule grows.
  {
    const float max_h = ImGui::GetMainViewport()->WorkSize.y - dp(24.0f);
    float h = dp(620.0f);
    if (h > max_h) h = max_h;
    if (h < dp(240.0f)) h = dp(240.0f);
    ImGui::SetNextWindowSize(ImVec2(dp(560.0f), h), ImGuiCond_Appearing);
  }

  if (!ImGui::Begin("Change rule", &g_edit_open,
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking)) {
    ImGui::End();
    return;
  }

  const float footer_h = ImGui::GetFrameHeightWithSpacing();
  ImGui::BeginChild("##edit_body", ImVec2(0, -footer_h), ImGuiChildFlags_None,
                    ImGuiWindowFlags_None);

  if (!g_edit_error.empty())
    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s",
                       g_edit_error.c_str());

  ImGui::TextUnformatted(narrow(g_edit_key).c_str());
  ImGui::Checkbox("Rule is enabled", &g_edit_rule.enabled);

  SectionHeader("Behaviour");
  ImGui::TextDisabled("Priority");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(dp(240.0f));
  if (ImGui::BeginCombo("##priority", prio_name(g_edit_rule.priority))) {
    for (int p = 0; p < kPriorityActionCount; ++p) {
      const auto v = static_cast<PriorityAction>(p);
      if (ImGui::MenuItem(prio_name(v), nullptr, g_edit_rule.priority == v))
        g_edit_rule.priority = v;
    }
    ImGui::EndCombo();
  }
  ImGui::TextDisabled("On start");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(dp(240.0f));
  if (ImGui::BeginCombo("##start", start_name(g_edit_rule.start_action))) {
    for (int s = 0; s < kStartActionCount; ++s) {
      const auto v = static_cast<StartAction>(s);
      if (ImGui::MenuItem(start_name(v), nullptr, g_edit_rule.start_action == v))
        g_edit_rule.start_action = v;
    }
    ImGui::EndCombo();
  }

  SectionHeader("Processor affinity");
  ImGui::Checkbox("Restrict to a subset of cores",
                  &g_edit_rule.affinity.enabled);
  if (g_edit_rule.affinity.enabled) {
    const int cores = system_core_count();
    auto& mask = g_edit_rule.affinity.mask;
    if (mask.size() != static_cast<size_t>(cores)) {
      // A mask written on another machine keeps the cores both machines have
      // and defaults the ones that did not exist there to "on", so a rule never
      // arrives as "no cores selected".
      std::vector<uint8_t> next(static_cast<size_t>(cores), 1);
      const size_t keep = (std::min)(mask.size(), next.size());
      std::copy(mask.begin(),
                mask.begin() + static_cast<std::ptrdiff_t>(keep), next.begin());
      mask = std::move(next);
    }

    ImGui::Checkbox("Fast cores only",
                    &g_edit_rule.affinity.performance_cores_only);
    if (g_edit_rule.affinity.performance_cores_only) {
      Hint("Uses every core the system puts in the fast class; the rest are "
           "left out.");
    } else {
      ImGui::TextDisabled("Cores");
      for (int i = 0; i < cores; ++i) {
        if (i > 0) {
          if (i % 16 == 0) ImGui::NewLine();
          else ImGui::SameLine(0, dp(6.0f));
        }
        bool on = mask[static_cast<size_t>(i)] != 0;
        char label[12];
        snprintf(label, sizeof(label), "%d", i);
        if (ImGui::Checkbox(label, &on))
          mask[static_cast<size_t>(i)] = on ? 1 : 0;
      }
      if (std::none_of(mask.begin(), mask.end(), [](uint8_t b) { return b != 0; }))
        Hint("At least one core has to stay selected.");
    }
  }

  SectionHeader("Monitors");
  if (g_edit_rule.watchdogs.empty())
    Hint("None yet. A monitor watches one metric and reacts when it stays past "
         "a limit.");

  for (size_t i = 0; i < g_edit_rule.watchdogs.size(); ++i) {
    auto& w = g_edit_rule.watchdogs[i];
    ImGui::PushID(static_cast<int>(i));
    ImGui::Separator();

    ImGui::TextDisabled("Metric");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(dp(150.0f));
    if (ImGui::BeginCombo("##metric", wd_metric_name(w.metric))) {
      for (int m = 0; m < kMetricCount; ++m) {
        const auto v = static_cast<Watchdog::Metric>(m);
        if (ImGui::MenuItem(wd_metric_name(v), nullptr, w.metric == v))
          w.metric = v;
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Response");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(dp(150.0f));
    if (ImGui::BeginCombo("##response", wd_response_name(w.response))) {
      for (int r = 0; r < kResponseCount; ++r) {
        const auto v = static_cast<Watchdog::Response>(r);
        if (ImGui::MenuItem(wd_response_name(v), nullptr, w.response == v))
          w.response = v;
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine(0, dp(8.0f));
    if (ImGui::SmallButton("Remove")) {
      g_edit_rule.watchdogs.erase(g_edit_rule.watchdogs.begin() +
                                  static_cast<std::ptrdiff_t>(i));
      g_edit_cmds.erase(g_edit_cmds.begin() + static_cast<std::ptrdiff_t>(i));
      ImGui::PopID();
      break;
    }

    ImGui::TextDisabled("Above");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(dp(120.0f));
    ImGui::InputDouble("##threshold", &w.threshold, 0.0, 0.0,
                       threshold_format(w.metric));
    ImGui::SameLine();
    threshold_hint(w.metric, w.threshold);
    ImGui::SameLine();
    ImGui::TextDisabled("for");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(dp(70.0f));
    ImGui::InputScalar("##sustain", ImGuiDataType_U32, &w.sustain_seconds);
    ImGui::SameLine();
    ImGui::TextDisabled("seconds");

    ImGui::TextDisabled("Repeat");
    ImGui::SameLine();
    // Wide enough for "Once for this process", the longest of the three labels.
    ImGui::SetNextItemWidth(dp(210.0f));
    if (ImGui::BeginCombo("##freq", wd_frequency_name(w.frequency))) {
      for (int f = 0; f < kFrequencyCount; ++f) {
        const auto v = static_cast<Watchdog::Frequency>(f);
        if (ImGui::MenuItem(wd_frequency_name(v), nullptr, w.frequency == v))
          w.frequency = v;
      }
      ImGui::EndCombo();
    }
    if (w.frequency == Watchdog::Frequency::EveryNMinutes) {
      ImGui::SameLine();
      ImGui::TextDisabled("every");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(dp(70.0f));
      ImGui::InputScalar("##every", ImGuiDataType_U32, &w.every_n_minutes);
      ImGui::SameLine();
      ImGui::TextDisabled("minutes");
    }

    if (w.response == Watchdog::Response::RunCommand) {
      ImGui::TextDisabled("Command");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(-1);
      if (i < g_edit_cmds.size() &&
          ImGui::InputText("##cmd", g_edit_cmds[i].data(), g_edit_cmds[i].size()))
        w.command = widen(g_edit_cmds[i].data());
    }

    ImGui::PopID();
  }

  if (g_edit_rule.watchdogs.size() < kMaxWatchdogsPerRule) {
    if (ImGui::SmallButton("Add monitor")) {
      g_edit_rule.watchdogs.push_back(Watchdog{});
      g_edit_cmds.push_back({});
    }
  } else {
    ImGui::TextDisabled("A rule can hold %zu monitors.", kMaxWatchdogsPerRule);
  }

  ImGui::EndChild();

  ImGui::Spacing();
  ImGui::Separator();
  if (ImGui::Button("Save", ImVec2(dp(120.0f), 0))) {
    for (size_t i = 0; i < g_edit_rule.watchdogs.size(); ++i) {
      if (i < g_edit_cmds.size())
        g_edit_rule.watchdogs[i].command = widen(g_edit_cmds[i].data());
    }
    std::string err;
    if (RuleStore::instance().update(g_edit_key, g_edit_rule, &err)) {
      RuleStore::instance().save();
      g_status = narrow(g_edit_key) + ": rule saved";
      g_edit_open = false;
    } else {
      g_edit_error = err;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(dp(120.0f), 0))) g_edit_open = false;
  ImGui::SameLine(0, dp(40.0f));
  if (ImGui::Button("Remove", ImVec2(dp(120.0f), 0))) {
    RuleStore::instance().remove(g_edit_key);
    RuleStore::instance().save();
    g_status = narrow(g_edit_key) + ": rule removed";
    g_edit_open = false;
  }

  ImGui::End();
}

}  // namespace

void render_rules_tab() {
  using namespace rules;

  // Internal flag: jump straight to the first rule's editor. Nothing is seeded
  // into the store - with no rules there is simply nothing to show.
  if (g_editor_pending) {
    g_editor_pending = false;
    const auto list = RuleStore::instance().all();
    if (!list.empty()) open_editor(list[0]);
  }

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
    render_editor();
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
        if (ImGui::MenuItem("Change rule")) {
          open_editor(r);
          ImGui::EndPopup();
          ImGui::PopID();
          continue;
        }
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

  render_editor();
}

void request_rule_editor_for_first_rule() { g_editor_pending = true; }

}  // namespace ui
