#include "ares/simulation/sensors.hpp"

#include <limits>

namespace ares::simulation {
namespace {

template <typename TimePoint> hardware::ImuSample<TimePoint> unavailable_imu() {
    return {};
}

template <typename TimePoint> hardware::GpsSample<TimePoint> unavailable_gps() {
    return {};
}

template <typename TimePoint> hardware::BatterySample<TimePoint> unavailable_battery() {
    return {};
}

template <typename TimePoint> hardware::TemperatureSample<TimePoint> unavailable_temperature() {
    return {};
}

// delta == int64 min is handled apart so the range check itself does not overflow.
[[nodiscard]] bool checked_add(std::int64_t base, std::int64_t delta, std::int64_t& out) noexcept {
    constexpr auto kMin = std::numeric_limits<std::int64_t>::min();
    constexpr auto kMax = std::numeric_limits<std::int64_t>::max();
    if (delta == kMin) {
        if (base < 0) {
            return false;
        }
        out = base + delta;
        return true;
    }
    if (delta > 0) {
        if (base > kMax - delta) {
            return false;
        }
    } else if (delta < 0 && base < kMin - delta) {
        return false;
    }
    out = base + delta;
    return true;
}

[[nodiscard]] bool amplitude_ok(std::int64_t amplitude) noexcept {
    if (amplitude <= 0) {
        return true;
    }
    constexpr auto kMax = std::numeric_limits<std::int64_t>::max();
    return amplitude <= (kMax - 1) / 2;
}

// On overflow the axis saturates. A non-positive amplitude does not draw.
[[nodiscard]] bool perturb(std::int64_t& value, std::int64_t amplitude,
                           DeterministicRng& rng) noexcept {
    std::int64_t offset = 0;
    if (!rng.try_offset(amplitude, offset)) {
        return false;
    }
    std::int64_t sum = 0;
    if (!checked_add(value, offset, sum)) {
        value = offset > 0 ? std::numeric_limits<std::int64_t>::max()
                           : std::numeric_limits<std::int64_t>::min();
        return true;
    }
    value = sum;
    return true;
}

template <typename Axes>
[[nodiscard]] bool perturb_axes(Axes& axes, std::int64_t amplitude,
                                DeterministicRng& rng) noexcept {
    return perturb(axes.x, amplitude, rng) && perturb(axes.y, amplitude, rng) &&
           perturb(axes.z, amplitude, rng);
}

} // namespace

template <core::Clock C>
hardware::ImuSample<typename SimulatedImu<C>::time_point> SimulatedImu<C>::read() const {
    const auto state = model_.state_now();
    if (!state.has_value()) {
        return unavailable_imu<time_point>();
    }
    hardware::ImuSample<time_point> sample;
    sample.acceleration = state->acceleration;
    sample.angular_rate = state->angular_rate;
    sample.time = state->time;
    sample.status = model_.truth().imu;
    if (!amplitude_ok(acceleration_noise_) || !amplitude_ok(rate_noise_)) {
        sample.status = hardware::SensorStatus::Invalid;
        return sample;
    }
    if (!perturb_axes(sample.acceleration, acceleration_noise_, rng_) ||
        !perturb_axes(sample.angular_rate, rate_noise_, rng_)) {
        sample.acceleration = state->acceleration;
        sample.angular_rate = state->angular_rate;
        sample.status = hardware::SensorStatus::Invalid;
    }
    return sample;
}

template <core::Clock C>
hardware::GpsSample<typename SimulatedGps<C>::time_point> SimulatedGps<C>::read() const {
    const auto state = model_.state_now();
    if (!state.has_value()) {
        return unavailable_gps<time_point>();
    }
    hardware::GpsSample<time_point> sample;
    sample.position = state->position;
    sample.velocity = state->velocity;
    sample.time = state->time;
    sample.status = model_.truth().gps;
    if (!amplitude_ok(position_noise_) || !amplitude_ok(velocity_noise_)) {
        sample.status = hardware::SensorStatus::Invalid;
        return sample;
    }
    if (!perturb_axes(sample.position, position_noise_, rng_) ||
        !perturb_axes(sample.velocity, velocity_noise_, rng_)) {
        sample.position = state->position;
        sample.velocity = state->velocity;
        sample.status = hardware::SensorStatus::Invalid;
    }
    return sample;
}

template <core::Clock C>
hardware::BatterySample<typename SimulatedBatteryMonitor<C>::time_point>
SimulatedBatteryMonitor<C>::read() const {
    const auto state = model_.state_now();
    if (!state.has_value()) {
        return unavailable_battery<time_point>();
    }
    hardware::BatterySample<time_point> sample;
    sample.voltage = state->voltage;
    sample.current = state->current;
    sample.state_of_charge = state->state_of_charge;
    sample.time = state->time;
    sample.status = model_.truth().battery;
    if (!amplitude_ok(voltage_noise_) || !amplitude_ok(current_noise_) ||
        !amplitude_ok(state_of_charge_noise_)) {
        sample.status = hardware::SensorStatus::Invalid;
        return sample;
    }
    if (!perturb(sample.voltage.count, voltage_noise_, rng_) ||
        !perturb(sample.current.count, current_noise_, rng_) ||
        !perturb(sample.state_of_charge.count, state_of_charge_noise_, rng_)) {
        sample.voltage = state->voltage;
        sample.current = state->current;
        sample.state_of_charge = state->state_of_charge;
        sample.status = hardware::SensorStatus::Invalid;
    }
    return sample;
}

template <core::Clock C>
hardware::TemperatureSample<typename SimulatedTemperatureSensor<C>::time_point>
SimulatedTemperatureSensor<C>::read() const {
    const auto state = model_.state_now();
    if (!state.has_value()) {
        return unavailable_temperature<time_point>();
    }
    hardware::TemperatureSample<time_point> sample;
    sample.temperature = state->temperature;
    sample.time = state->time;
    sample.status = model_.truth().thermal;
    if (!amplitude_ok(temperature_noise_)) {
        sample.status = hardware::SensorStatus::Invalid;
        return sample;
    }
    if (!perturb(sample.temperature.count, temperature_noise_, rng_)) {
        sample.temperature = state->temperature;
        sample.status = hardware::SensorStatus::Invalid;
    }
    return sample;
}

template class SimulatedImu<core::ManualClock>;
template class SimulatedImu<core::SteadyClock>;
template class SimulatedGps<core::ManualClock>;
template class SimulatedGps<core::SteadyClock>;
template class SimulatedBatteryMonitor<core::ManualClock>;
template class SimulatedBatteryMonitor<core::SteadyClock>;
template class SimulatedTemperatureSensor<core::ManualClock>;
template class SimulatedTemperatureSensor<core::SteadyClock>;

} // namespace ares::simulation
