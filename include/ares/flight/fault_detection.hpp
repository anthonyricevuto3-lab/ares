#pragma once

#include "ares/flight/fault.hpp"
#include "ares/flight/fdir_limits.hpp"
#include "ares/flight/freshness.hpp"
#include "ares/hardware/units.hpp"

#include <cstdint>

namespace ares::flight {

// Detection only. These functions do not read the registry and do not choose a mode.
enum class SensorDetectionAction : std::uint8_t { Raise, Clear, Hold };

struct SensorDetection {
    SensorDetectionAction action{SensorDetectionAction::Hold};
    FaultType type{FaultType::SensorUnavailable};
};

struct BatteryThresholds {
    hardware::Millivolts activate_below{limits::kLowBatteryActivate};
    hardware::Millivolts clear_above{limits::kLowBatteryClear};
};

enum class BatteryCommand : std::uint8_t { Activate, Clear, Hold };

enum class DeadlineFact : std::uint8_t { OnTime, Missed, Unusable };

enum class DeadlineCommand : std::uint8_t { Raise, Clear, Hold };

// Usable clears. Unavailable, Invalid, and Stale raise the matching fault.
// Future and TimeError hold: they are not SensorStale, and they are not recovery.
[[nodiscard]] constexpr SensorDetection detect_sensor(SampleUsability usability) noexcept {
    switch (usability) {
    case SampleUsability::Usable:
        return SensorDetection{SensorDetectionAction::Clear, FaultType::SensorUnavailable};
    case SampleUsability::Unavailable:
        return SensorDetection{SensorDetectionAction::Raise, FaultType::SensorUnavailable};
    case SampleUsability::Invalid:
        return SensorDetection{SensorDetectionAction::Raise, FaultType::SensorInvalid};
    case SampleUsability::Stale:
        return SensorDetection{SensorDetectionAction::Raise, FaultType::SensorStale};
    case SampleUsability::Future:
    case SampleUsability::TimeError:
        return SensorDetection{SensorDetectionAction::Hold, FaultType::SensorUnavailable};
    }
    return SensorDetection{SensorDetectionAction::Hold, FaultType::SensorUnavailable};
}

// Unusable samples do not activate or clear LowBattery. The voltage is ignored.
// Activate when the reading is strictly below activate_below.
// Clear when it is strictly above clear_above. If clear_above is not greater
// than activate_below, the clear line is activate_below.
[[nodiscard]] constexpr BatteryCommand detect_low_battery(SampleUsability usability,
                                                          hardware::Millivolts voltage,
                                                          BatteryThresholds thresholds) noexcept {
    if (usability != SampleUsability::Usable) {
        return BatteryCommand::Hold;
    }
    const std::int64_t reading = voltage.count;
    const std::int64_t activate = thresholds.activate_below.count;
    const std::int64_t configured_clear = thresholds.clear_above.count;
    const std::int64_t clear_line = configured_clear > activate ? configured_clear : activate;
    if (reading < activate) {
        return BatteryCommand::Activate;
    }
    if (reading > clear_line) {
        return BatteryCommand::Clear;
    }
    return BatteryCommand::Hold;
}

// Initial DeadlineMiss severity is Advisory. The live record uses this instead
// once the streak is known. Counts below the warning line stay Advisory.
[[nodiscard]] constexpr FaultSeverity deadline_severity(std::uint32_t consecutive) noexcept {
    if (consecutive >= limits::kDeadlineCriticalAfter) {
        return FaultSeverity::Critical;
    }
    if (consecutive >= limits::kDeadlineWarningAfter) {
        return FaultSeverity::Warning;
    }
    return FaultSeverity::Advisory;
}

[[nodiscard]] constexpr DeadlineCommand detect_deadline(DeadlineFact fact) noexcept {
    switch (fact) {
    case DeadlineFact::Missed:
        return DeadlineCommand::Raise;
    case DeadlineFact::OnTime:
        return DeadlineCommand::Clear;
    case DeadlineFact::Unusable:
        return DeadlineCommand::Hold;
    }
    return DeadlineCommand::Hold;
}

} // namespace ares::flight
