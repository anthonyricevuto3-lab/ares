#pragma once

#include "ares/hardware/samples.hpp"

namespace ares::flight {

// GPS position and velocity plus IMU acceleration and rate. No filter.
template <typename TimePoint> struct NavigationSolution {
    hardware::PositionUm position{};
    hardware::VelocityUmps velocity{};
    hardware::AccelerationUmps2 acceleration{};
    hardware::AngularRateUradps angular_rate{};
    TimePoint time{};
    hardware::SensorStatus status{hardware::SensorStatus::Unavailable};
    constexpr bool operator==(const NavigationSolution&) const = default;
};

template <typename TimePoint>
[[nodiscard]] constexpr NavigationSolution<TimePoint>
combine_navigation(const hardware::ImuSample<TimePoint>& imu,
                   const hardware::GpsSample<TimePoint>& gps) noexcept {
    NavigationSolution<TimePoint> solution;
    solution.position = gps.position;
    solution.velocity = gps.velocity;
    solution.acceleration = imu.acceleration;
    solution.angular_rate = imu.angular_rate;
    solution.time = gps.time;
    solution.status = hardware::worse(imu.status, gps.status);
    return solution;
}

} // namespace ares::flight
