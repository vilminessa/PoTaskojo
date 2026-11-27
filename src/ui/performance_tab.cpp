#include "ui/performance_tab.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include <windows.h>

#include "collectors/performance.h"
#include "ui/chart_time.h"
#include "ui/scale.h"
#include "core/settings.h"
#include "core/format.h"

namespace ui {
namespace {

collectors::PerfCollector& perf() {
  static collectors::PerfCollector c;
  static bool started = false;
  if (!started) {
    started = true;
    c.start(collectors::kSampleIntervalMs);
  }
  return c;
}

// Memory safety valve only: the age window is what decides how much history is
// kept. The cap is derived from the sampling interval so that raising the rate
// cannot quietly shorten the window - a flat number would have cut ten minutes
// down to four at 250 ms.
constexpr size_t kHistory = static_cast<size_t>(chart_time::history_cap(
    chart_time::kWindowMs, collectors::kSampleIntervalMs));

struct Sample {
  long long ms;  // GetTickCount64 at the moment the sample was taken
  double v;
};

struct Series {
  std::deque<Sample> v;
  void push(long long ms, double value) {
    v.push_back({ms, value});
    // Age first: ten minutes of real time, not ten minutes' worth of whatever
    // count the sampling rate happens to produce.
    while (v.size() > 1 && !chart_time::inside_window(ms, v.front().ms))
      v.pop_front();
    while (v.size() > kHistory) v.pop_front();
  }
};

Series g_cpu;       // total %, shaded
Series g_cpu_kern;  // kernel %
Series g_mem;       // used %, shaded
Series g_swap;      // pagefile usage %
Series g_disk_r;    // read B/s
Series g_disk_w;    // write B/s
Series g_net_s;     // send B/s
Series g_net_r;     // recv B/s

// --- Chart options (persisted under our own "Charts" section) ---
struct ChartOpts {
  bool show_cpu = true;
  bool show_mem = true;
  bool show_disk = true;
  bool show_net = true;
  bool auto_scale = true;  // pick magnitude (KB/s, MB/s, ...) from the data
  bool smooth = false;     // EMA smoothing
  bool bits = false;       // network unit: bits/s instead of bytes/s
};

ChartOpts& opts() {
  static ChartOpts o = [] {
    ChartOpts c;
    c.show_cpu = core::settings::get_bool(L"Charts", L"ShowCpuChart", true);
    c.show_mem = core::settings::get_bool(L"Charts", L"ShowMemoryChart", true);
    c.show_disk = core::settings::get_bool(L"Charts", L"ShowDiskChart", true);
    c.show_net = core::settings::get_bool(L"Charts", L"ShowNetworkChart", true);
    c.auto_scale = core::settings::get_bool(L"Charts", L"ChartAutoScale", true);
    c.smooth = core::settings::get_bool(L"Charts", L"SmoothCharts", false);
    c.bits = core::settings::get_bool(L"Charts", L"NetworkBitsPerSecond", false);
    return c;
  }();
  return o;
}

void persist(const wchar_t* key, bool value) {
  core::settings::set_bool(L"Charts", key, value);
}

struct Stats {
  double now = 0, peak = 0, avg = 0, minv = 0;
};

Stats stats_of(const std::deque<Sample>& d) {
  Stats s;
  if (d.empty()) return s;
  s.now = d.back().v;
  s.peak = s.minv = d.front().v;
  double sum = 0.0;
  for (const Sample& x : d) {
    if (x.v > s.peak) s.peak = x.v;
    if (x.v < s.minv) s.minv = x.v;
    sum += x.v;
  }
  s.avg = sum / static_cast<double>(d.size());
  return s;
}

double mem_pct_of(const collectors::PerfSnapshot& s) {
  return s.memory.total_bytes
             ? 100.0 * static_cast<double>(s.memory.used_bytes) /
                   static_cast<double>(s.memory.total_bytes)
             : 0.0;
}

double swap_pct_of(const collectors::PerfSnapshot& s) {
  return s.memory.swap_total_bytes
             ? 100.0 * static_cast<double>(s.memory.swap_used_bytes) /
                   static_cast<double>(s.memory.swap_total_bytes)
             : 0.0;
}

// --- Y axis formatting for byte rates ---

struct RateAxis {
  double div;   // bytes per display unit
  bool bits;    // display as bits
};

int rate_formatter(double value, char* buff, int size, void* data) {
  const auto* a = static_cast<const RateAxis*>(data);
  const double v = a->bits ? value * 8.0 / a->div : value / a->div;
  const char* unit;
  if (a->div <= 1.0) unit = a->bits ? "b/s" : "B/s";
  else if (a->div < 1048576.0) unit = a->bits ? "kb/s" : "KB/s";
  else if (a->div < 1073741824.0) unit = a->bits ? "Mb/s" : "MB/s";
  else unit = a->bits ? "Gb/s" : "GB/s";
  const int n = snprintf(buff, size, a->div <= 1.0 ? "%.0f %s" : "%.1f %s", v, unit);
  return n < 0 ? 0 : n;
}

// Magnitude (bytes per display unit) for the given peak rate.
double pick_div(double max_bps, bool bits, bool auto_scale) {
  if (!auto_scale) return 1.0;
  const double v = bits ? max_bps * 8.0 : max_bps;
  const double base = bits ? 1000.0 : 1024.0;
  if (v < base) return 1.0;
  if (v < base * base) return base;
  if (v < base * base * base) return base * base;
  return base * base * base;
}

// EMA smoothing in place.
void smooth_inplace(std::vector<float>& v, float alpha) {
  if (v.empty()) return;
  float s = v.front();
  for (float& x : v) {
    s = alpha * x + (1.0f - alpha) * s;
    x = s;
  }
}

// --- one chart ---

void render_chart(const char* id, const char* title,
                  const Series& s1, const char* label1, const ImVec4& col1,
                  const Series* s2, const char* label2, const ImVec4& col2,
                  bool percent, bool rate, const char* headline,
                  long long now_ms) {
  const int n = static_cast<int>(s1.v.size());
  if (n < 2) {
    ImGui::TextDisabled("%s: collecting...", title);
    return;
  }

  // Header: title (left) + headline value (right).
  ImGui::Text("%s", title);
  if (headline && *headline) {
    const float w = ImGui::CalcTextSize(headline).x;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail - w > 0.0f) ImGui::SameLine(avail - w);
    ImGui::Text("%s", headline);
  }

