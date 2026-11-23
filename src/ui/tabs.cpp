#include "ui/tabs.h"

#include <imgui.h>

#include "ui/other_tabs.h"
#include "ui/performance_tab.h"
#include "ui/processes_tab.h"
#include "ui/rules_tab.h"

namespace ui {
namespace {

void placeholder(const char* title, const char* hint) {
  ImGui::TextUnformatted(title);
  ImGui::TextDisabled("%s", hint);
}

}  // namespace

void render_tab(Tab tab) {
  switch (tab) {
    case Tab::Processes:
      render_processes_tab();
      break;
    case Tab::Performance:
      render_performance_tab();
      break;
    case Tab::Services:
      render_services_tab();
      break;
    case Tab::StartupApps:
      render_startup_tab();
      break;
    case Tab::Connections:
      render_connections_tab();
      break;
    case Tab::Information:
      render_information_tab();
      break;
    case Tab::Rules:
      render_rules_tab();
      break;
    case Tab::Count:
      break;
  }
}

}  // namespace ui
