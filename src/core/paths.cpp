#include "core/paths.h"

#include <windows.h>
#include <shlobj.h>

namespace core::paths {
namespace {

std::wstring known_folder(REFKNOWNFOLDERID id) {
  PWSTR out = nullptr;
  if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &out))) return {};
  std::wstring s(out);
  CoTaskMemFree(out);
  return s;
}

Layout make_layout() {
  Layout l;

  wchar_t exe[MAX_PATH]{};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  l.exe_dir = std::filesystem::path(exe).parent_path();

  // Portable mode: data dir next to the exe.
  const auto portable_marker = l.exe_dir / L"PoTaskojoData";
  if (std::filesystem::exists(portable_marker)) {
    l.portable = true;
    l.data_dir = portable_marker;
  } else {
    l.local_dir = std::filesystem::path(known_folder(FOLDERID_LocalAppData)) / L"PoTaskojo";
    l.roaming_dir = std::filesystem::path(known_folder(FOLDERID_RoamingAppData)) / L"PoTaskojo";
    l.data_dir = l.local_dir;
  }

  l.logs_dir = l.data_dir / L"logs";
  l.dumps_dir = l.data_dir / L"dumps";
  std::error_code ec;
  std::filesystem::create_directories(l.logs_dir, ec);
  std::filesystem::create_directories(l.dumps_dir, ec);
  if (!l.portable) {
    std::filesystem::create_directories(l.local_dir, ec);
    std::filesystem::create_directories(l.roaming_dir, ec);
  }
  return l;
}

}  // namespace

const Layout& get() {
  static const Layout layout = make_layout();
  return layout;
}

std::filesystem::path settings_file() { return get().data_dir / L"settings.ini"; }
std::filesystem::path themes_file() { return get().data_dir / L"themes.json"; }
std::filesystem::path run_history_file() { return get().data_dir / L"runHistory.json"; }
std::filesystem::path history_dir() { return get().data_dir / L"processHistory"; }

}  // namespace core::paths
