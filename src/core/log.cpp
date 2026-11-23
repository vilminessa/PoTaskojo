#include "core/log.h"

#include <windows.h>

#include <cstdio>
#include <ctime>

namespace core::log {
namespace {

std::mutex g_mutex;
std::ofstream g_file;
std::wstring g_path;
constexpr size_t kMaxBytes = 4u * 1024u * 1024u;  // rotate at 4 MB

const char* level_tag(Level l) {
  switch (l) {
    case Level::Debug: return "DBG";
    case Level::Info:  return "INF";
    case Level::Warn:  return "WRN";
    case Level::Error: return "ERR";
  }
  return "???";
}

void rotate_locked() {
  if (!g_file.is_open()) return;
  g_file.close();
  std::wstring bak = g_path + L".1";
  DeleteFileW(bak.c_str());
  MoveFileW(g_path.c_str(), bak.c_str());
  g_file.open(g_path, std::ios::app | std::ios::binary);
}

}  // namespace

void init(const std::wstring& directory) {
  std::lock_guard lock(g_mutex);
  CreateDirectoryW(directory.c_str(), nullptr);
  g_path = directory + L"\\app.log";
  g_file.open(g_path, std::ios::app | std::ios::binary);
}

void shutdown() {
  std::lock_guard lock(g_mutex);
  if (g_file.is_open()) g_file.close();
}

bool is_ready() {
  std::lock_guard lock(g_mutex);
  return g_file.is_open();
}

void write(Level level, const std::string& message) {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char header[96];
  const int n = snprintf(header, sizeof(header),
                         "%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu %s | ",
                         st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                         st.wSecond, st.wMilliseconds,
                         static_cast<unsigned long>(GetCurrentThreadId()),
                         level_tag(level));
  OutputDebugStringA(std::string(header, n) .append(message).append("\n").c_str());

  std::lock_guard lock(g_mutex);
  if (!g_file.is_open()) return;
  if (static_cast<size_t>(g_file.tellp()) > kMaxBytes) rotate_locked();
  g_file.write(header, n);
  g_file.write(message.data(), static_cast<std::streamsize>(message.size()));
  g_file.put('\n');
  g_file.flush();
}

}  // namespace core::log
