#pragma once

#include <cstdint>
#include <limits>

namespace ares::simulation {

// SplitMix64 increment. Also the per-sensor seed salt.
inline constexpr std::uint64_t kStreamSalt = 0x9E3779B97F4A7C15ULL;

// SplitMix64. One 64-bit state, no heap, no std::random_device.
// next() adds kStreamSalt, then applies the SplitMix64 finalizer.
class DeterministicRng {
public:
    explicit DeterministicRng(std::uint64_t seed) noexcept : state_(seed) {}

    [[nodiscard]] std::uint64_t next() noexcept {
        state_ += ares::simulation::kStreamSalt;
        std::uint64_t mixed = state_;
        mixed = (mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9ULL;
        mixed = (mixed ^ (mixed >> 27)) * 0x94D049BB133111EBULL;
        return mixed ^ (mixed >> 31);
    }

    // Inclusive offset in [-amplitude, +amplitude].
    // A non-positive amplitude returns 0 and does not draw.
    // An amplitude too large to form that span returns false and does not draw.
    [[nodiscard]] bool try_offset(std::int64_t amplitude, std::int64_t& offset) noexcept {
        if (amplitude <= 0) {
            offset = 0;
            return true;
        }
        constexpr auto kMax = std::numeric_limits<std::int64_t>::max();
        if (amplitude > (kMax - 1) / 2) {
            return false;
        }
        const auto span = (static_cast<std::uint64_t>(amplitude) * 2ULL) + 1ULL;
        const auto pick = next() % span;
        offset = static_cast<std::int64_t>(pick) - amplitude;
        return true;
    }

private:
    std::uint64_t state_;
};

// One stream per simulated device. Later draws stay inside that device.
enum class SensorStream : std::uint8_t {
    Imu = 0,
    Gps = 1,
    Battery = 2,
    Temperature = 3,
    BackupGps = 4
};

// stream_seed = SplitMix64(mission_seed + (stream_index + 1) * kStreamSalt).next()
// Unsigned addition and multiplication wrap modulo 2^64. The salt is the
// SplitMix64 increment, 0x9E3779B97F4A7C15. Copy the result into a fresh
// DeterministicRng. Do not share that object across sensors.
[[nodiscard]] inline std::uint64_t derive_sensor_seed(std::uint64_t mission_seed,
                                                      SensorStream stream) noexcept {
    const auto channel = static_cast<std::uint64_t>(stream) + 1ULL;
    DeterministicRng mixer{mission_seed + (channel * kStreamSalt)};
    return mixer.next();
}

// Amplitudes are absolute bounds in the same integer unit as the measurement.
// Zero or negative means that channel draws nothing and adds nothing.
struct SensorNoise {
    std::uint64_t mission_seed{0};
    std::int64_t acceleration{0};
    std::int64_t angular_rate{0};
    std::int64_t position{0};
    std::int64_t velocity{0};
    std::int64_t voltage{0};
    std::int64_t current{0};
    std::int64_t state_of_charge{0};
    std::int64_t temperature{0};
};

} // namespace ares::simulation
