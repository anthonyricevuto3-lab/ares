#pragma once

#include "ares/flight/fault.hpp"

#include <cstdint>

namespace ares::flight {

// Published when a logical fault becomes active, including reactivation.
// Not published again on a later detection of the same active fault.
template <typename TimePoint> struct FaultActivatedEvent {
    FaultType type{};
    FaultSource source{};
    FaultSeverity severity{};
    TimePoint time{};

    bool operator==(const FaultActivatedEvent&) const = default;
};

// Published once, when an active Warning fault's consecutive count first
// reaches the persistence limit. Ordinary repeats are not events.
template <typename TimePoint> struct FaultUpdatedEvent {
    FaultType type{};
    FaultSource source{};
    FaultSeverity severity{};
    std::uint32_t consecutive_count{0};
    TimePoint time{};

    bool operator==(const FaultUpdatedEvent&) const = default;
};

// Published when an active fault becomes inactive. The registry keeps the record.
template <typename TimePoint> struct FaultClearedEvent {
    FaultType type{};
    FaultSource source{};
    TimePoint time{};

    bool operator==(const FaultClearedEvent&) const = default;
};

} // namespace ares::flight
