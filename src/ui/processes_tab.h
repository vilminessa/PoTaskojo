#pragma once
// Processes tab: virtualized table with search, sort, and process actions.

#include "collectors/processes.h"

namespace ui {

void render_processes_tab();

// Shared process collector (started on first use) — used by the tabs and by
// the app frame loop to feed the rules engine.
collectors::ProcessCollector& process_collector();

}  // namespace ui
