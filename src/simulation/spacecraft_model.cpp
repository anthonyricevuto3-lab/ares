#include "ares/simulation/spacecraft_model.hpp"

#include <cstdint>
#include <limits>

namespace ares::simulation {
namespace {

constexpr std::int64_t kNanosecondsPerSecond = 1000000000;

[[nodiscard]] std::int64_t integrate(std::int64_t rate_per_second, std::int64_t dt_ns) noexcept {
    if (rate_per_second == 0 || dt_ns == 0) {
        return 0;
    }
    constexpr auto kMin = std::numeric_limits<std::int64_t>::min();
    constexpr auto kMax = std::numeric_limits<std::int64_t>::max();
    if (rate_per_second == kMin || dt_ns == kMin) {
        return ((rate_per_second < 0) != (dt_ns < 0)) ? kMin : kMax;
    }
    const bool negative = (rate_per_second < 0) != (dt_ns < 0);
    const auto magnitude_rate =
        static_cast<std::uint64_t>(rate_per_second < 0 ? -rate_per_second : rate_per_second);
    const auto magnitude_dt = static_cast<std::uint64_t>(dt_ns < 0 ? -dt_ns : dt_ns);
    const auto whole = magnitude_rate / static_cast<std::uint64_t>(kNanosecondsPerSecond);
    const auto remainder = magnitude_rate % static_cast<std::uint64_t>(kNanosecondsPerSecond);
    const auto high = whole * magnitude_dt;
    const auto low = (remainder * magnitude_dt) / static_cast<std::uint64_t>(kNanosecondsPerSecond);
    const auto sum = high + low;
    const auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (sum > limit) {
        return negative ? std::numeric_limits<std::int64_t>::min()
                        : std::numeric_limits<std::int64_t>::max();
    }
    const auto magnitude = static_cast<std::int64_t>(sum);
    return negative ? -magnitude : magnitude;
}

[[nodiscard]] std::int64_t add_saturated(std::int64_t base, std::int64_t delta) noexcept {
    if (delta > 0 && base > std::numeric_limits<std::int64_t>::max() - delta) {
        return std::numeric_limits<std::int64_t>::max();
    }
    if (delta < 0 && base < std::numeric_limits<std::int64_t>::min() - delta) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return base + delta;
}

} // namespace

template <core::Clock C> SpacecraftModel<C>::SpacecraftModel(C& clock) : clock_(clock) {}

template <core::Clock C> void SpacecraftModel<C>::set_truth(const SimulationTruth<C>& truth) {
    truth_ = truth;
}

template <core::Clock C> void SpacecraftModel<C>::set_epoch_now() {
    const core::ClockSample<time_point> sample = clock_.now();
    if (sample.status == core::ClockStatus::Ok) {
        truth_.epoch = sample.time;
    }
}

template <core::Clock C>
void SpacecraftModel<C>::set_imu_status(hardware::SensorStatus status) noexcept {
    truth_.imu = status;
}

template <core::Clock C>
void SpacecraftModel<C>::set_gps_status(hardware::SensorStatus status) noexcept {
    truth_.gps = status;
}

template <core::Clock C>
void SpacecraftModel<C>::set_battery_status(hardware::SensorStatus status) noexcept {
    truth_.battery = status;
}

template <core::Clock C>
void SpacecraftModel<C>::set_thermal_status(hardware::SensorStatus status) noexcept {
    truth_.thermal = status;
}

template <core::Clock C>
std::optional<hardware::SpacecraftState<typename SpacecraftModel<C>::time_point>>
SpacecraftModel<C>::state_now() const {
    const core::ClockSample<time_point> sample = clock_.now();
    if (sample.status != core::ClockStatus::Ok) {
        return std::nullopt;
    }
    core::Duration elapsed{0};
    if (sample.time >= truth_.epoch) {
        if (!core::checked_time_between(sample.time, truth_.epoch, elapsed)) {
            return std::nullopt;
        }
    }
    const auto dt = elapsed.count();
    hardware::SpacecraftState<time_point> state;
    state.time = sample.time;
    state.velocity = truth_.velocity;
    state.angular_rate = truth_.angular_rate;
    state.acceleration = truth_.acceleration;
    state.voltage = truth_.voltage;
    state.current = truth_.current;
    state.state_of_charge = truth_.state_of_charge;
    state.temperature = truth_.temperature;
    state.position.x = add_saturated(truth_.position.x, integrate(truth_.velocity.x, dt));
    state.position.y = add_saturated(truth_.position.y, integrate(truth_.velocity.y, dt));
    state.position.z = add_saturated(truth_.position.z, integrate(truth_.velocity.z, dt));
    state.attitude.x = add_saturated(truth_.attitude.x, integrate(truth_.angular_rate.x, dt));
    state.attitude.y = add_saturated(truth_.attitude.y, integrate(truth_.angular_rate.y, dt));
    state.attitude.z = add_saturated(truth_.attitude.z, integrate(truth_.angular_rate.z, dt));
    return state;
}

template class SpacecraftModel<core::ManualClock>;
template class SpacecraftModel<core::SteadyClock>;

} // namespace ares::simulation
