#include "ui/other_tabs.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstdio>
#include <string>

#include <imgui.h>

#include "collectors/connections.h"
#include "core/text.h"
#include "ui/scale.h"
#include "collectors/services.h"
#include "collectors/startup.h"

namespace ui {
namespace {

using core::text::narrow;

// Shared status line for action feedback.
const char*& status_line() {
  static const char* s = "";
  return s;
}

}  // namespace

// ---------------------------------------------------------------------------
// Services
// ---------------------------------------------------------------------------
void render_services_tab() {
  static collectors::ServiceCollector c;
  static bool started = false;
  if (!started) { started = true; c.start(3000); }

  auto snap = c.latest();
  ImGui::TextDisabled("%s", status_line());
  if (!snap) {
    ImGui::TextDisabled("Enumerating services...");
    return;
  }

  static char search[128] = "";
  ImGui::SetNextItemWidth(dp(260.0f));
  ImGui::InputTextWithHint("##svc_search", "Search...", search, sizeof(search));
  ImGui::SameLine();
  ImGui::TextDisabled("%zu services", snap->rows.size());
  ImGui::SameLine();
  if (ImGui::SmallButton("Refresh")) c.refresh_now();
  ImGui::Separator();

  const ImGuiTableFlags flags =
      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
      ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable;
  if (ImGui::BeginTable("##svcs", 5, flags, ImVec2(0, -ImGui::GetFrameHeightWithSpacing()))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0, 0);
    ImGui::TableSetupColumn("Display label", ImGuiTableColumnFlags_WidthStretch, 0, 1);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, dp(90.0f), 2);
    ImGui::TableSetupColumn("Startup", ImGuiTableColumnFlags_WidthFixed, dp(100.0f), 3);
    ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, dp(70.0f), 4);
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(snap->rows.size()));
    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
        const auto& r = snap->rows[i];
        if (*search && !core::text::contains_ci(narrow(r.name), search) &&
            !core::text::contains_ci(narrow(r.display_name), search))
          continue;
        ImGui::PushID(i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(narrow(r.name).c_str());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", narrow(r.display_name).c_str());
        ImGui::TableNextColumn();
        if (r.state == SERVICE_RUNNING)
          ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1), "Running");
        else if (r.state == SERVICE_PAUSED)
          ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1), "Paused");
        else
          ImGui::TextDisabled("Stopped");
        ImGui::TableNextColumn();
        static const wchar_t* kStartup[] = {L"?", L"Boot", L"System", L"Automatic",
                                            L"Manual", L"Disabled"};
        const wchar_t* st = (r.startup_type < 6) ? kStartup[r.startup_type] : L"?";
        char sbuf[32];
        snprintf(sbuf, sizeof(sbuf), "%ls", st);
        ImGui::TextDisabled("%s", sbuf);
        ImGui::TableNextColumn();
        if (r.pid) ImGui::Text("%u", r.pid);

        if (ImGui::BeginPopupContextItem()) {
          using collectors::service_action;
          if (r.state == SERVICE_STOPPED && ImGui::MenuItem("Start")) {
            auto res = service_action(r.name, L"start");
            status_line() = res.ok ? "Started the service" : res.message.c_str();
          }
          if (r.state == SERVICE_RUNNING && ImGui::MenuItem("Stop")) {
            auto res = service_action(r.name, L"stop");
            status_line() = res.ok ? "Stopped the service" : res.message.c_str();
          }
          if (r.state == SERVICE_RUNNING && ImGui::MenuItem("Pause")) {
            service_action(r.name, L"pause");
          }
          if (r.state == SERVICE_PAUSED && ImGui::MenuItem("Continue")) {
            service_action(r.name, L"continue");
          }
          if (ImGui::MenuItem("Restart")) {
            auto res = service_action(r.name, L"restart");
            status_line() = res.ok ? "Restarted the service" : res.message.c_str();
          }
          ImGui::EndPopup();
        }
        ImGui::PopID();
      }
    }
    ImGui::EndTable();
  }
  ImGui::TextDisabled("right-click a service for actions (admin required)");
}

