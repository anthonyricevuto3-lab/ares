#pragma once

#include "ares/core/clock.hpp"
#include "ares/hardware/spacecraft_state.hpp"

#include <optional>

namespace ares::simulation {

// Constant-rate truth model. Position and attitude integrate from the epoch
// using the injected clock. There is no orbital propagator and no noise.
template <core::Clock C> struct SimulationTruth {
    using time_point = typename C::time_point;
    time_point epoch{};
    hardware::PositionUm position{};
    hardware::VelocityUmps velocity{};
    hardware::AttitudeUrad attitude{};
    hardware::AngularRateUradps angular_rate{};
    hardware::AccelerationUmps2 acceleration{};
    hardware::Millivolts voltage{12000};
    hardware::Milliamps current{};
    hardware::MilliPercent state_of_charge{100000};
    hardware::Millicelsius temperature{20000};
    hardware::SensorStatus imu{hardware::SensorStatus::Valid};
    hardware::SensorStatus gps{hardware::SensorStatus::Valid};
    hardware::SensorStatus battery{hardware::SensorStatus::Valid};
    hardware::SensorStatus thermal{hardware::SensorStatus::Valid};
};

template <core::Clock C> class SpacecraftModel {
public:
    using time_point = typename C::time_point;

    explicit SpacecraftModel(C& clock);

    void set_truth(const SimulationTruth<C>& truth);
    void set_epoch_now();
    void set_imu_status(hardware::SensorStatus status) noexcept;
    void set_gps_status(hardware::SensorStatus status) noexcept;
    void set_battery_status(hardware::SensorStatus status) noexcept;
    void set_thermal_status(hardware::SensorStatus status) noexcept;

    [[nodiscard]] std::optional<hardware::SpacecraftState<time_point>> state_now() const;
    [[nodiscard]] const SimulationTruth<C>& truth() const noexcept { return truth_; }

private:
    C& clock_;
    SimulationTruth<C> truth_{};
};

extern template class SpacecraftModel<core::ManualClock>;
extern template class SpacecraftModel<core::SteadyClock>;

} // namespace ares::simulation
