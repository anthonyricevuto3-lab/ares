#include "ares/simulation/spacecraft_model.hpp"

#include <cstdint>
#include <limits>

namespace ares::simulation {
namespace {

constexpr std::int64_t kNanosecondsPerSecond = 1000000000;

// Every int64 factor fits in signed 128-bit, including INT64_MIN * INT64_MIN.
// The product and the sum are therefore formed without wrapping, and saturation
// happens only after the true mathematical result is known.
#if defined(__GNUC__) || defined(__clang__)
__extension__ using Wide = __int128;
#else
#error "ARES integration requires a 128-bit integer"
#endif

[[nodiscard]] std::int64_t saturating_narrow(Wide value) noexcept {
    constexpr Wide kMin = static_cast<Wide>(std::numeric_limits<std::int64_t>::min());
    constexpr Wide kMax = static_cast<Wide>(std::numeric_limits<std::int64_t>::max());
    if (value > kMax) {
        return std::numeric_limits<std::int64_t>::max();
    }
    if (value < kMin) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return static_cast<std::int64_t>(value);
}

// rate * elapsed_ns / 1e9, truncated toward zero. Position and attitude both use this.
[[nodiscard]] std::int64_t integrate(std::int64_t rate_per_second, std::int64_t dt_ns) noexcept {
    if (rate_per_second == 0 || dt_ns == 0) {
        return 0;
    }
    const Wide product = static_cast<Wide>(rate_per_second) * static_cast<Wide>(dt_ns);
    const Wide quotient = product / static_cast<Wide>(kNanosecondsPerSecond);
    return saturating_narrow(quotient);
}

[[nodiscard]] std::int64_t add_saturated(std::int64_t base, std::int64_t delta) noexcept {
    const Wide sum = static_cast<Wide>(base) + static_cast<Wide>(delta);
    return saturating_narrow(sum);
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
    if (sample.time < truth_.epoch) {
        return std::nullopt;
    }
    core::Duration elapsed{0};
    if (!core::checked_time_between(sample.time, truth_.epoch, elapsed)) {
        return std::nullopt;
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
