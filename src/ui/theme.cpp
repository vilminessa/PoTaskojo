#include "ui/theme.h"

#include <imgui.h>

#include <string>

#include "core/log.h"
#include "core/settings.h"

namespace ui {
namespace {

void apply_rounding() {
  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowRounding = 4.0f;
  s.FrameRounding = 3.0f;
  s.GrabRounding = 3.0f;
  s.ScrollbarRounding = 6.0f;
}

void setup_dark() {
  ImGui::StyleColorsDark();
  apply_rounding();
  ImGuiStyle& s = ImGui::GetStyle();
  s.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.07f, 0.09f, 1.0f);
  s.Colors[ImGuiCol_ChildBg] = ImVec4(0.06f, 0.07f, 0.09f, 1.0f);
  s.Colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.15f, 0.18f, 1.0f);
  s.Colors[ImGuiCol_Header] = ImVec4(0.16f, 0.19f, 0.24f, 1.0f);
  s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.40f, 0.58f, 1.0f);
  s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.22f, 0.36f, 0.52f, 1.0f);
}

void setup_light() {
  ImGui::StyleColorsLight();
  apply_rounding();
  ImGuiStyle& s = ImGui::GetStyle();
  s.Colors[ImGuiCol_WindowBg] = ImVec4(0.94f, 0.95f, 0.96f, 1.0f);
  s.Colors[ImGuiCol_ChildBg] = ImVec4(0.94f, 0.95f, 0.96f, 1.0f);
  s.Colors[ImGuiCol_FrameBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  s.Colors[ImGuiCol_Header] = ImVec4(0.86f, 0.88f, 0.91f, 1.0f);
  s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.72f, 0.80f, 0.90f, 1.0f);
  s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.62f, 0.72f, 0.85f, 1.0f);
}

}  // namespace

void apply_theme(bool light) {
  if (light) setup_light();
  else setup_dark();
}

void init_theme() {
  const bool light = core::settings::get_bool(L"Theme", L"LightTheme", false);
  apply_theme(light);
  core::log::info(light ? "theme: light" : "theme: dark");
}

}  // namespace ui
