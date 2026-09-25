#pragma once

#include "ares/hardware/units.hpp"

#include <cstddef>
#include <cstdint>

namespace ares::flight::limits {

// One low-battery pair for this milestone. Activate strictly below the first
// value. Clear strictly above the second. The gap is the hysteresis band.
inline constexpr hardware::Millivolts kLowBatteryActivate{11000};
inline constexpr hardware::Millivolts kLowBatteryClear{11500};

// Consecutive Warning detections required before policy may enter Degraded.
// One detection still records the fault.
inline constexpr std::uint32_t kWarningPersistence{3};

// DeadlineMiss stays one record per task. Its severity follows the current
// miss streak: 1-2 Advisory, 3-4 Warning, 5 and above Critical.
inline constexpr std::uint32_t kDeadlineWarningAfter{3};
inline constexpr std::uint32_t kDeadlineCriticalAfter{5};

// Consecutive healthy FDIR evaluations required before SafeMode may request
// Standby. A blocked evaluation resets the count.
inline constexpr std::uint32_t kSafeModeRecoveryCycles{3};

// Navigation restart executions in one recovery episode, and consecutive
// on-time completions required from the new generation.
inline constexpr std::uint8_t kNavigationRestartAttempts{2};
inline constexpr std::uint8_t kRecoveryVerifyCount{3};

// Five sensor sources times three sensor-health types, plus three task deadlines,
// plus LowBattery. Primary and backup GPS are distinct identities.
inline constexpr std::size_t kFaultRegistryCapacity{19};

} // namespace ares::flight::limits
