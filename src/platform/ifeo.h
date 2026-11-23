#pragma once
// Task Manager replacement via Image File Execution Options (IFEO).
//
// Mechanism (standard Windows IFEO behaviour):
//   HKLM\...\Image File Execution Options\taskmgr.exe
//     Debugger = <path to our exe>
// When the user (or a shortcut) launches taskmgr.exe, Windows instead runs
//   <our exe> "C:\Windows\System32\taskmgr.exe"
// so we can detect replacement mode by the argument, and offer to launch the
// original Task Manager.

#include <string>

namespace platform {

// True if our exe path is currently registered as the taskmgr Debugger.
bool ifeo_is_enabled();

// Writes/clears the IFEO Debugger value. Requires administrator (HKLM).
// Returns false with *error describing why on failure.
bool ifeo_set(bool enable, std::string* error = nullptr);

// True when this process was started by IFEO as the taskmgr replacement
// (i.e. argv contains the path to taskmgr.exe).
bool launched_as_taskmgr_replacement(const wchar_t* cmd_line);

// Starts the real Task Manager (System32\taskmgr.exe).
bool launch_original_task_manager();

// Path to the real taskmgr.exe.
std::wstring original_taskmgr_path();

}  // namespace platform
