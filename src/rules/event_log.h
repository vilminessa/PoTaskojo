#pragma once
// A bounded journal of what the monitors actually did.
//
// Kept apart from the engine so the ordering and the size limit can be tested
// without a live process: the panel shows it newest-first, and a monitor that
// fires in a loop must not grow the list without end.

#include <cstddef>
#include <string>
#include <vector>

namespace rules {

// One entry as the panel shows it: when, which program, what came of it.
struct RuleEvent {
  std::string time;     // HH:MM:SS, wall clock at the moment it fired
  std::string program;  // the program the rule matched
  std::string outcome;  // e.g. "suspended", "minidump written"
};

class EventLog {
 public:
  static constexpr size_t kDefaultCapacity = 200;

  explicit EventLog(size_t capacity = kDefaultCapacity)
      : capacity_(capacity == 0 ? 1 : capacity) {}

  // Inserted at the front, so index 0 is the most recent and the oldest fall
  // off the end once the capacity is reached.
  void add(RuleEvent e);

  // Newest first. Only the snapshot feed writes and it does so before the UI
  // reads, so a reference is safe and avoids copying the list every frame.
  const std::vector<RuleEvent>& all() const { return events_; }

  size_t size() const { return events_.size(); }
  void clear() { events_.clear(); }

 private:
  size_t capacity_;
  std::vector<RuleEvent> events_;
};

}  // namespace rules
