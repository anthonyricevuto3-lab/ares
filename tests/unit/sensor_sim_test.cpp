#include "ares/simulation/sensors.hpp"

#include <chrono>

#include <gtest/gtest.h>

namespace hardware = ares::hardware;
namespace simulation = ares::simulation;
using namespace std::chrono_literals;
using Clock = ares::core::ManualClock;
using Time = Clock::time_point;

namespace {

simulation::SimulationTruth<Clock> truth_at_origin() {
    simulation::SimulationTruth<Clock> truth;
    truth.epoch = Time{};
    truth.position = hardware::PositionUm{0, 0, 0};
    truth.velocity = hardware::VelocityUmps{1000, 0, 0};
    truth.attitude = hardware::AttitudeUrad{0, 0, 0};
    truth.angular_rate = hardware::AngularRateUradps{0, 250, 0};
    truth.acceleration = hardware::AccelerationUmps2{0, 0, -9806650};
    truth.voltage = hardware::Millivolts{12000};
    truth.current = hardware::Milliamps{150};
    truth.state_of_charge = hardware::MilliPercent{80000};
    truth.temperature = hardware::Millicelsius{21500};
    return truth;
}

} // namespace

TEST(SimulatedSensors, ClockAdvanceProducesPredictableReadings) {
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    model.set_truth(truth_at_origin());
    simulation::SimulatedImu<Clock> imu(model);
    simulation::SimulatedGps<Clock> gps(model);
    simulation::SimulatedBatteryMonitor<Clock> battery(model);
    simulation::SimulatedTemperatureSensor<Clock> temperature(model);

    const auto imu0 = imu.read();
    const auto gps0 = gps.read();
    EXPECT_EQ(imu0.time, Time{});
    EXPECT_EQ(gps0.time, Time{});
    EXPECT_EQ(imu0.status, hardware::SensorStatus::Valid);
    EXPECT_EQ(gps0.position, (hardware::PositionUm{0, 0, 0}));
    EXPECT_EQ(imu0.angular_rate, (hardware::AngularRateUradps{0, 250, 0}));
    EXPECT_EQ(battery.read().voltage, hardware::Millivolts{12000});
    EXPECT_EQ(temperature.read().temperature, hardware::Millicelsius{21500});

    ASSERT_EQ(clock.advance(2s), ares::core::AdvanceStatus::Applied);
    const auto gps2 = gps.read();
    const auto imu2 = imu.read();
    EXPECT_EQ(gps2.time, Time{2s});
    EXPECT_EQ(imu2.time, Time{2s});
    EXPECT_EQ(gps2.position, (hardware::PositionUm{2000, 0, 0}));
    EXPECT_EQ(gps2.velocity, (hardware::VelocityUmps{1000, 0, 0}));
    EXPECT_EQ(imu2.acceleration, (hardware::AccelerationUmps2{0, 0, -9806650}));
}

TEST(SimulatedSensors, StatusIsReportedWithoutChangingTheNumbers) {
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    model.set_truth(truth_at_origin());
    simulation::SimulatedGps<Clock> gps(model);
    simulation::SimulatedImu<Clock> imu(model);
    simulation::SimulatedBatteryMonitor<Clock> battery(model);
    simulation::SimulatedTemperatureSensor<Clock> temperature(model);

    model.set_gps_status(hardware::SensorStatus::Stale);
    model.set_imu_status(hardware::SensorStatus::Invalid);
    model.set_battery_status(hardware::SensorStatus::Unavailable);
    model.set_thermal_status(hardware::SensorStatus::Stale);

    EXPECT_EQ(gps.read().status, hardware::SensorStatus::Stale);
    EXPECT_EQ(gps.read().velocity, (hardware::VelocityUmps{1000, 0, 0}));
    EXPECT_EQ(imu.read().status, hardware::SensorStatus::Invalid);
    EXPECT_EQ(battery.read().status, hardware::SensorStatus::Unavailable);
    EXPECT_EQ(temperature.read().status, hardware::SensorStatus::Stale);
    EXPECT_EQ(temperature.read().time, Time{});
}

TEST(SpacecraftState, IntegratesAttitudeFromTheEpoch) {
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    auto truth = truth_at_origin();
    truth.angular_rate = hardware::AngularRateUradps{100, 0, 0};
    model.set_truth(truth);
    ASSERT_EQ(clock.advance(3s), ares::core::AdvanceStatus::Applied);
    const auto state = model.state_now();
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->attitude, (hardware::AttitudeUrad{300, 0, 0}));
    EXPECT_EQ(state->time, Time{3s});
}
