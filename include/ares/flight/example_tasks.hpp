#pragma once

#include "ares/core/logger.hpp"
#include "ares/flight/gps_selector.hpp"
#include "ares/flight/navigation.hpp"
#include "ares/flight/power_manager.hpp"
#include "ares/flight/thermal_monitor.hpp"
#include "ares/hardware/interfaces.hpp"

#include <atomic>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string_view>

namespace ares::flight {

template <core::Clock C> class HealthPulse {
public:
    static constexpr std::string_view name{"health"};
    static constexpr std::string_view action{"pulse"};

    explicit HealthPulse(core::Logger<C>& logger) : logger_(logger) {}

    void operator()(typename C::time_point scheduled, std::stop_token stop);
    [[nodiscard]] std::uint64_t cycles() const noexcept {
        return cycles_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t debug_formats() const noexcept {
        return debug_formats_.load(std::memory_order_acquire);
    }

private:
    core::Logger<C>& logger_;
    std::atomic<std::uint64_t> cycles_{0};
    std::atomic<std::uint64_t> debug_formats_{0};
};

template <core::Clock C> class NavigationCadence {
public:
    using time_point = typename C::time_point;
    static constexpr std::string_view name{"navigation"};
    static constexpr std::string_view action{"cadence"};

    explicit NavigationCadence(core::Logger<C>& logger) : logger_(logger) {}
    NavigationCadence(core::Logger<C>& logger, const hardware::IImu<time_point>& imu,
                      const hardware::IGps<time_point>& gps, C& clock, NavigationAgeLimits limits)
        : logger_(logger), imu_(&imu), gps_(&gps), clock_(&clock), limits_(limits) {}
    NavigationCadence(core::Logger<C>& logger, const hardware::IImu<time_point>& imu,
                      const GpsSelector<C>& gps, C& clock, NavigationAgeLimits limits,
                      const std::atomic<std::uint32_t>* generation = nullptr)
        : logger_(logger), imu_(&imu), selector_(&gps), generation_(generation), clock_(&clock),
          limits_(limits) {}

    void operator()(typename C::time_point scheduled, std::stop_token stop);
    [[nodiscard]] std::uint64_t cycles() const noexcept {
        return cycles_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t debug_formats() const noexcept {
        return debug_formats_.load(std::memory_order_acquire);
    }
    // Latest combination, including a result that is not usable.
    [[nodiscard]] const NavigationSolution<time_point>& solution() const noexcept {
        return solution_;
    }
    // Last combination whose usability was Usable. SensorStatus is not acceptance.
    [[nodiscard]] const std::optional<NavigationSolution<time_point>>&
    last_usable() const noexcept {
        return last_usable_;
    }
    [[nodiscard]] const hardware::GpsSample<time_point>& primary_gps() const noexcept {
        return primary_gps_;
    }
    [[nodiscard]] const hardware::GpsSample<time_point>& backup_gps() const noexcept {
        return backup_gps_;
    }

private:
    core::Logger<C>& logger_;
    const hardware::IImu<time_point>* imu_{nullptr};
    const hardware::IGps<time_point>* gps_{nullptr};
    const GpsSelector<C>* selector_{nullptr};
    const std::atomic<std::uint32_t>* generation_{nullptr};
    std::uint32_t seen_generation_{0};
    C* clock_{nullptr};
    NavigationAgeLimits limits_{};
    NavigationSolution<time_point> solution_{};
    std::optional<NavigationSolution<time_point>> last_usable_{};
    hardware::GpsSample<time_point> primary_gps_{};
    hardware::GpsSample<time_point> backup_gps_{};
    std::atomic<std::uint64_t> cycles_{0};
    std::atomic<std::uint64_t> debug_formats_{0};
};

template <core::Clock C> class CommBeacon {
public:
    static constexpr std::string_view name{"comms"};
    static constexpr std::string_view action{"beacon"};

    explicit CommBeacon(core::Logger<C>& logger) : logger_(logger) {}

    void operator()(typename C::time_point scheduled, std::stop_token stop);
    [[nodiscard]] std::uint64_t cycles() const noexcept {
        return cycles_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t debug_formats() const noexcept {
        return debug_formats_.load(std::memory_order_acquire);
    }

private:
    core::Logger<C>& logger_;
    std::atomic<std::uint64_t> cycles_{0};
    std::atomic<std::uint64_t> debug_formats_{0};
};

// Returns before reading sensors when stop is already requested, so shutdown does not
// advance the battery or temperature streams.
template <core::Clock C>
void run_health_cycle(PowerManager<C>& power, ThermalMonitor<C>& thermal, HealthPulse<C>& health,
                      typename C::time_point scheduled, std::stop_token stop) {
    if (stop.stop_requested()) {
        return;
    }
    (void)power.sample();
    (void)thermal.sample();
    health(scheduled, stop);
}

extern template class HealthPulse<core::ManualClock>;
extern template class HealthPulse<core::SteadyClock>;
extern template class NavigationCadence<core::ManualClock>;
extern template class NavigationCadence<core::SteadyClock>;
extern template class CommBeacon<core::ManualClock>;
extern template class CommBeacon<core::SteadyClock>;

} // namespace ares::flight
