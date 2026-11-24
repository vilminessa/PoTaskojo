#pragma once
// Autorun registration: current-user Run value + StartupApproved toggle (the standard
// Windows mechanism for launching an app at user logon).

namespace platform {

// True if our Run entry exists and is enabled.
bool autorun_is_enabled();
bool autorun_set(bool enable);

}  // namespace platform
