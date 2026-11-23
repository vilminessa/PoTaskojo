#include "ui/performance_tab.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include <imgui.h>

#include "collectors/performance.h"

namespace ui {
namespace {

collectors::PerfCollector& perf() {
  static collectors::PerfCollector c;
  static bool started = false;
  if (!started) {
    started = true;
    c.start(1000);
  }
  return c;
}

// Rolling history for sparklines (last N samples).
constexpr size_t kHistory = 120;
std::deque<double> g_cpu_history;
std::deque<double> g_mem_history;
std::deque<double> g_net_history;
std::deque<double> g_disk_history;

void push(std::deque<double>& h, double v) {
  h.push_back(v);
  if (h.size() > kHistory) h.pop_front();
}

// Max value of a history series (for graph auto-scaling), min 1.0.
float history_max(const std::deque<double>& h) {
  if (h.empty()) return 1.0f;
  double m = 1.0;
  for (double v : h)
    if (v > m) m = v;
  return static_cast<float>(m);
}

std::string format_bytes(uint64_t b) {
  char buf[64];
  if (b < 1024ull) snprintf(buf, sizeof(buf), "%llu B", b);
  else if (b < 1024ull * 1024) snprintf(buf, sizeof(buf), "%.1f KB", b / 1024.0);
  else if (b < 1024ull * 1024 * 1024)
    snprintf(buf, sizeof(buf), "%.1f MB", b / (1024.0 * 1024));
  else
    snprintf(buf, sizeof(buf), "%.2f GB", b / (1024.0 * 1024 * 1024));
  return buf;
}

std::string format_rate(uint64_t bps) {
  char buf[64];
  if (bps < 1024ull) snprintf(buf, sizeof(buf), "%llu B/s", bps);
  else if (bps < 1024ull * 1024) snprintf(buf, sizeof(buf), "%.1f KB/s", bps / 1024.0);
  else if (bps < 1024ull * 1024 * 1024)
    snprintf(buf, sizeof(buf), "%.1f MB/s", bps / (1024.0 * 1024));
  else
    snprintf(buf, sizeof(buf), "%.2f GB/s", bps / (1024.0 * 1024 * 1024));
  return buf;
}

void graph(const char* id, const std::deque<double>& data, float max_v,
           const ImVec2& size, const ImVec4& color) {
  if (data.empty()) {
    ImGui::Dummy(size);
    return;
  }
  std::vector<float> vals;
  vals.reserve(data.size());
  for (double v : data) vals.push_back(static_cast<float>(v));
  ImGui::PushStyleColor(ImGuiCol_PlotLines, color);
  ImGui::PlotLines(id, vals.data(), static_cast<int>(vals.size()), 0, nullptr,
                   0.0f, max_v, size);
  ImGui::PopStyleColor();
}

}  // namespace

void render_performance_tab() {
  auto snap = perf().latest();
  if (!snap) {
    ImGui::TextDisabled("Collecting performance data...");
    return;
  }

  push(g_cpu_history, snap->cpu.total_percent);
  push(g_mem_history, snap->memory.total_bytes
                          ? 100.0 * snap->memory.used_bytes / snap->memory.total_bytes
                          : 0.0);
  push(g_net_history, static_cast<double>(snap->net.bytes_recv_s +
                                          snap->net.bytes_sent_s));
  push(g_disk_history, static_cast<double>(snap->disk.bytes_per_sec));

  const ImVec2 graph_size(-1, 80);

  // --- CPU ---
  ImGui::Text("CPU");
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
  ImGui::Text("%.1f%%", snap->cpu.total_percent);
  graph("##cpu", g_cpu_history, 100.0f, graph_size, ImVec4(0.3f, 0.7f, 1.0f, 1.0f));

  // Per-core bars.
  if (!snap->cpu.per_core.empty()) {
    ImGui::TextDisabled("Per-core");
    const int cores = static_cast<int>(snap->cpu.per_core.size());
    const float avail = ImGui::GetContentRegionAvail().x;
    const float cell = std::max(24.0f, (avail - (cores - 1) * 4.0f) / cores);
    for (int i = 0; i < cores; ++i) {
      if (i > 0) ImGui::SameLine(0, 4);
      ImGui::PushID(i);
      char lbl[16];
      snprintf(lbl, sizeof(lbl), "%d", i);
      const float frac = static_cast<float>(snap->cpu.per_core[i] / 100.0);
      ImGui::ProgressBar(frac, ImVec2(cell, 18), lbl);
      ImGui::PopID();
    }
  }
  ImGui::Separator();

  // --- Memory ---
  ImGui::Text("Memory");
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - 180);
  ImGui::Text("%s / %s", format_bytes(snap->memory.used_bytes).c_str(),
              format_bytes(snap->memory.total_bytes).c_str());
  graph("##mem", g_mem_history, 100.0f, graph_size, ImVec4(0.4f, 0.9f, 0.5f, 1.0f));
  ImGui::TextDisabled("Committed: %s of %s",
                      format_bytes(snap->memory.committed_bytes).c_str(),
                      format_bytes(snap->memory.commit_limit).c_str());
  ImGui::Separator();

  // --- Disk ---
  ImGui::Text("Disk");
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120);
  ImGui::Text("%s", format_rate(snap->disk.bytes_per_sec).c_str());
  graph("##disk", g_disk_history, history_max(g_disk_history),
        graph_size, ImVec4(1.0f, 0.8f, 0.3f, 1.0f));
  ImGui::Separator();

  // --- Network ---
  ImGui::Text("Network");
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - 240);
  ImGui::Text("Send %s  |  Recv %s",
              format_rate(snap->net.bytes_sent_s).c_str(),
              format_rate(snap->net.bytes_recv_s).c_str());
  graph("##net", g_net_history, history_max(g_net_history),
        graph_size, ImVec4(0.9f, 0.5f, 0.9f, 1.0f));

  ImGui::Spacing();
  ImGui::TextDisabled("sample #%llu",
                      static_cast<unsigned long long>(snap->seq));
}

}  // namespace ui