  // X is the real age of each sample, so the axis stays honest if the
  // collector drifts, pauses, or the machine wakes from sleep.
  const double span_sec =
      static_cast<double>(now_ms - s1.v.front().ms) / 1000.0;

  // Data buffers (must outlive EndPlot).
  std::vector<float> xs(n), y1(n);
  std::vector<float> y2;
  for (int i = 0; i < n; ++i) {
    const Sample& s = s1.v[static_cast<size_t>(i)];
    xs[i] = static_cast<float>(-(now_ms - s.ms)) / 1000.0f;
    y1[i] = static_cast<float>(s.v);
  }
  const bool has2 = s2 && static_cast<int>(s2->v.size()) == n;
  if (has2) {
    y2.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
      y2[static_cast<size_t>(i)] =
          static_cast<float>(s2->v[static_cast<size_t>(i)].v);
  }

  if (opts().smooth) {
    smooth_inplace(y1, 0.3f);
    if (has2) smooth_inplace(y2, 0.3f);
  }

  // Y range + rate unit magnitude.
  double ymax = 1.0;
  if (percent) {
    ymax = 100.0;
  } else {
    for (float x : y1) ymax = std::max(ymax, static_cast<double>(x));
    if (has2)
      for (float x : y2) ymax = std::max(ymax, static_cast<double>(x));
    ymax *= 1.08;
  }
  RateAxis rate_axis{rate ? pick_div(ymax, opts().bits, opts().auto_scale) : 1.0,
                     opts().bits};

  // Time ticks (labels must outlive EndPlot).
  const chart_time::TimeTicks ticks =
      chart_time::compute_time_ticks(span_sec);

  // Stats line under the plot.
  const Stats st = stats_of(s1.v);
  std::string stats =
      (rate ? core::format::format_rate(st.now, opts().bits) : core::format::format_pct(st.now)) +
      " now  |  peak " +
      (rate ? core::format::format_rate(st.peak, opts().bits) : core::format::format_pct(st.peak)) +
      "  |  avg " +
      (rate ? core::format::format_rate(st.avg, opts().bits) : core::format::format_pct(st.avg)) +
      "  |  min " +
      (rate ? core::format::format_rate(st.minv, opts().bits) : core::format::format_pct(st.minv));
  if (has2) {
    const Stats st2 = stats_of(s2->v);
    stats += "  ||  ";
    stats += label2;
    stats += " ";
    stats += rate ? core::format::format_rate(st2.now, opts().bits) : core::format::format_pct(st2.now);
  }

  const ImVec2 size(-1, dp(105.0f));
  ImPlotFlags pflags = ImPlotFlags_NoTitle;
  if (!has2) pflags |= ImPlotFlags_NoLegend;

  char plot_id[64];
  snprintf(plot_id, sizeof(plot_id), "##%s", id);

