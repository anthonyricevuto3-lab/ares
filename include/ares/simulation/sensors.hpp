#pragma once

#include "ares/hardware/interfaces.hpp"
#include "ares/simulation/random_source.hpp"
#include "ares/simulation/spacecraft_model.hpp"

#include <cstdint>
#include <optional>

namespace ares::simulation {

template <core::Clock C> class ChaosEngine;

// Zero amplitudes reproduce the noise-free v0.2 sample. A positive amplitude
// draws one bounded offset per axis, in x, y, z order, from this sensor's stream.
// The stream state is mutable so read() can stay const on the hardware interface.
// Each simulated sensor instance has exactly one flight-task reader for its lifetime.
// Concurrent read() calls on one instance are a data race. The stream is not locked.

template <core::Clock C> class SimulatedImu final : public hardware::IImu<typename C::time_point> {
public:
    using time_point = typename C::time_point;
    explicit SimulatedImu(const SpacecraftModel<C>& model, SensorNoise noise = {},
                          const ChaosEngine<C>* chaos = nullptr)
        : model_(model), rng_(derive_sensor_seed(noise.mission_seed, SensorStream::Imu)),
          acceleration_noise_(noise.acceleration), rate_noise_(noise.angular_rate), chaos_(chaos) {}
    [[nodiscard]] hardware::ImuSample<time_point> read() const override;

private:
    const SpacecraftModel<C>& model_;
    mutable DeterministicRng rng_;
    std::int64_t acceleration_noise_;
    std::int64_t rate_noise_;
    const ChaosEngine<C>* chaos_{nullptr};
    mutable std::optional<hardware::ImuSample<time_point>> frozen_{};
    mutable std::optional<std::uint16_t> frozen_id_{};
};

template <core::Clock C> class SimulatedGps final : public hardware::IGps<typename C::time_point> {
public:
    using time_point = typename C::time_point;
    explicit SimulatedGps(const SpacecraftModel<C>& model, SensorNoise noise = {},
                          const ChaosEngine<C>* chaos = nullptr)
        : model_(model), rng_(derive_sensor_seed(noise.mission_seed, SensorStream::Gps)),
          position_noise_(noise.position), velocity_noise_(noise.velocity), chaos_(chaos) {}
    [[nodiscard]] hardware::GpsSample<time_point> read() const override;

private:
    const SpacecraftModel<C>& model_;
    mutable DeterministicRng rng_;
    std::int64_t position_noise_;
    std::int64_t velocity_noise_;
    const ChaosEngine<C>* chaos_{nullptr};
    mutable std::optional<hardware::GpsSample<time_point>> frozen_{};
    mutable std::optional<std::uint16_t> frozen_id_{};
};

template <core::Clock C>
class SimulatedBatteryMonitor final : public hardware::IBatteryMonitor<typename C::time_point> {
public:
    using time_point = typename C::time_point;
    explicit SimulatedBatteryMonitor(const SpacecraftModel<C>& model, SensorNoise noise = {},
                                     const ChaosEngine<C>* chaos = nullptr)
        : model_(model), rng_(derive_sensor_seed(noise.mission_seed, SensorStream::Battery)),
          voltage_noise_(noise.voltage), current_noise_(noise.current),
          state_of_charge_noise_(noise.state_of_charge), chaos_(chaos) {}
    [[nodiscard]] hardware::BatterySample<time_point> read() const override;

private:
    const SpacecraftModel<C>& model_;
    mutable DeterministicRng rng_;
    std::int64_t voltage_noise_;
    std::int64_t current_noise_;
    std::int64_t state_of_charge_noise_;
    const ChaosEngine<C>* chaos_{nullptr};
};

template <core::Clock C>
class SimulatedTemperatureSensor final
    : public hardware::ITemperatureSensor<typename C::time_point> {
public:
    using time_point = typename C::time_point;
    explicit SimulatedTemperatureSensor(const SpacecraftModel<C>& model, SensorNoise noise = {})
        : model_(model), rng_(derive_sensor_seed(noise.mission_seed, SensorStream::Temperature)),
          temperature_noise_(noise.temperature) {}
    [[nodiscard]] hardware::TemperatureSample<time_point> read() const override;

private:
    const SpacecraftModel<C>& model_;
    mutable DeterministicRng rng_;
    std::int64_t temperature_noise_;
};

extern template class SimulatedImu<core::ManualClock>;
extern template class SimulatedImu<core::SteadyClock>;
extern template class SimulatedGps<core::ManualClock>;
extern template class SimulatedGps<core::SteadyClock>;
extern template class SimulatedBatteryMonitor<core::ManualClock>;
extern template class SimulatedBatteryMonitor<core::SteadyClock>;
extern template class SimulatedTemperatureSensor<core::ManualClock>;
extern template class SimulatedTemperatureSensor<core::SteadyClock>;

} // namespace ares::simulation
