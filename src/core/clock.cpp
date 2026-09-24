#include "ares/core/clock.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>

namespace ares::core {

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
ClockSample<SteadyClock::time_point> SteadyClock::now() const {
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    Duration nanos{};
    if (!checked_duration_convert(elapsed, nanos)) {
        return {};
    }
    return ClockSample<time_point>{ClockStatus::Ok, time_point{nanos}};
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
ClockStatus SteadyClock::wait_until(time_point target, const std::stop_token& stop) {
    using Host = std::chrono::steady_clock;
    const Duration stamp = target.time_since_epoch();
    Host::duration host_ticks{};
    if (!checked_duration_convert(stamp, host_ticks)) {
        return ClockStatus::Unrepresentable;
    }
    Duration round_trip{};
    if (!checked_duration_convert(host_ticks, round_trip)) {
        return ClockStatus::Unrepresentable;
    }
    // A coarser host tick truncates the target. Round up by one tick when that
    // tick exists so the wait does not return early. Precision is one host tick.
    if (round_trip < stamp) {
        if (host_ticks >= Host::duration::max()) {
            return ClockStatus::Unrepresentable;
        }
        host_ticks += Host::duration{1};
    }
    const Host::time_point real_target{host_ticks};
    std::mutex mutex;
    std::condition_variable_any cv;
    std::unique_lock lock(mutex);
    cv.wait_until(lock, stop, real_target, [] { return false; });
    return ClockStatus::Ok;
}

ClockSample<ManualClock::time_point> ManualClock::now() const {
    std::lock_guard lock(mutex_);
    return ClockSample<time_point>{ClockStatus::Ok, now_};
}

ClockStatus ManualClock::wait_until(time_point target, const std::stop_token& stop) {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, stop, [&] { return now_ >= target; });
    return ClockStatus::Ok;
}

AdvanceStatus ManualClock::advance(Duration delta) {
    if (delta < Duration::zero()) {
        return AdvanceStatus::Negative;
    }
    std::lock_guard lock(mutex_);
    time_point next{};
    if (!checked_time_add(now_, delta, next)) {
        return AdvanceStatus::Unrepresentable;
    }
    now_ = next;
    cv_.notify_all();
    return AdvanceStatus::Applied;
}

} // namespace ares::core
