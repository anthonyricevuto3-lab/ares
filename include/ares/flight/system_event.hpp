#pragma once

#include "ares/flight/events.hpp"
#include "ares/flight/fault_events.hpp"

#include <variant>

namespace ares::flight {

// Mode, command, and FDIR records. Cycle and deadline-miss records stay in
// BoundedLog; they are not published through EventLog. Fault events are the
// lifecycle edges (activated, persistence crossed, cleared), not every sample.
template <typename TimePoint>
using SystemEvent = std::variant<ModeChangedEvent<TimePoint>, ModeTransitionRejected<TimePoint>,
                                 CommandEvent<TimePoint>, FaultActivatedEvent<TimePoint>,
                                 FaultUpdatedEvent<TimePoint>, FaultClearedEvent<TimePoint>,
                                 RecoveryEvent<TimePoint>>;

} // namespace ares::flight
