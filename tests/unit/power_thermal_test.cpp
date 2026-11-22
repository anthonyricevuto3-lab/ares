#include "ares/core/clock.hpp"
#include "ares/flight/flight_tasks.hpp"
#include "ares/flight/power_manager.hpp"
#include "ares/flight/thermal_monitor.hpp"
#include "ares/hardware/interfaces.hpp"

#include <chrono>
#include <sstream>
#include <stop_token>

#include <gtest/gtest.h>

namespace flight = ares::flight;
namespace hardware = ares::hardware;
using namespace std::chrono_literals;
using Clock = ares::core::ManualClock;
using Time = Clock::time_point;

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

hardware::BatterySample<Time> battery_at(Time time, hardware::SensorStatus status) {
    hardware::BatterySample<Time> sample;
    sample.voltage = hardware::Millivolts{11100};
    sample.current = hardware::Milliamps{40};
    sample.state_of_charge = hardware::MilliPercent{55000};
    sample.time = time;
    sample.status = status;
    return sample;
}

} // namespace

TEST(PowerManager, ExposesTheBatterySample) {
    Clock clock;
    const auto sample = battery_at(Time{}, hardware::SensorStatus::Valid);
    FixedBattery battery(sample);
    flight::PowerManager<Clock> power(battery, clock, 1s);
    EXPECT_EQ(power.latest_observation().status, hardware::SensorStatus::Unavailable);
    EXPECT_FALSE(power.last_usable().has_value());
    EXPECT_EQ(power.usability(), flight::SampleUsability::Unavailable);
    EXPECT_EQ(power.sample(), sample);
    EXPECT_EQ(power.latest_observation(), sample);
    ASSERT_TRUE(power.last_usable().has_value());
    EXPECT_EQ(*power.last_usable(), sample);
    EXPECT_EQ(power.usability(), flight::SampleUsability::Usable);
}

TEST(PowerFreshness, StaleInvalidUnavailableAndFuture) {
    Clock clock;
    ASSERT_EQ(clock.advance(1s), ares::core::AdvanceStatus::Applied);
    FixedBattery stale(battery_at(Time{}, hardware::SensorStatus::Valid));
    flight::PowerManager<Clock> stale_power(stale, clock, 100ms);
    (void)stale_power.sample();
    EXPECT_EQ(stale_power.latest_observation().status, hardware::SensorStatus::Valid);
    EXPECT_EQ(stale_power.usability(), flight::SampleUsability::Stale);

    Clock now;
    FixedBattery invalid(battery_at(Time{}, hardware::SensorStatus::Invalid));
    flight::PowerManager<Clock> invalid_power(invalid, now, 1s);
    (void)invalid_power.sample();
    EXPECT_EQ(invalid_power.usability(), flight::SampleUsability::Invalid);

    FixedBattery unavailable(battery_at(Time{}, hardware::SensorStatus::Unavailable));
    flight::PowerManager<Clock> unavailable_power(unavailable, now, 1s);
    (void)unavailable_power.sample();
    EXPECT_EQ(unavailable_power.usability(), flight::SampleUsability::Unavailable);

    FixedBattery future(battery_at(Time{5s}, hardware::SensorStatus::Valid));
    flight::PowerManager<Clock> future_power(future, now, 1s);
    (void)future_power.sample();
    EXPECT_EQ(future_power.usability(), flight::SampleUsability::Future);
}

TEST(ThermalMonitor, ExposesTheTemperatureSample) {
    Clock clock;
    hardware::TemperatureSample<Time> sample;
    sample.temperature = hardware::Millicelsius{-5000};
    sample.time = Time{};
    sample.status = hardware::SensorStatus::Invalid;
    FixedTemperature sensor(sample);
    flight::ThermalMonitor<Clock> thermal(sensor, clock, 1s);
    EXPECT_EQ(thermal.sample().temperature, hardware::Millicelsius{-5000});
    EXPECT_EQ(thermal.latest_observation().status, hardware::SensorStatus::Invalid);
    EXPECT_FALSE(thermal.last_usable().has_value());
    EXPECT_EQ(thermal.usability(), flight::SampleUsability::Invalid);
}