  if (ImPlot::BeginPlot(plot_id, size, pflags)) {
    ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_None);
    ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_None);
    if (rate)
      ImPlot::SetupAxisFormat(ImAxis_Y1, rate_formatter, &rate_axis);
    else
      ImPlot::SetupAxisFormat(ImAxis_Y1, "%.0f%%");

    // Fewer, well-spaced Y ticks (auto-picking crowds a short plot).
    if (percent) {
      static const double kPctTicks[] = {0, 25, 50, 75, 100};
      ImPlot::SetupAxisTicks(ImAxis_Y1, kPctTicks, 5, nullptr, false);
    } else {
      const double rate_ticks[] = {0.0, ymax * 0.5, ymax};
      ImPlot::SetupAxisTicks(ImAxis_Y1, rate_ticks, 3, nullptr, false);
    }

    ImPlot::SetupAxisLimits(ImAxis_X1, -span_sec, 0.0, ImPlotCond_Always);
    ImPlot::SetupAxisTicks(ImAxis_X1, ticks.pos.data(),
                           static_cast<int>(ticks.pos.size()),
                           ticks.ptrs.data(), false);
    ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, ymax, ImPlotCond_Always);

    ImPlotSpec spec1;
    spec1.LineColor = col1;
    spec1.FillColor = col1;
    spec1.FillAlpha = 0.25f;
    spec1.LineWeight = 1.5f;
    spec1.Flags = ImPlotLineFlags_Shaded;
    ImPlot::PlotLine(label1, xs.data(), y1.data(), n, spec1);

    if (has2) {
      ImPlotSpec spec2;
      spec2.LineColor = col2;
      spec2.FillAlpha = 1.0f;
      spec2.LineWeight = 1.2f;
      ImPlot::PlotLine(label2, xs.data(), y2.data(), n, spec2);
    }
    ImPlot::EndPlot();
  }

  ImGui::TextDisabled("%s", stats.c_str());
}

}  // namespace

void performance_ingest() {
  static uint64_t last_seq = 0;
  auto snap = perf().latest();
  // Same shape as the rules feed in App::render_frame: one record per sample,
  // never per frame, and independent of which tab is on screen.
  if (!snap || snap->seq == last_seq) return;
  last_seq = snap->seq;

  const long long ms = static_cast<long long>(snap->taken_ms);
  g_cpu.push(ms, snap->cpu.total_percent);
  g_cpu_kern.push(ms, snap->cpu.kernel_percent);
  g_mem.push(ms, mem_pct_of(*snap));
  g_swap.push(ms, swap_pct_of(*snap));
  g_disk_r.push(ms, static_cast<double>(snap->disk.read_bytes_s));
  g_disk_w.push(ms, static_cast<double>(snap->disk.write_bytes_s));
  g_net_s.push(ms, static_cast<double>(snap->net.bytes_sent_s));
  g_net_r.push(ms, static_cast<double>(snap->net.bytes_recv_s));
}

