#include "rules/event_log.h"

namespace rules {

void EventLog::add(RuleEvent e) {
  events_.insert(events_.begin(), std::move(e));
  if (events_.size() > capacity_) events_.resize(capacity_);
}

}  // namespace rules
