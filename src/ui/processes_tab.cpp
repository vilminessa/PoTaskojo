#include "ui/processes_tab.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>

#include "collectors/processes.h"
#include "ui/scale.h"
#include "collectors/process_actions.h"
#include "rules/rules.h"
#include "core/format.h"

namespace ui {

collectors::ProcessCollector& process_collector() {
  static collectors::ProcessCollector c;
  static bool started = false;
  if (!started) {
    started = true;
    c.start(1000);
  }
  return c;
}

namespace {

collectors::ProcessCollector& collector() { return process_collector(); }

char g_search[128] = "";
int g_sort_col = 2;  // default: CPU desc
bool g_sort_asc = false;

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

const char* priority_name(int pc) {
  switch (pc) {
    case IDLE_PRIORITY_CLASS: return "Idle";
    case BELOW_NORMAL_PRIORITY_CLASS: return "Below normal";
    case NORMAL_PRIORITY_CLASS: return "Normal";
    case ABOVE_NORMAL_PRIORITY_CLASS: return "Above normal";
    case HIGH_PRIORITY_CLASS: return "High";
    case REALTIME_PRIORITY_CLASS: return "Realtime";
    default: return nullptr;
  }
}

// System processes (services, lsass, csrss...) report dynamic base priorities
// (9, 11, 13) that don't map to a class; Task Manager shows them as "Base N".
std::string priority_display(const collectors::ProcessRow& r) {
  if (const char* n = priority_name(r.priority_class)) return n;
  if (r.base_priority > 0) return "Base " + std::to_string(r.base_priority);
  return "-";
}

bool matches_search(const collectors::ProcessRow& r, const char* q) {
  if (!q || !*q) return true;
  // Case-insensitive UTF-8-ish substring on name and path.
  const std::string name = narrow(r.name);
  const std::string path = narrow(r.image_path);
  auto contains_ci = [&](const std::string& hay) {
    auto tolow = [](unsigned char c) { return static_cast<char>(tolower(c)); };
    std::string h(hay), n(q);
    std::transform(h.begin(), h.end(), h.begin(), tolow);
    std::transform(n.begin(), n.end(), n.begin(), tolow);
    return h.find(n) != std::string::npos;
  };
  return contains_ci(name) || contains_ci(path);
}

}  // namespace

void render_processes_tab() {
  static std::vector<collectors::ProcessRow> view;
  auto snap = collector().latest();

  // Search box (type-to-filter).
  ImGui::SetNextItemWidth(dp(260.0f));
  ImGui::InputTextWithHint("##search", "Search... (type to filter)", g_search,
                           sizeof(g_search));
  ImGui::SameLine();
  const auto totals = collector().totals();
  ImGui::TextDisabled("Processes: %u", totals.process_count);
  ImGui::Separator();

  if (!snap) {
    ImGui::TextDisabled("Collecting process snapshot...");
    return;
  }

  // Build filtered view.
  view.clear();
  view.reserve(snap->rows.size());
  for (const auto& r : snap->rows)
    if (matches_search(r, g_search)) view.push_back(r);

  // Sort.
  std::stable_sort(view.begin(), view.end(), [&](const auto& a, const auto& b) {
    int cmp = 0;
    switch (g_sort_col) {
      case 0: cmp = a.name.compare(b.name); break;
      case 1: cmp = a.pid < b.pid ? -1 : (a.pid > b.pid ? 1 : 0); break;
      case 2:
        cmp = a.cpu_percent < b.cpu_percent ? -1
              : (a.cpu_percent > b.cpu_percent ? 1 : 0);
        break;
      case 3:
        cmp = a.working_set < b.working_set ? -1
              : (a.working_set > b.working_set ? 1 : 0);
        break;
      case 4:
        cmp = a.read_bytes + a.write_bytes < b.read_bytes + b.write_bytes ? -1
              : (a.read_bytes + a.write_bytes > b.read_bytes + b.write_bytes ? 1 : 0);
        break;
      case 5: cmp = a.name.compare(b.name); break;  // placeholder for path
      default: break;
    }
    return g_sort_asc ? cmp < 0 : cmp > 0;
  });

  const ImGuiTableFlags flags =
      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
      ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
      ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
      ImGuiTableFlags_Hideable;

  const float footer = ImGui::GetFrameHeightWithSpacing();
  if (ImGui::BeginTable("##procs", 7, flags,
                        ImVec2(0, -footer))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.0f, 0);
    ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, dp(70.0f), 1);
    ImGui::TableSetupColumn("CPU %", ImGuiTableColumnFlags_WidthFixed, dp(70.0f), 2);
    ImGui::TableSetupColumn("Memory", ImGuiTableColumnFlags_WidthFixed, dp(90.0f), 3);
    ImGui::TableSetupColumn("I/O", ImGuiTableColumnFlags_WidthFixed, dp(90.0f), 4);
    ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, dp(110.0f), 5);
    ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, 1.0f, 6);

    ImGui::TableHeadersRow();
    if (ImGuiTableSortSpecs* sort = ImGui::TableGetSortSpecs()) {
      if (sort->SpecsDirty && sort->SpecsCount > 0) {
        const ImGuiTableColumnSortSpecs* spec = &sort->Specs[0];
        g_sort_col = static_cast<int>(spec->ColumnIndex);
        g_sort_asc = (spec->SortDirection == ImGuiSortDirection_Ascending);
        sort->SpecsDirty = false;
      }
    }

    // Clipper for large lists.
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(view.size()));
    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
        const auto& r = view[i];
        ImGui::PushID(static_cast<int>(r.pid));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (r.is_new) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
        ImGui::TextUnformatted(narrow(r.name).c_str());
        if (r.is_new) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && !r.image_path.empty()) {
          ImGui::SetTooltip("%s\n%s", narrow(r.name).c_str(),
                            narrow(r.image_path).c_str());
        }
        ImGui::TableNextColumn();
        ImGui::Text("%u", r.pid);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(core::format::format_cpu(r.cpu_percent).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(core::format::format_bytes(r.working_set).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(
            core::format::format_bytes(r.read_bytes + r.write_bytes).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(priority_display(r).c_str());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", narrow(r.image_path).c_str());
        // The Path column clips long paths; the name cell already shows one,
        // but this is the cell the eye goes to.
        if (ImGui::IsItemHovered() && !r.image_path.empty()) {
          ImGui::BeginTooltip();
          ImGui::TextWrapped("%s", narrow(r.image_path).c_str());
          ImGui::EndTooltip();
        }

        // Right-click: process actions.
        if (ImGui::BeginPopupContextItem()) {
          using collectors::ProcessAction;
          const bool disabled = collectors::is_protected_pid(r.pid);
          if (disabled) ImGui::BeginDisabled();
          if (ImGui::MenuItem("End task")) {
            collectors::execute({ProcessAction::End, r.pid, 0, r.image_path});
          }
          if (ImGui::MenuItem("Suspend")) {
            collectors::execute({ProcessAction::Suspend, r.pid, 0, r.image_path});
          }
          if (ImGui::MenuItem("Resume")) {
            collectors::execute({ProcessAction::Resume, r.pid, 0, r.image_path});
          }
          if (disabled) ImGui::EndDisabled();

          ImGui::Separator();
          if (ImGui::BeginMenu("Set priority")) {
            struct P { const char* name; int cls; };
            static const P kPriorities[] = {
                {"Realtime", REALTIME_PRIORITY_CLASS},
                {"High", HIGH_PRIORITY_CLASS},
                {"Above normal", ABOVE_NORMAL_PRIORITY_CLASS},
                {"Normal", NORMAL_PRIORITY_CLASS},
                {"Below normal", BELOW_NORMAL_PRIORITY_CLASS},
                {"Idle", IDLE_PRIORITY_CLASS},
            };
            for (const auto& p : kPriorities) {
              if (ImGui::MenuItem(p.name, nullptr,
                                  r.priority_class == p.cls)) {
                collectors::execute({ProcessAction::SetPriority, r.pid, p.cls,
                                     r.image_path});
              }
            }
            ImGui::EndMenu();
          }
          ImGui::Separator();
          if (ImGui::MenuItem("Open file location")) {
            collectors::execute({ProcessAction::OpenLocation, r.pid, 0,
                                 r.image_path});
          }
          if (ImGui::MenuItem("Copy path")) {
            collectors::execute({ProcessAction::CopyPath, r.pid, 0, r.image_path});
          }
          ImGui::EndPopup();
        }

        ImGui::PopID();
      }
    }
    ImGui::EndTable();
  }

  ImGui::TextDisabled("snapshot #%llu  |  %zu rows shown",
                      static_cast<unsigned long long>(snap->seq), view.size());
}

}  // namespace ui
