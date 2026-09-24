#include "ares/simulation/sensors.hpp"

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
