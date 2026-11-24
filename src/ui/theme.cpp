#include "ui/theme.h"

#include <imgui.h>
#include <implot.h>

#include <string>

#include "core/log.h"
#include "core/settings.h"
#include "ui/scale.h"

namespace ui {
namespace {

void apply_rounding(ImGuiStyle& s) {
  s.WindowRounding = 4.0f;
  s.FrameRounding = 3.0f;
  s.GrabRounding = 3.0f;
  s.ScrollbarRounding = 6.0f;
}

// Pristine base styles. Always start from these before scaling, otherwise
// ScaleAllSizes() would accumulate on repeated DPI changes.
void build_dark(ImGuiStyle& s) {
  ImGui::StyleColorsDark(&s);
  apply_rounding(s);
  s.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.07f, 0.09f, 1.0f);
  s.Colors[ImGuiCol_ChildBg] = ImVec4(0.06f, 0.07f, 0.09f, 1.0f);
  s.Colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.15f, 0.18f, 1.0f);
  s.Colors[ImGuiCol_Header] = ImVec4(0.16f, 0.19f, 0.24f, 1.0f);
  s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.40f, 0.58f, 1.0f);
  s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.22f, 0.36f, 0.52f, 1.0f);
}

void build_light(ImGuiStyle& s) {
  ImGui::StyleColorsLight(&s);
  apply_rounding(s);
  s.Colors[ImGuiCol_WindowBg] = ImVec4(0.94f, 0.95f, 0.96f, 1.0f);
  s.Colors[ImGuiCol_ChildBg] = ImVec4(0.94f, 0.95f, 0.96f, 1.0f);
  s.Colors[ImGuiCol_FrameBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  s.Colors[ImGuiCol_Header] = ImVec4(0.86f, 0.88f, 0.91f, 1.0f);
  s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.72f, 0.80f, 0.90f, 1.0f);
  s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.62f, 0.72f, 0.85f, 1.0f);
}

// ImPlot has no ScaleAllSizes(); scale the pixel-valued fields by hand.
// Skips multipliers (MinorAlpha), percentages (FitPadding) and colors.
void scale_implot(ImPlotStyle& p, float s) {
  auto v2 = [s](ImVec2 a) { return ImVec2(a.x * s, a.y * s); };
  p.PlotDefaultSize = v2(p.PlotDefaultSize);
  p.PlotMinSize = v2(p.PlotMinSize);
  p.PlotBorderSize *= s;
  p.MajorTickLen = v2(p.MajorTickLen);
  p.MinorTickLen = v2(p.MinorTickLen);
  p.MajorTickSize = v2(p.MajorTickSize);
  p.MinorTickSize = v2(p.MinorTickSize);
  p.MajorGridSize = v2(p.MajorGridSize);
  p.MinorGridSize = v2(p.MinorGridSize);
  p.PlotPadding = v2(p.PlotPadding);
  p.LabelPadding = v2(p.LabelPadding);
  p.LegendPadding = v2(p.LegendPadding);
  p.LegendInnerPadding = v2(p.LegendInnerPadding);
  p.LegendSpacing = v2(p.LegendSpacing);
  p.MousePosPadding = v2(p.MousePosPadding);
  p.AnnotationPadding = v2(p.AnnotationPadding);
  p.DigitalPadding *= s;
  p.DigitalSpacing *= s;
}

}  // namespace

void apply_theme(bool light) {
  const float scale = ui::ui_scale();

  // --- ImGui style: pristine base -> scale -> publish ---
  ImGuiStyle base;
  if (light) build_light(base);
  else build_dark(base);
  base.ScaleAllSizes(scale);
  ImGui::GetStyle() = base;

  // --- ImPlot style: pristine base -> scale -> publish ---
  ImPlotStyle pbase;
  if (light) ImPlot::StyleColorsLight(&pbase);
  else ImPlot::StyleColorsDark(&pbase);
  scale_implot(pbase, scale);
  ImPlot::GetStyle() = pbase;
}

void init_theme() {
  const bool light = core::settings::get_bool(L"Theme", L"LightMode", false);
  apply_theme(light);
  core::log::info((light ? std::string("theme: light") : std::string("theme: dark")) +
                  " @ scale " + std::to_string(ui::ui_scale()));
}

}  // namespace ui
