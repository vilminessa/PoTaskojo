#pragma once
// Process actions: end / suspend / resume / set priority / open location.
// Safety: refuses Windows-critical and protected processes (spec §5.2 rules
// apply the same restrictions to the rules engine).

#include <cstdint>
#include <string>

namespace collectors {

struct ProcessAction {
  enum Kind { End, Suspend, Resume, SetPriority, OpenLocation, CopyPath };
  Kind kind;
  uint32_t pid = 0;
  int priority_class = 0;  // for SetPriority
  std::wstring image_path;
};

struct ActionResult {
  bool ok = false;
  std::string message;  // human-readable, for status bar / toast
};

// PIDs Task Manager itself refuses to touch (System, smss, csrss...).
bool is_protected_pid(uint32_t pid);

ActionResult execute(const ProcessAction& action);

}  // namespace collectors
