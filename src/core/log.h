#pragma once
// Lightweight logging: file + optional debugger output.
// Log format follows the recovered convention:
//   YYYY-MM-DD HH:MM:SS.mmm pid=<tid> <level> | <message>

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

namespace core::log {

enum class Level { Debug, Info, Warn, Error };

void init(const std::wstring& directory);  // creates dir, opens app.log with rotation
void shutdown();
bool is_ready();

void write(Level level, const std::string& message);

inline void debug(std::string m) { write(Level::Debug, std::move(m)); }
inline void info(std::string m)  { write(Level::Info,  std::move(m)); }
inline void warn(std::string m)  { write(Level::Warn,  std::move(m)); }
inline void error(std::string m) { write(Level::Error, std::move(m)); }

}  // namespace core::log