TEST(ThermalFreshness, FreshAndStale) {
    Clock clock;
    hardware::TemperatureSample<Time> sample;
    sample.temperature = hardware::Millicelsius{21000};
    sample.time = Time{};
    sample.status = hardware::SensorStatus::Valid;
    FixedTemperature sensor(sample);
    flight::ThermalMonitor<Clock> thermal(sensor, clock, 100ms);
    (void)thermal.sample();
    EXPECT_EQ(thermal.usability(), flight::SampleUsability::Usable);

    ASSERT_EQ(clock.advance(101ms), ares::core::AdvanceStatus::Applied);
    (void)thermal.sample();
    EXPECT_EQ(thermal.latest_observation().status, hardware::SensorStatus::Valid);
    EXPECT_EQ(thermal.usability(), flight::SampleUsability::Stale);
    EXPECT_EQ(thermal.latest_observation().temperature, hardware::Millicelsius{21000});
    ASSERT_TRUE(thermal.last_usable().has_value());
    EXPECT_EQ(thermal.last_usable()->temperature, hardware::Millicelsius{21000});
}

namespace {

class MutableBattery final : public hardware::IBatteryMonitor<Time> {
public:
    hardware::BatterySample<Time> sample{};
    [[nodiscard]] hardware::BatterySample<Time> read() const override { return sample; }
};

class MutableTemperature final : public hardware::ITemperatureSensor<Time> {
public:
    hardware::TemperatureSample<Time> sample{};
    [[nodiscard]] hardware::TemperatureSample<Time> read() const override { return sample; }
};

class CountingBattery final : public hardware::IBatteryMonitor<Time> {
public:
    [[nodiscard]] hardware::BatterySample<Time> read() const override {
        ++reads;
        return {};
    }
    mutable int reads{0};
};

class CountingTemperature final : public hardware::ITemperatureSensor<Time> {
public:
    [[nodiscard]] hardware::TemperatureSample<Time> read() const override {
        ++reads;
        return {};
    }
    mutable int reads{0};
};

class FailAfterFirst {
public:
    using time_point = Time;
    [[nodiscard]] ares::core::ClockSample<time_point> now() const {
        ++calls_;
        if (calls_ > 1) {
            return {};
        }
        return {ares::core::ClockStatus::Ok, Time{}};
    }
    [[nodiscard]] ares::core::ClockStatus wait_until(time_point, const std::stop_token&) {
        return ares::core::ClockStatus::Ok;
    }

private:
    mutable int calls_{0};
};

hardware::BatterySample<Time> battery_value(std::int64_t voltage, Time time,
                                            hardware::SensorStatus status) {
    hardware::BatterySample<Time> sample;
    sample.voltage = hardware::Millivolts{voltage};
    sample.time = time;
    sample.status = status;
    return sample;
}

} // namespace

TEST(PowerFreshness, UnusableSamplesDoNotReplaceLastUsable) {
    Clock clock;
    MutableBattery battery;
    flight::PowerManager<Clock> power(battery, clock, 1s);
    battery.sample = battery_value(1000, Time{}, hardware::SensorStatus::Valid);
    (void)power.sample();
    ASSERT_TRUE(power.last_usable().has_value());
    EXPECT_EQ(power.last_usable()->voltage.count, 1000);

    ASSERT_EQ(clock.advance(2s), ares::core::AdvanceStatus::Applied);
    battery.sample = battery_value(2000, Time{}, hardware::SensorStatus::Valid);
    (void)power.sample();
    EXPECT_EQ(power.usability(), flight::SampleUsability::Stale);
    EXPECT_EQ(power.latest_observation().voltage.count, 2000);
    EXPECT_EQ(power.last_usable()->voltage.count, 1000);

    battery.sample = battery_value(3000, Time{2s}, hardware::SensorStatus::Invalid);
    (void)power.sample();
    EXPECT_EQ(power.usability(), flight::SampleUsability::Invalid);
    EXPECT_EQ(power.latest_observation().voltage.count, 3000);
    EXPECT_EQ(power.last_usable()->voltage.count, 1000);

    battery.sample = battery_value(4000, Time{2s}, hardware::SensorStatus::Unavailable);
    (void)power.sample();
    EXPECT_EQ(power.usability(), flight::SampleUsability::Unavailable);
    EXPECT_EQ(power.latest_observation().voltage.count, 4000);
    EXPECT_EQ(power.last_usable()->voltage.count, 1000);

    battery.sample = battery_value(5000, Time{3s}, hardware::SensorStatus::Valid);
    (void)power.sample();
    EXPECT_EQ(power.usability(), flight::SampleUsability::Future);
    EXPECT_EQ(power.last_usable()->voltage.count, 1000);
}

