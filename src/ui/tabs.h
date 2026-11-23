#pragma once
// Tab definitions: the standard system-monitor tabs.

#include <array>
#include <string_view>

namespace ui {

enum class Tab : int {
  Processes = 0,
  Performance,
  Services,
  StartupApps,
  Connections,
  Information,
  Rules,
  Count
};

inline constexpr std::array<std::string_view, static_cast<size_t>(Tab::Count)>
    kTabNames = {"Processes", "Performance", "Services",
                 "Startup Apps", "Connections", "Information", "Rules"};

void render_tab(Tab tab);  // implemented in ui/*.cpp

}  // namespace ui
