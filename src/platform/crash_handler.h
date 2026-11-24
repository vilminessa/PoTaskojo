#pragma once
// Crash handling: unhandled-exception filter writing crash-report.log + minidump,
// with a message pointing the user at the log and the dumps directory.

#include <cstdint>

namespace platform {

// Installs SetUnhandledExceptionFilter. Safe to call once at startup.
void install_crash_handler();

// Minidump of this process; used by the unhandled-exception filter.
bool write_minidump(const wchar_t* reason);

// Dump of another process - a watchdog response has to capture the program it
// fired on, not PoTaskojo itself. |full| asks for the whole address space,
// which can run to gigabytes, so callers gate it. False when the process
// cannot be opened: protected targets and higher-integrity ones refuse.
bool write_process_dump(uint32_t pid, bool full, const wchar_t* reason);

}  // namespace platform
