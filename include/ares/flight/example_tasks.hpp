#pragma once

#include "ares/core/logger.hpp"
#include "ares/flight/navigation.hpp"
#include "ares/hardware/interfaces.hpp"

#include <atomic>
#include <cstdint>
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
                      const hardware::IGps<time_point>& gps)
        : logger_(logger), imu_(&imu), gps_(&gps) {}

    void operator()(typename C::time_point scheduled, std::stop_token stop);
    [[nodiscard]] std::uint64_t cycles() const noexcept {
        return cycles_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t debug_formats() const noexcept {
        return debug_formats_.load(std::memory_order_acquire);
    }
    [[nodiscard]] const NavigationSolution<time_point>& solution() const noexcept {
        return solution_;
    }

private:
    core::Logger<C>& logger_;
    const hardware::IImu<time_point>* imu_{nullptr};
    const hardware::IGps<time_point>* gps_{nullptr};
    NavigationSolution<time_point> solution_{};
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

extern template class HealthPulse<core::ManualClock>;
extern template class HealthPulse<core::SteadyClock>;
extern template class NavigationCadence<core::ManualClock>;
extern template class NavigationCadence<core::SteadyClock>;
extern template class CommBeacon<core::ManualClock>;
extern template class CommBeacon<core::SteadyClock>;

} // namespace ares::flight
