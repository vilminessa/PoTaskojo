#pragma once
namespace ui {

void render_rules_tab();

// Opens the editor for the first stored rule on the next frame. Internal
// only: the dialog is reached by right-clicking a row, and synthetic clicks
// into ImGui are unreliable in the screenshot harness.
void request_rule_editor_for_first_rule();

}  // namespace ui
