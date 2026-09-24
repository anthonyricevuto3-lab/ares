#include "ares/core/clock.hpp"
#include "ares/flight/power_manager.hpp"
#include "ares/flight/thermal_monitor.hpp"
#include "ares/hardware/interfaces.hpp"

#include <gtest/gtest.h>

namespace flight = ares::flight;
namespace hardware = ares::hardware;
using Time = ares::core::ManualClock::time_point;

namespace {

class FixedBattery final : public hardware::IBatteryMonitor<Time> {
public:
    explicit FixedBattery(hardware::BatterySample<Time> sample) : sample_(sample) {}
    [[nodiscard]] hardware::BatterySample<Time> read() const override { return sample_; }

private:
    hardware::BatterySample<Time> sample_;
};

class FixedTemperature final : public hardware::ITemperatureSensor<Time> {
public:
    explicit FixedTemperature(hardware::TemperatureSample<Time> sample) : sample_(sample) {}
    [[nodiscard]] hardware::TemperatureSample<Time> read() const override { return sample_; }

private:
    hardware::TemperatureSample<Time> sample_;
};

} // namespace

TEST(PowerManager, ExposesTheBatterySample) {
    hardware::BatterySample<Time> sample;
    sample.voltage = hardware::Millivolts{11100};
    sample.current = hardware::Milliamps{40};
    sample.state_of_charge = hardware::MilliPercent{55000};
    sample.time = Time{};
    sample.status = hardware::SensorStatus::Valid;
    FixedBattery battery(sample);
    flight::PowerManager<Time> power(battery);
    EXPECT_EQ(power.state().status, hardware::SensorStatus::Unavailable);
    EXPECT_EQ(power.sample(), sample);
    EXPECT_EQ(power.state(), sample);
}

TEST(ThermalMonitor, ExposesTheTemperatureSample) {
    hardware::TemperatureSample<Time> sample;
    sample.temperature = hardware::Millicelsius{-5000};
    sample.time = Time{};
    sample.status = hardware::SensorStatus::Invalid;
    FixedTemperature sensor(sample);
    flight::ThermalMonitor<Time> thermal(sensor);
    EXPECT_EQ(thermal.sample().temperature, hardware::Millicelsius{-5000});
    EXPECT_EQ(thermal.state().status, hardware::SensorStatus::Invalid);
}
