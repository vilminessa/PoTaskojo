#include "ui/widgets.h"

#include <imgui.h>

namespace ui {
namespace {

ImVec4 g_section_color(0.42f, 0.62f, 0.92f, 1.0f);

}  // namespace

const ImVec4& section_color() { return g_section_color; }

void SectionHeader(const char* label) {
  ImGui::Spacing();
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Text, g_section_color);
  ImGui::TextUnformatted(label);
  ImGui::PopStyleColor();
  ImGui::Separator();
}

void Hint(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text,
                        ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

}  // namespace ui
