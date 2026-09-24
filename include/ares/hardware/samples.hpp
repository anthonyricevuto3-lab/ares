#pragma once

#include "ares/hardware/sensor_status.hpp"
#include "ares/hardware/units.hpp"

namespace ares::hardware {

template <typename TimePoint> struct ImuSample {
    AccelerationUmps2 acceleration{};
    AngularRateUradps angular_rate{};
    TimePoint time{};
    SensorStatus status{SensorStatus::Unavailable};
    constexpr bool operator==(const ImuSample&) const = default;
};

template <typename TimePoint> struct GpsSample {
    PositionUm position{};
    VelocityUmps velocity{};
    TimePoint time{};
    SensorStatus status{SensorStatus::Unavailable};
    constexpr bool operator==(const GpsSample&) const = default;
};

template <typename TimePoint> struct BatterySample {
    Millivolts voltage{};
    Milliamps current{};
    MilliPercent state_of_charge{};
    TimePoint time{};
    SensorStatus status{SensorStatus::Unavailable};
    constexpr bool operator==(const BatterySample&) const = default;
};

template <typename TimePoint> struct TemperatureSample {
    Millicelsius temperature{};
    TimePoint time{};
    SensorStatus status{SensorStatus::Unavailable};
    constexpr bool operator==(const TemperatureSample&) const = default;
};

} // namespace ares::hardware
