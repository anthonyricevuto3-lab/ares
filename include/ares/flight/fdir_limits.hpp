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

// Every (sensor source, sensor-health type), plus three deadline sources, plus
// LowBattery, is 16 identities. The flight registry is that size.
inline constexpr std::size_t kFaultRegistryCapacity{16};

} // namespace ares::flight::limits
