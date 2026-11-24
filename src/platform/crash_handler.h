#pragma once
// Crash handling: unhandled-exception filter writing crash-report.log + minidump,
// with a message pointing the user at the log and the dumps directory.

namespace platform {

// Installs SetUnhandledExceptionFilter. Safe to call once at startup.
void install_crash_handler();

// Writes a minidump for the current process (also usable from rules engine
// watchdog "dump" responses). Returns false on failure.
bool write_minidump(const wchar_t* reason);

}  // namespace platform
