#pragma once

#include "ares/core/time.hpp"
#include "ares/hardware/sensor_status.hpp"

#include <cstdint>

namespace ares::flight {

// Flight decision about a sample. This is not the status the sensor reported.
// TimeError means the age could not be computed or the limit is negative.
// Future means the sample timestamp is later than the current ARES time.
enum class SampleUsability : std::uint8_t {
    Usable,
    Stale,
    Invalid,
    Unavailable,
    Future,
    TimeError
};

// TimeError is worse than Future, then Unavailable, Invalid, Stale, Usable.
[[nodiscard]] constexpr SampleUsability worse_usability(SampleUsability left,
                                                        SampleUsability right) noexcept {
    const auto rank = [](SampleUsability value) noexcept {
        switch (value) {
        case SampleUsability::Usable:
            return 0;
        case SampleUsability::Stale:
            return 1;
        case SampleUsability::Invalid:
            return 2;
        case SampleUsability::Unavailable:
            return 3;
        case SampleUsability::Future:
            return 4;
        case SampleUsability::TimeError:
            return 5;
        }
        return 5;
    };
    return rank(left) >= rank(right) ? left : right;
}

// Time is checked before the reported status. A future timestamp is Future
// even when the sensor said Valid. A negative max_age is TimeError.
// If the age does not fit in a duration, the result is TimeError for every
// reported status. Otherwise a non-future Invalid, Stale, or Unavailable
// sample keeps that reported status. Only a Valid sample can become Stale
// because of age, and only when the age is greater than max_age. Age equal
// to max_age is still Usable.
template <typename TimePoint>
[[nodiscard]] constexpr SampleUsability evaluate_freshness(hardware::SensorStatus reported,
                                                           TimePoint sample_time, TimePoint now,
                                                           core::Duration max_age) noexcept {
    if (max_age < core::Duration::zero()) {
        return SampleUsability::TimeError;
    }
    if (sample_time > now) {
        return SampleUsability::Future;
    }
    core::Duration age{};
    if (!core::checked_time_between(now, sample_time, age)) {
        return SampleUsability::TimeError;
    }
    switch (reported) {
    case hardware::SensorStatus::Unavailable:
        return SampleUsability::Unavailable;
    case hardware::SensorStatus::Invalid:
        return SampleUsability::Invalid;
    case hardware::SensorStatus::Stale:
        return SampleUsability::Stale;
    case hardware::SensorStatus::Valid:
        return age > max_age ? SampleUsability::Stale : SampleUsability::Usable;
    }
    return SampleUsability::TimeError;
}

} // namespace ares::flight