// ---------------------------------------------------------------------------
// Startup Apps
// ---------------------------------------------------------------------------
void render_startup_tab() {
  static collectors::StartupCollector c;
  static bool started = false;
  if (!started) { started = true; c.start(10000); }

  auto snap = c.latest();
  ImGui::TextDisabled("%s", status_line());
  if (!snap) {
    ImGui::TextDisabled("Collecting startup entries...");
    return;
  }
  static char search[128] = "";
  ImGui::SetNextItemWidth(dp(260.0f));
  ImGui::InputTextWithHint("##startup_search", "Search...", search,
                           sizeof(search));
  ImGui::SameLine();
  ImGui::TextDisabled("%zu entries, %zu enabled", snap->rows.size(),
                      snap->enabled_count);
  ImGui::SameLine();
  if (ImGui::SmallButton("Refresh##startup")) c.refresh_now();
  ImGui::Separator();

  const ImGuiTableFlags flags =
      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
      ImGuiTableFlags_Resizable;
  if (ImGui::BeginTable("##startup", 4, flags, ImVec2(0, -ImGui::GetFrameHeightWithSpacing()))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, dp(70.0f));
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthFixed, dp(170.0f));
    ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < snap->rows.size(); ++i) {
      const auto& r = snap->rows[i];
      if (*search && !core::text::contains_ci(narrow(r.name), search) &&
          !core::text::contains_ci(narrow(r.command), search) &&
          !core::text::contains_ci(narrow(r.location), search))
        continue;
      ImGui::PushID(static_cast<int>(i));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      bool en = r.enabled;
      if (ImGui::Checkbox("##en", &en)) {
        c.set_enabled(r, en);
        c.refresh_now();
      }
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(narrow(r.name).c_str());
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", narrow(r.location).c_str());
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", narrow(r.command).c_str());
      // A command line is far wider than the column, and it is exactly what
      // someone checking a startup entry wants to read in full.
      if (ImGui::IsItemHovered() && !r.command.empty()) {
        ImGui::BeginTooltip();
        ImGui::TextWrapped("%s", narrow(r.command).c_str());
        ImGui::EndTooltip();
      }

      if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Enable")) { c.set_enabled(r, true); c.refresh_now(); }
        if (ImGui::MenuItem("Disable")) { c.set_enabled(r, false); c.refresh_now(); }
        if (ImGui::MenuItem("Delete")) {
          if (c.remove(r)) status_line() = "Startup entry removed";
          else status_line() = "Could not remove startup entry";
          c.refresh_now();
        }
        ImGui::EndPopup();
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::TextDisabled("right-click to enable / disable / delete");
}

// ---------------------------------------------------------------------------
// Connections
// ---------------------------------------------------------------------------
void render_connections_tab() {
  static collectors::ConnectionCollector c;
  static bool started = false;
  if (!started) { started = true; c.start(2000); }

  auto snap = c.latest();
  if (!snap) {
    ImGui::TextDisabled("Collecting connections...");
    return;
  }
  ImGui::TextDisabled(
      "%zu connections: %zu TCP, %zu UDP, %zu established, %zu listening",
      snap->rows.size(), snap->tcp_count, snap->udp_count, snap->established,
      snap->listening);
  static char search[128] = "";
  ImGui::SetNextItemWidth(dp(260.0f));
  ImGui::InputTextWithHint("##conn_search", "Search...", search, sizeof(search));
  ImGui::Separator();

  const ImGuiTableFlags flags =
      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
      ImGuiTableFlags_Resizable;
  if (ImGui::BeginTable("##conns", 6, flags, ImVec2(0, -ImGui::GetFrameHeightWithSpacing()))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Proto", ImGuiTableColumnFlags_WidthFixed, dp(55.0f));
    ImGui::TableSetupColumn("Local endpoint", ImGuiTableColumnFlags_WidthFixed, dp(180.0f));
    ImGui::TableSetupColumn("Remote endpoint", ImGuiTableColumnFlags_WidthFixed, dp(180.0f));
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, dp(100.0f));
    ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, dp(70.0f));
    ImGui::TableSetupColumn("Process", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(snap->rows.size()));
    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
        const auto& r = snap->rows[i];
        if (*search) {
          const std::string hay =
              narrow(r.process_name) + " " + narrow(r.local_addr) + " " +
              narrow(r.remote_addr);
          if (!core::text::contains_ci(hay, search)) continue;
        }
        ImGui::PushID(i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", r.udp ? "UDP" : "TCP");
        ImGui::TableNextColumn();
        char buf[80];
        snprintf(buf, sizeof(buf), "%s:%u", narrow(r.local_addr).c_str(), r.local_port);
        ImGui::TextUnformatted(buf);
        ImGui::TableNextColumn();
        if (!r.udp && r.remote_port) {
          snprintf(buf, sizeof(buf), "%s:%u", narrow(r.remote_addr).c_str(), r.remote_port);
          ImGui::TextUnformatted(buf);
        } else {
          ImGui::TextDisabled("-");
        }
        ImGui::TableNextColumn();
        if (!r.udp) {
          // state names are wide; simplify
          ImGui::TextDisabled("%u", r.state);
        } else {
          ImGui::TextDisabled("-");
        }
        ImGui::TableNextColumn();
        ImGui::Text("%u", r.pid);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(narrow(r.process_name).c_str());
        ImGui::PopID();
      }
    }
    ImGui::EndTable();
  }
}

