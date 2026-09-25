#pragma once

#include "ares/flight/freshness.hpp"
#include "ares/hardware/samples.hpp"

namespace ares::flight {

struct NavigationAgeLimits {
    core::Duration imu{};
    core::Duration gps{};
};

// GPS position and velocity plus IMU acceleration and rate. No filter.
// status is the worse sensor-reported status. usability is the flight decision.
template <typename TimePoint> struct NavigationSolution {
    hardware::PositionUm position{};
    hardware::VelocityUmps velocity{};
    hardware::AccelerationUmps2 acceleration{};
    hardware::AngularRateUradps angular_rate{};
    TimePoint time{};
    hardware::SensorStatus status{hardware::SensorStatus::Unavailable};
    SampleUsability usability{SampleUsability::Unavailable};
    // Per-sensor freshness already computed above. FDIR reads these instead of
    // calling evaluate_freshness again. usability remains the worse of the two.
    SampleUsability imu_usability{SampleUsability::Unavailable};
    SampleUsability gps_usability{SampleUsability::Unavailable};
    constexpr bool operator==(const NavigationSolution&) const = default;
};

template <typename TimePoint>
[[nodiscard]] constexpr NavigationSolution<TimePoint>
combine_navigation(const hardware::ImuSample<TimePoint>& imu,
                   const hardware::GpsSample<TimePoint>& gps, core::ClockStatus now_status,
                   TimePoint now, NavigationAgeLimits limits) noexcept {
    NavigationSolution<TimePoint> solution;
    solution.position = gps.position;
    solution.velocity = gps.velocity;
    solution.acceleration = imu.acceleration;
    solution.angular_rate = imu.angular_rate;
    solution.time = gps.time;
    solution.status = hardware::worse(imu.status, gps.status);
    if (now_status != core::ClockStatus::Ok) {
        solution.usability = SampleUsability::TimeError;
        solution.imu_usability = SampleUsability::TimeError;
        solution.gps_usability = SampleUsability::TimeError;
        return solution;
    }
    solution.imu_usability = evaluate_freshness(imu.status, imu.time, now, limits.imu);
    solution.gps_usability = evaluate_freshness(gps.status, gps.time, now, limits.gps);
    solution.usability = worse_usability(solution.imu_usability, solution.gps_usability);
    return solution;
}

} // namespace ares::flight
