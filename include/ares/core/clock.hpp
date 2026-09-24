#pragma once

#include "ares/core/time.hpp"

#include <concepts>
#include <condition_variable>
#include <mutex>
#include <stop_token>

namespace ares::core {

template <typename C>
concept Clock = requires(C& clock, const C& const_clock, typename C::time_point target,
                         const std::stop_token& stop) {
    typename C::time_point;
    { const_clock.now() } -> std::same_as<ClockSample<typename C::time_point>>;
    { clock.wait_until(target, stop) } -> std::same_as<ClockStatus>;
};

// Host monotonic clock. time_point uses SteadyEpoch and is not comparable with
// ManualClock::time_point. now() and wait_until() refuse a conversion that
// does not fit in the destination duration. When the host tick is coarser than
// one nanosecond, a successful wait is rounded up by at most one host tick;
// precision is that tick, not a nanosecond.
class SteadyClock {
public:
    using time_point = SteadyEpoch::time_point;

    SteadyClock() = default;
    SteadyClock(const SteadyClock&) = delete;
    SteadyClock& operator=(const SteadyClock&) = delete;
    SteadyClock(SteadyClock&&) = delete;
    SteadyClock& operator=(SteadyClock&&) = delete;

    [[nodiscard]] ClockSample<time_point> now() const;
    [[nodiscard]] ClockStatus wait_until(time_point target, const std::stop_token& stop);
};

// Test and simulation clock. time_point uses ManualEpoch, starting at zero.
// advance() never sleeps. A negative or unrepresentable step leaves time unchanged.
class ManualClock {
public:
    using time_point = ManualEpoch::time_point;

    ManualClock() = default;
    ManualClock(const ManualClock&) = delete;
    ManualClock& operator=(const ManualClock&) = delete;
    ManualClock(ManualClock&&) = delete;
    ManualClock& operator=(ManualClock&&) = delete;

    [[nodiscard]] ClockSample<time_point> now() const;
    [[nodiscard]] ClockStatus wait_until(time_point target, const std::stop_token& stop);
    [[nodiscard]] AdvanceStatus advance(Duration delta);

private:
    mutable std::mutex mutex_;
    std::condition_variable_any cv_;
    time_point now_{};
};

} // namespace ares::core