// ---------------------------------------------------------------------------
// Information (system info)
// ---------------------------------------------------------------------------
void render_information_tab() {
  ImGui::TextUnformatted("System Information");
  ImGui::Separator();

  // OS version.
  typedef LONG (WINAPI *RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
  RTL_OSVERSIONINFOW ov{};
  ov.dwOSVersionInfoSize = sizeof(ov);
  if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
    if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")))
      fn(&ov);
  }
  ImGui::Text("Windows version: %lu.%lu (build %lu)", ov.dwMajorVersion,
              ov.dwMinorVersion, ov.dwBuildNumber);

  // Memory.
  MEMORYSTATUSEX ms{};
  ms.dwLength = sizeof(ms);
  if (GlobalMemoryStatusEx(&ms)) {
    ImGui::Text("Physical memory: %.1f GB total, %.1f GB in use",
                ms.ullTotalPhys / (1024.0 * 1024 * 1024),
                (ms.ullTotalPhys - ms.ullAvailPhys) / (1024.0 * 1024 * 1024));
    ImGui::Text("Commit charge: %.1f GB of %.1f GB",
                (ms.ullTotalPageFile - ms.ullAvailPageFile) / (1024.0 * 1024 * 1024),
                ms.ullTotalPageFile / (1024.0 * 1024 * 1024));
  }

  // CPU.
  SYSTEM_INFO si{};
  GetSystemInfo(&si);
  ImGui::Text("Processors: %u logical", si.dwNumberOfProcessors);

  // CPU brand from registry.
  {
    wchar_t brand[256]{};
    DWORD size = sizeof(brand) - sizeof(wchar_t);
    DWORD type = 0;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0,
                      KEY_READ, &key) == ERROR_SUCCESS) {
      RegQueryValueExW(key, L"ProcessorNameString", nullptr, &type,
                       reinterpret_cast<LPBYTE>(brand), &size);
      RegCloseKey(key);
      if (brand[0]) ImGui::Text("CPU: %ls", brand);
    }
  }

  // Uptime.
  const ULONGLONG uptime_ms = GetTickCount64();
  const unsigned days = static_cast<unsigned>(uptime_ms / 86400000);
  const unsigned hours = static_cast<unsigned>((uptime_ms % 86400000) / 3600000);
  const unsigned mins = static_cast<unsigned>((uptime_ms % 3600000) / 60000);
  ImGui::Text("Uptime: %u days %u hours %u minutes", days, hours, mins);

  // Session / elevation.
  ImGui::Text("Elevated: %s", IsUserAnAdmin() ? "yes" : "no");
  DWORD session = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &session);
  ImGui::Text("Session: %lu", session);
}

}  // namespace ui
