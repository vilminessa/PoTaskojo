#pragma once
// Performance tab: CPU / memory / disk / network graphs.

namespace ui {

// Records the newest performance sample into the chart history. Called once per
// frame regardless of which tab is showing, and it ignores a sample it has
// already taken - the collector produces one per second, the frame loop runs
// far faster than that, and pushing per frame would fill ten minutes of history
// in ten seconds.
void performance_ingest();

void render_performance_tab();

}  // namespace ui
