#include <windows.h>

#include "app/app.h"
#include "core/log.h"
#include "core/paths.h"
#include "platform/autorun.h"
#include "platform/crash_handler.h"
#include "platform/ifeo.h"
#include "platform/low_memory.h"
#include "platform/single_instance.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmd_line,
                    int cmd_show) {
  // Hidden self-test hook: verifies the crash handler writes crash-report.log +
  // minidump. Internal --internal-* verb, never documented in the UI.
  if (cmd_line && wcsstr(cmd_line, L"--internal-selftest-crash")) {
    const auto& l = core::paths::get();
    core::log::init(l.logs_dir.wstring());
    platform::install_crash_handler();
    core::log::warn("selftest: triggering deliberate crash");
    volatile int* p = nullptr;
    *p = 42;  // intentional AV → unhandled exception filter
    return 0;
  }

  // Open the settings dialog at startup (used for visual tests).
  const bool open_settings =
      cmd_line && wcsstr(cmd_line, L"--internal-show-settings");

  // Open the rule editor at startup (used for visual tests).
  const bool open_rule_editor =
      cmd_line && wcsstr(cmd_line, L"--internal-open-rule-editor");

  // Task Manager replacement mode (IFEO): when Windows launches us instead of
  // taskmgr.exe, note it and offer the genuine Task Manager.
  const bool ifeo_mode =
      platform::launched_as_taskmgr_replacement(cmd_line);

  // DPI awareness before any window creation.
  if (auto user32 = GetModuleHandleW(L"user32.dll")) {
    using SetCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (auto fn = reinterpret_cast<SetCtxFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
      fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
  }

  const auto& layout = core::paths::get();
  core::log::init(layout.logs_dir.wstring());
  core::log::info("PoTaskojo starting");
  platform::install_crash_handler();

  if (ifeo_mode) {
    core::log::info("launched as Task Manager replacement (IFEO)");
    // Offer the real Task Manager (never trap the user in our window).
    platform::launch_original_task_manager();
  }

  // Low-memory watcher: surface critical pressure in the log (status UI hookup
  // can be added to the shell later).
  platform::LowMemoryWatcher::instance().start([](bool critical) {
    core::log::warn(critical ? "LOW MEMORY: close some processes"
                             : "memory pressure cleared");
  });

  platform::SingleInstance guard(L"Local\\PoTaskojo_Mutex");
  if (!guard.owns()) {
    // MVP: simply exit; activation of existing window comes later.
    core::log::info("another instance is running, exiting");
    core::log::shutdown();
    return 0;
  }

  const int rc =
      app::App::instance().run(instance, cmd_show, open_settings, open_rule_editor);
  return rc;
}
