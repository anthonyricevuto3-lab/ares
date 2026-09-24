#pragma once

#include "ares/core/time.hpp"

#include <cstdint>

namespace ares::core {

enum class DeadlineStatus : std::uint8_t { OnTime, Missed, Unusable };

// OnTime: completion is at or before scheduled + deadline.
// Missed: completion is later than that instant.
// Unusable: the timestamps cannot form an interval, including completion
// before the scheduled release. That is not an on-time result.
// A negative deadline is a miss. Both time points must be the same clock's type.
template <typename TimePoint>
[[nodiscard]] constexpr DeadlineStatus deadline_status(TimePoint scheduled, TimePoint completed,
                                                       Duration deadline) noexcept {
    if (deadline < Duration::zero()) {
        return DeadlineStatus::Missed;
    }
    if (completed < scheduled) {
        return DeadlineStatus::Unusable;
    }
    Duration elapsed{};
    if (!checked_time_between(completed, scheduled, elapsed)) {
        return DeadlineStatus::Unusable;
    }
    return elapsed > deadline ? DeadlineStatus::Missed : DeadlineStatus::OnTime;
}

template <typename TimePoint>
[[nodiscard]] constexpr bool deadline_missed(TimePoint scheduled, TimePoint completed,
                                             Duration deadline) noexcept {
    return deadline_status(scheduled, completed, deadline) == DeadlineStatus::Missed;
}

} // namespace ares::core
