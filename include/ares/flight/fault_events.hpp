#pragma once

#include "ares/flight/fault.hpp"

#include <cstdint>

namespace ares::flight {

// Subsystem recovery. Distinct from RecoveryAction, which is a mode recommendation.
enum class RecoveryState : std::uint8_t {
    Idle,
    Requested,
    Executing,
    Verifying,
    Succeeded,
    Failed
};

enum class SubsystemAction : std::uint8_t { RestartTask, SwitchSensor };

enum class RecoveryTarget : std::uint8_t { NavigationTask, PrimaryGps, BackupGps };

// Which edge a RecoveryEvent is. Started is the default so existing aggregates
// that do not name it stay start edges.
enum class RecoveryNotice : std::uint8_t { Started, Succeeded, Failed };

// Published when a logical fault becomes active, including reactivation.
// Not published again on a later detection of the same active fault.
template <typename TimePoint> struct FaultActivatedEvent {
    FaultType type{};
    FaultSource source{};
    FaultSeverity severity{};
    TimePoint time{};

    bool operator==(const FaultActivatedEvent&) const = default;
};

// Published when an active warning first reaches the persistence limit, and
// when a DeadlineMiss record changes severity. Repeats inside the same
// severity, and repeats past the warning limit, are not events.
template <typename TimePoint> struct FaultUpdatedEvent {
    FaultType type{};
    FaultSource source{};
    FaultSeverity severity{};
    std::uint32_t consecutive_count{0};
    TimePoint time{};

    bool operator==(const FaultUpdatedEvent&) const = default;
};

// Published when a recovery attempt starts, succeeds, or fails. notice says which.
template <typename TimePoint> struct RecoveryEvent {
    SubsystemAction action{SubsystemAction::RestartTask};
    RecoveryTarget target{RecoveryTarget::NavigationTask};
    std::uint8_t attempt{0};
    std::uint32_t generation{0};
    TimePoint time{};
    RecoveryNotice notice{RecoveryNotice::Started};

    bool operator==(const RecoveryEvent&) const = default;
};

// Published when an active fault becomes inactive. The registry keeps the record.
template <typename TimePoint> struct FaultClearedEvent {
    FaultType type{};
    FaultSource source{};
    TimePoint time{};

    bool operator==(const FaultClearedEvent&) const = default;
};

} // namespace ares::flight
