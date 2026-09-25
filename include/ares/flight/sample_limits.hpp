#pragma once

#include "ares/core/time.hpp"

#include <chrono>

namespace ares::flight::limits {

// Mission defaults for ares::run. Consumers receive these values. The freshness
// evaluator does not contain its own timing numbers.
// The navigation task period is 100 ms. IMU samples older than two periods are stale.
inline constexpr core::Duration kNavigationImuMaxAge{std::chrono::milliseconds{200}};
// GPS is not on its own task. One second is ten navigation periods.
inline constexpr core::Duration kNavigationGpsMaxAge{std::chrono::seconds{1}};
// The health task period is 200 ms. Power and thermal samples older than two periods are stale.
inline constexpr core::Duration kPowerMaxAge{std::chrono::milliseconds{400}};
inline constexpr core::Duration kThermalMaxAge{std::chrono::milliseconds{400}};

} // namespace ares::flight::limits
