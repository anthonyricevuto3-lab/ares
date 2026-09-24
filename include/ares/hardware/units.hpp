#pragma once

#include <cstdint>

namespace ares::hardware {

// Fixed-point spacecraft units. Counts are exact so tests do not compare floats.
// Position: micrometers. Velocity: micrometers/second.
// Acceleration: micrometers/second^2. Attitude: microradians.
// Angular rate: microradians/second. Voltage: millivolts. Current: milliamps.
// State of charge: milli-percent (100000 = 100%). Temperature: millicelsius.

struct PositionUm {
    std::int64_t x{0};
    std::int64_t y{0};
    std::int64_t z{0};
    constexpr bool operator==(const PositionUm&) const = default;
};

struct VelocityUmps {
    std::int64_t x{0};
    std::int64_t y{0};
    std::int64_t z{0};
    constexpr bool operator==(const VelocityUmps&) const = default;
};

struct AccelerationUmps2 {
    std::int64_t x{0};
    std::int64_t y{0};
    std::int64_t z{0};
    constexpr bool operator==(const AccelerationUmps2&) const = default;
};

struct AttitudeUrad {
    std::int64_t x{0};
    std::int64_t y{0};
    std::int64_t z{0};
    constexpr bool operator==(const AttitudeUrad&) const = default;
};

struct AngularRateUradps {
    std::int64_t x{0};
    std::int64_t y{0};
    std::int64_t z{0};
    constexpr bool operator==(const AngularRateUradps&) const = default;
};

struct Millivolts {
    std::int64_t count{0};
    constexpr bool operator==(const Millivolts&) const = default;
};

struct Milliamps {
    std::int64_t count{0};
    constexpr bool operator==(const Milliamps&) const = default;
};

struct MilliPercent {
    std::int64_t count{0};
    constexpr bool operator==(const MilliPercent&) const = default;
};

struct Millicelsius {
    std::int64_t count{0};
    constexpr bool operator==(const Millicelsius&) const = default;
};

} // namespace ares::hardware
