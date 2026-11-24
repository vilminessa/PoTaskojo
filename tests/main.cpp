// Test entry point.
//
// Everything the code under test touches on disk (settings.ini, rules.tsv,
// logs) is resolved through core::paths, which switches to portable mode when
// a PoTaskojoData folder sits next to the executable. Creating - and clearing -
// that folder here keeps every run hermetic and inside the build tree, so a
// test run can never touch the developer's real profile.

#include <windows.h>

#include <filesystem>
#include <iostream>

#include "test_support.h"

namespace {

void isolate_data_dir() {
  wchar_t exe[MAX_PATH]{};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  const auto dir = std::filesystem::path(exe).parent_path() / L"PoTaskojoData";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  std::cout << "data dir: " << dir.string() << "\n";
}

}  // namespace

int main() {
  isolate_data_dir();
  return ::testing::run_all();
}
