#pragma once

#include <chrono>
#include <cstdint>
#include <limits>
#include <ratio>
#include <type_traits>

namespace ares::core {

// Durations have no epoch and may be shared. Time points are tagged so a
// ManualClock reading cannot be passed to SteadyClock, or the reverse.
using Duration = std::chrono::nanoseconds;

struct SteadyEpoch {
    using rep = Duration::rep;
    using period = Duration::period;
    using duration = Duration;
    using time_point = std::chrono::time_point<SteadyEpoch, Duration>;
    static constexpr bool is_steady = true;
};

struct ManualEpoch {
    using rep = Duration::rep;
    using period = Duration::period;
    using duration = Duration;
    using time_point = std::chrono::time_point<ManualEpoch, Duration>;
    static constexpr bool is_steady = true;
};

enum class AdvanceStatus : std::uint8_t { Applied, Negative, Unrepresentable };

// Read and wait result. Unrepresentable matches AdvanceStatus::Unrepresentable:
// the value is left unchanged and the caller must not treat it as a timestamp.
enum class ClockStatus : std::uint8_t { Ok, Unrepresentable };

template <typename TimePoint> struct ClockSample {
    ClockStatus status{ClockStatus::Unrepresentable};
    TimePoint time{};
};

// duration_cast is undefined when the result does not fit. This rejects that
// case instead. Truncation toward zero matches duration_cast when the value fits.
template <typename ToDuration, typename FromDuration>
[[nodiscard]] constexpr bool checked_duration_convert(FromDuration from,
                                                      ToDuration& out) noexcept {
    using FromRep = typename FromDuration::rep;
    using ToRep = typename ToDuration::rep;
    static_assert(std::is_integral_v<FromRep> && std::is_integral_v<ToRep>);
    static_assert(std::is_signed_v<FromRep> && std::is_signed_v<ToRep>);
    static_assert(sizeof(FromRep) <= sizeof(std::intmax_t));
    using Factor = std::ratio_divide<typename FromDuration::period, typename ToDuration::period>;
    constexpr auto num = Factor::num;
    constexpr auto den = Factor::den;
    static_assert(num > 0 && den > 0);

    const auto fits = [](std::intmax_t value) noexcept {
        return value >= static_cast<std::intmax_t>(std::numeric_limits<ToRep>::min()) &&
               value <= static_cast<std::intmax_t>(std::numeric_limits<ToRep>::max());
    };

    std::intmax_t scaled = static_cast<std::intmax_t>(from.count());
    if constexpr (num != 1) {
        if (scaled > 0) {
            if (scaled > std::numeric_limits<std::intmax_t>::max() / num) {
                return false;
            }
        } else if (scaled < 0) {
            if (scaled < std::numeric_limits<std::intmax_t>::min() / num) {
                return false;
            }
        }
        scaled *= num;
    }
    if constexpr (den != 1) {
        scaled /= den;
    }
    if (!fits(scaled)) {
        return false;
    }
    out = ToDuration{static_cast<ToRep>(scaled)};
    return true;
}

// False when base + delta is outside time_point's range. Neither argument is modified.
template <typename TimePoint>
[[nodiscard]] constexpr bool checked_time_add(TimePoint base, Duration delta,
                                              TimePoint& out) noexcept {
    using Dur = typename TimePoint::duration;
    static_assert(std::is_same_v<Dur, Duration>);
    const Dur stamp = base.time_since_epoch();
    if (delta > Dur::zero()) {
        if (stamp > Dur::max() - delta) {
            return false;
        }
    } else if (delta < Dur::zero()) {
        if (stamp < Dur::min() - delta) {
            return false;
        }
    }
    out = TimePoint{stamp + delta};
    return true;
}

// later - earlier. False when the difference is not representable.
template <typename TimePoint>
[[nodiscard]] constexpr bool checked_time_between(TimePoint later, TimePoint earlier,
                                                  Duration& out) noexcept {
    using Dur = typename TimePoint::duration;
    static_assert(std::is_same_v<Dur, Duration>);
    const Dur end = later.time_since_epoch();
    const Dur start = earlier.time_since_epoch();
    if (end >= start) {
        if (start < Dur::zero() && end > Dur::max() + start) {
            return false;
        }
        out = end - start;
        return true;
    }
    if (start > Dur::zero() && end < Dur::min() + start) {
        return false;
    }
    out = end - start;
    return true;
}

} // namespace ares::core
