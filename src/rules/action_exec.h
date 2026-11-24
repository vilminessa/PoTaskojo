#pragma once
// The process actions the rules engine performs, in one place.
//
// Every action states the rights it needs: TerminateProcess requires
// PROCESS_TERMINATE and NtSuspendProcess requires PROCESS_SUSPEND_RESUME.
// OpenProcess succeeds either way - the failure only shows up at the call
// itself, as a return value nobody was checking. Keeping the handle request
// next to the call is what stops the two from drifting apart again.
//
// The functions are separate from the engine so a test can spawn a throwaway
// child process and prove the rights are right.

#include <windows.h>

#include <cstdint>

namespace rules {

// Rights needed by end / suspend / resume. OpenProcess fails outright if any
// of them cannot be granted, which is the behaviour we want: a partial handle
// would let the call appear to work and do nothing.
inline constexpr DWORD kProcessActionAccess =
    PROCESS_TERMINATE | PROCESS_SUSPEND_RESUME | PROCESS_QUERY_LIMITED_INFORMATION;

// False when the process could not be opened or the operation was refused
// (protected and protected-process-light targets land here).
bool end_process(uint32_t pid);
bool suspend_process(uint32_t pid);
bool resume_process(uint32_t pid);

}  // namespace rules
