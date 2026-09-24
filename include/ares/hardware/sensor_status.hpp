#pragma once

#include <cstdint>

namespace ares::hardware {

// Reported by the sensor. v0.2 does not turn a status into a mode change.
enum class SensorStatus : std::uint8_t { Valid, Invalid, Stale, Unavailable };

// Unavailable is worse than Invalid, which is worse than Stale, which is worse than Valid.
[[nodiscard]] constexpr SensorStatus worse(SensorStatus left, SensorStatus right) noexcept {
    const auto rank = [](SensorStatus status) noexcept {
        switch (status) {
        case SensorStatus::Valid:
            return 0;
        case SensorStatus::Stale:
            return 1;
        case SensorStatus::Invalid:
            return 2;
        case SensorStatus::Unavailable:
            return 3;
        }
        return 3;
    };
    return rank(left) >= rank(right) ? left : right;
}

} // namespace ares::hardware