TEST(ThermalFreshness, UnusableSamplesDoNotReplaceLastUsable) {
    Clock clock;
    MutableTemperature sensor;
    flight::ThermalMonitor<Clock> thermal(sensor, clock, 1s);
    sensor.sample.temperature = hardware::Millicelsius{100};
    sensor.sample.status = hardware::SensorStatus::Valid;
    (void)thermal.sample();
    ASSERT_TRUE(thermal.last_usable().has_value());
    EXPECT_EQ(thermal.last_usable()->temperature.count, 100);

    ASSERT_EQ(clock.advance(2s), ares::core::AdvanceStatus::Applied);
    sensor.sample.temperature = hardware::Millicelsius{200};
    (void)thermal.sample();
    EXPECT_EQ(thermal.usability(), flight::SampleUsability::Stale);
    EXPECT_EQ(thermal.latest_observation().temperature.count, 200);
    EXPECT_EQ(thermal.last_usable()->temperature.count, 100);

    sensor.sample.temperature = hardware::Millicelsius{300};
    sensor.sample.time = Time{2s};
    sensor.sample.status = hardware::SensorStatus::Invalid;
    (void)thermal.sample();
    EXPECT_EQ(thermal.usability(), flight::SampleUsability::Invalid);
    EXPECT_EQ(thermal.last_usable()->temperature.count, 100);

    sensor.sample.temperature = hardware::Millicelsius{400};
    sensor.sample.status = hardware::SensorStatus::Unavailable;
    (void)thermal.sample();
    EXPECT_EQ(thermal.usability(), flight::SampleUsability::Unavailable);
    EXPECT_EQ(thermal.latest_observation().temperature.count, 400);
    EXPECT_EQ(thermal.last_usable()->temperature.count, 100);
}

TEST(PowerFreshness, ClockFailureIsTimeError) {
    FailAfterFirst clock;
    MutableBattery battery;
    flight::PowerManager<FailAfterFirst> power(battery, clock, 1s);
    battery.sample = battery_value(1000, Time{}, hardware::SensorStatus::Valid);
    (void)power.sample();
    EXPECT_EQ(power.usability(), flight::SampleUsability::Usable);
    battery.sample = battery_value(2000, Time{}, hardware::SensorStatus::Valid);
    (void)power.sample();
    EXPECT_EQ(power.usability(), flight::SampleUsability::TimeError);
    EXPECT_EQ(power.latest_observation().voltage.count, 2000);
    ASSERT_TRUE(power.last_usable().has_value());
    EXPECT_EQ(power.last_usable()->voltage.count, 1000);
}

TEST(ThermalFreshness, ClockFailureIsTimeError) {
    FailAfterFirst clock;
    MutableTemperature sensor;
    flight::ThermalMonitor<FailAfterFirst> thermal(sensor, clock, 1s);
    sensor.sample.temperature = hardware::Millicelsius{100};
    sensor.sample.status = hardware::SensorStatus::Valid;
    (void)thermal.sample();
    EXPECT_EQ(thermal.usability(), flight::SampleUsability::Usable);
    sensor.sample.temperature = hardware::Millicelsius{200};
    (void)thermal.sample();
    EXPECT_EQ(thermal.usability(), flight::SampleUsability::TimeError);
    EXPECT_EQ(thermal.latest_observation().temperature.count, 200);
    ASSERT_TRUE(thermal.last_usable().has_value());
    EXPECT_EQ(thermal.last_usable()->temperature.count, 100);
}

TEST(HealthCycle, StopSkipsSensorReads) {
    Clock clock;
    std::ostringstream output;
    ares::core::Logger<Clock> logger(output, clock, ares::core::LogLevel::Info);
    CountingBattery battery;
    CountingTemperature temperature;
    flight::PowerManager<Clock> power(battery, clock, 1s);
    flight::ThermalMonitor<Clock> thermal(temperature, clock, 1s);
    flight::HealthPulse<Clock> health(logger);
    std::stop_source stop;
    stop.request_stop();
    flight::run_health_cycle(power, thermal, health, Time{}, stop.get_token());
    EXPECT_EQ(battery.reads, 0);
    EXPECT_EQ(temperature.reads, 0);
    EXPECT_EQ(health.cycles(), 0U);

    flight::run_health_cycle(power, thermal, health, Time{}, {});
    EXPECT_EQ(battery.reads, 1);
    EXPECT_EQ(temperature.reads, 1);
    EXPECT_EQ(health.cycles(), 1U);
}
