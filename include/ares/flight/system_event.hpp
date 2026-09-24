#pragma once

#include "ares/flight/events.hpp"

#include <variant>

namespace ares::flight {

// Mode and command records only. Cycle and deadline-miss records stay in
// BoundedLog; they are not published through EventLog.
template <typename TimePoint>
using SystemEvent = std::variant<ModeChangedEvent<TimePoint>, ModeTransitionRejected<TimePoint>,
                                 CommandEvent<TimePoint>>;

} // namespace ares::flight
