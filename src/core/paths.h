#pragma once
// Resolves application data directories (portable-aware). Layout:
//   portable:  <exe_dir>\PoTaskojoData\
//   installed: %LOCALAPPDATA%\PoTaskojo\   (+ %APPDATA%\PoTaskojo\ for roaming)

#include <filesystem>
#include <string>

namespace core::paths {

struct Layout {
  std::filesystem::path exe_dir;
  std::filesystem::path data_dir;    // settings, themes, history
  std::filesystem::path local_dir;   // %LOCALAPPDATA%\PoTaskojo
  std::filesystem::path roaming_dir; // %APPDATA%\PoTaskojo
  std::filesystem::path logs_dir;
  std::filesystem::path dumps_dir;
  bool portable = false;
};

// Resolved once at startup (thread-safe via static init in .cpp).
const Layout& get();

// Convenience file paths inside data_dir.
std::filesystem::path settings_file();  // settings.ini
std::filesystem::path themes_file();    // theme-pack.json
std::filesystem::path run_history_file();
std::filesystem::path history_dir();    // history\

}  // namespace core::paths
