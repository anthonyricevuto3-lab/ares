#pragma once

#include "ares/hardware/interfaces.hpp"
#include "ares/simulation/spacecraft_model.hpp"

namespace ares::simulation {

template <core::Clock C> class SimulatedImu final : public hardware::IImu<typename C::time_point> {
public:
    using time_point = typename C::time_point;
    explicit SimulatedImu(const SpacecraftModel<C>& model) : model_(model) {}
    [[nodiscard]] hardware::ImuSample<time_point> read() const override;

private:
    const SpacecraftModel<C>& model_;
};

template <core::Clock C> class SimulatedGps final : public hardware::IGps<typename C::time_point> {
public:
    using time_point = typename C::time_point;
    explicit SimulatedGps(const SpacecraftModel<C>& model) : model_(model) {}
    [[nodiscard]] hardware::GpsSample<time_point> read() const override;

private:
    const SpacecraftModel<C>& model_;
};

template <core::Clock C>
class SimulatedBatteryMonitor final : public hardware::IBatteryMonitor<typename C::time_point> {
public:
    using time_point = typename C::time_point;
    explicit SimulatedBatteryMonitor(const SpacecraftModel<C>& model) : model_(model) {}
    [[nodiscard]] hardware::BatterySample<time_point> read() const override;

private:
    const SpacecraftModel<C>& model_;
};

template <core::Clock C>
class SimulatedTemperatureSensor final
    : public hardware::ITemperatureSensor<typename C::time_point> {
public:
    using time_point = typename C::time_point;
    explicit SimulatedTemperatureSensor(const SpacecraftModel<C>& model) : model_(model) {}
    [[nodiscard]] hardware::TemperatureSample<time_point> read() const override;

private:
    const SpacecraftModel<C>& model_;
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
