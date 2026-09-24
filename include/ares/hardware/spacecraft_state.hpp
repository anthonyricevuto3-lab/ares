#pragma once

#include "ares/hardware/samples.hpp"

namespace ares::hardware {

template <typename TimePoint> struct SpacecraftState {
    PositionUm position{};
    VelocityUmps velocity{};
    AttitudeUrad attitude{};
    AngularRateUradps angular_rate{};
    AccelerationUmps2 acceleration{};
    Millivolts voltage{};
    Milliamps current{};
    MilliPercent state_of_charge{};
    Millicelsius temperature{};
    TimePoint time{};
    constexpr bool operator==(const SpacecraftState&) const = default;
};

} // namespace ares::hardware
