#pragma once
// Small shared UI widgets for consistent layout rhythm.

#include <imgui.h>

namespace ui {

// Section header: spacing above, accent-coloured label, rule below.
// Replaces ad-hoc TextDisabled + Separator pairs.
void SectionHeader(const char* label);

// Wrapped hint text in the muted "disabled" colour (TextDisabled clips
// instead of wrapping, which overflows at large UI scales).
void Hint(const char* text);

// Accent colour used for section headers (theme-aware via apply_theme).
const ImVec4& section_color();

}  // namespace ui