void render_performance_tab() {
  auto snap = perf().latest();
  if (!snap) {
    ImGui::TextDisabled("Collecting performance data...");
    return;
  }

  // One clock for every chart in this frame, so they share the same x mapping.
  const long long now_ms = static_cast<long long>(GetTickCount64());
  const double disk_total = static_cast<double>(snap->disk.read_bytes_s +
                                                snap->disk.write_bytes_s);
  // The status line reports the window in minutes and seconds; computed from
  // real timestamps it finally means what it says.
  int span_s = 0;
  if (!g_cpu.v.empty())
    span_s = static_cast<int>((now_ms - g_cpu.v.front().ms) / 1000);

  auto& o = opts();

  // --- Toolbar ---
  struct Toggle {
    const char* label;
    bool ChartOpts::*field;
    const wchar_t* key;
  };
  static const Toggle kToggles[] = {
      {"CPU", &ChartOpts::show_cpu, L"ShowCpuChart"},
      {"Memory", &ChartOpts::show_mem, L"ShowMemoryChart"},
      {"Disk", &ChartOpts::show_disk, L"ShowDiskChart"},
      {"Network", &ChartOpts::show_net, L"ShowNetworkChart"},
  };
  for (const auto& t : kToggles) {
    bool v = o.*(t.field);
    if (ImGui::Checkbox(t.label, &v)) {
      o.*(t.field) = v;
      persist(t.key, v);
    }
    ImGui::SameLine();
  }
  {
    const char* unit = o.bits ? "Bits/s" : "Bytes/s";
    ImGui::SetNextItemWidth(dp(96.0f));
    if (ImGui::BeginCombo("##netunit", unit)) {
      if (ImGui::MenuItem("Bytes/s", nullptr, !o.bits)) {
        o.bits = false;
        persist(L"NetworkBitsPerSecond", false);
      }
      if (ImGui::MenuItem("Bits/s", nullptr, o.bits)) {
        o.bits = true;
        persist(L"NetworkBitsPerSecond", true);
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
  }
  {
    bool v = o.auto_scale;
    if (ImGui::Checkbox("Scale to fit", &v)) {
      o.auto_scale = v;
      persist(L"ChartAutoScale", v);
    }
    ImGui::SameLine();
  }
  {
    bool v = o.smooth;
    if (ImGui::Checkbox("Smooth", &v)) {
      o.smooth = v;
      persist(L"SmoothCharts", v);
    }
  }
  ImGui::Separator();

  const ImVec4 kCpu(0.30f, 0.70f, 1.00f, 1.0f);
  const ImVec4 kKern(0.55f, 0.50f, 0.85f, 1.0f);
  const ImVec4 kMem(0.40f, 0.90f, 0.50f, 1.0f);
  const ImVec4 kSwap(0.92f, 0.85f, 0.35f, 1.0f);
  const ImVec4 kRead(0.30f, 0.85f, 0.75f, 1.0f);
  const ImVec4 kWrite(1.00f, 0.80f, 0.30f, 1.0f);
  const ImVec4 kSend(0.90f, 0.50f, 0.90f, 1.0f);
  const ImVec4 kRecv(0.40f, 0.80f, 0.95f, 1.0f);

  if (o.show_cpu) {
    render_chart("cpu", "CPU", g_cpu, "Total", kCpu, &g_cpu_kern, "Kernel",
                 kKern, true, false,
                 core::format::format_pct(snap->cpu.total_percent).c_str(),
                 now_ms);

    // Per-core bars.
    if (!snap->cpu.per_core.empty()) {
      ImGui::TextDisabled("Per-core");
      const int cores = static_cast<int>(snap->cpu.per_core.size());
      const float avail = ImGui::GetContentRegionAvail().x;
      const float cell = std::max(dp(24.0f), (avail - (cores - 1) * dp(4.0f)) / cores);
      for (int i = 0; i < cores; ++i) {
        if (i > 0) ImGui::SameLine(0, dp(4.0f));
        ImGui::PushID(i);
        char lbl[16];
        snprintf(lbl, sizeof(lbl), "%d", i);
        const float frac = static_cast<float>(snap->cpu.per_core[static_cast<size_t>(i)] / 100.0);
        ImGui::ProgressBar(frac, ImVec2(cell, dp(18.0f)), lbl);
        ImGui::PopID();
      }
    }
    ImGui::Separator();
  }

  if (o.show_mem) {
    const std::string head =
        core::format::format_bytes(snap->memory.used_bytes) + " / " +
        core::format::format_bytes(snap->memory.total_bytes);
    render_chart("mem", "Memory", g_mem, "RAM used", kMem, &g_swap, "Swap",
                 kSwap, true, false, head.c_str(), now_ms);
    ImGui::TextDisabled(
        "Committed %s of %s  |  Swap %s of %s",
        core::format::format_bytes(snap->memory.committed_bytes).c_str(),
        core::format::format_bytes(snap->memory.commit_limit).c_str(),
        core::format::format_bytes(snap->memory.swap_used_bytes).c_str(),
        core::format::format_bytes(snap->memory.swap_total_bytes).c_str());
    ImGui::Separator();
  }

  if (o.show_disk) {
    render_chart("disk", "Disk", g_disk_r, "Read", kRead, &g_disk_w, "Write",
                 kWrite, false, true,
                 core::format::format_rate(disk_total, false).c_str(), now_ms);
    ImGui::Separator();
  }

  if (o.show_net) {
    const std::string head =
        "Send " + core::format::format_rate(static_cast<double>(snap->net.bytes_sent_s), o.bits) +
        "  |  Recv " +
        core::format::format_rate(static_cast<double>(snap->net.bytes_recv_s), o.bits);
    render_chart("net", "Network", g_net_s, "Send", kSend, &g_net_r, "Recv",
                 kRecv, false, true, head.c_str(), now_ms);
  }

  ImGui::Spacing();
  ImGui::TextDisabled("sample #%llu  |  window %d min %02d s  |  %zu shown",
                      static_cast<unsigned long long>(snap->seq), span_s / 60,
                      span_s % 60,
                      static_cast<size_t>(o.show_cpu + o.show_mem +
                                          o.show_disk + o.show_net));
}

}  // namespace ui
