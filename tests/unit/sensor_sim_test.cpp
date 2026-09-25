#include "ares/simulation/sensors.hpp"

#include <chrono>
#include <limits>
#include <utility>

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

TEST(SimulatedSensors, ZeroNoiseMatchesTruthForAnySeed) {
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    model.set_truth(truth_at_origin());
    simulation::SensorNoise noise;
    noise.mission_seed = 99;
    simulation::SimulatedImu<Clock> imu(model, noise);
    simulation::SimulatedGps<Clock> gps(model, noise);
    simulation::SimulatedBatteryMonitor<Clock> battery(model, noise);
    simulation::SimulatedTemperatureSensor<Clock> temperature(model, noise);
    simulation::SimulatedImu<Clock> quiet(model);

    ASSERT_EQ(clock.advance(2s), ares::core::AdvanceStatus::Applied);
    const auto state = model.state_now();
    ASSERT_TRUE(state.has_value());
    const auto imu_sample = imu.read();
    const auto gps_sample = gps.read();
    EXPECT_EQ(imu_sample.acceleration, state->acceleration);
    EXPECT_EQ(imu_sample.angular_rate, state->angular_rate);
    EXPECT_EQ(gps_sample.position, state->position);
    EXPECT_EQ(gps_sample.velocity, state->velocity);
    EXPECT_EQ(battery.read().voltage, state->voltage);
    EXPECT_EQ(battery.read().current, state->current);
    EXPECT_EQ(battery.read().state_of_charge, state->state_of_charge);
    EXPECT_EQ(temperature.read().temperature, state->temperature);
    EXPECT_EQ(imu_sample.acceleration, quiet.read().acceleration);
}

TEST(SimulatedSensors, NoiseStaysInsideAmplitude) {
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    model.set_truth(truth_at_origin());
    simulation::SensorNoise noise;
    noise.mission_seed = 5;
    noise.acceleration = 20;
    noise.angular_rate = 7;
    noise.position = 15;
    noise.velocity = 4;
    noise.voltage = 30;
    noise.current = 3;
    noise.state_of_charge = 50;
    noise.temperature = 12;
    simulation::SimulatedImu<Clock> imu(model, noise);
    simulation::SimulatedGps<Clock> gps(model, noise);
    simulation::SimulatedBatteryMonitor<Clock> battery(model, noise);
    simulation::SimulatedTemperatureSensor<Clock> temperature(model, noise);

    const auto within = [](std::int64_t value, std::int64_t truth, std::int64_t amplitude) {
        const auto delta = value - truth;
        const auto magnitude = delta < 0 ? -delta : delta;
        return magnitude <= amplitude;
    };

    const auto imu_sample = imu.read();
    const auto gps_sample = gps.read();
    const auto battery_sample = battery.read();
    const auto thermal_sample = temperature.read();
    EXPECT_TRUE(within(imu_sample.acceleration.x, 0, noise.acceleration));
    EXPECT_TRUE(within(imu_sample.acceleration.z, -9806650, noise.acceleration));
    EXPECT_TRUE(within(imu_sample.angular_rate.y, 250, noise.angular_rate));
    EXPECT_TRUE(within(gps_sample.velocity.x, 1000, noise.velocity));
    EXPECT_TRUE(within(gps_sample.position.x, 0, noise.position));
    EXPECT_TRUE(within(battery_sample.voltage.count, 12000, noise.voltage));
    EXPECT_TRUE(within(battery_sample.current.count, 150, noise.current));
    EXPECT_TRUE(within(battery_sample.state_of_charge.count, 80000, noise.state_of_charge));
    EXPECT_TRUE(within(thermal_sample.temperature.count, 21500, noise.temperature));
    EXPECT_EQ(imu_sample.status, hardware::SensorStatus::Valid);
}

TEST(SimulatedSensors, SameSeedAndClockMatch) {
    const auto read_pair = [] {
        Clock clock;
        simulation::SpacecraftModel<Clock> model(clock);
        model.set_truth(truth_at_origin());
        simulation::SensorNoise noise;
        noise.mission_seed = 3;
        noise.acceleration = 40;
        noise.angular_rate = 9;
        noise.position = 25;
        noise.velocity = 6;
        noise.voltage = 10;
        noise.current = 2;
        noise.state_of_charge = 20;
        noise.temperature = 8;
        simulation::SimulatedImu<Clock> imu(model, noise);
        simulation::SimulatedGps<Clock> gps(model, noise);
        simulation::SimulatedBatteryMonitor<Clock> battery(model, noise);
        simulation::SimulatedTemperatureSensor<Clock> temperature(model, noise);
        struct Bundle {
            hardware::ImuSample<Time> imu{};
            hardware::GpsSample<Time> gps{};
            hardware::BatterySample<Time> battery{};
            hardware::TemperatureSample<Time> temperature{};
            constexpr bool operator==(const Bundle&) const = default;
        };
        Bundle first{imu.read(), gps.read(), battery.read(), temperature.read()};
        EXPECT_EQ(clock.advance(1s), ares::core::AdvanceStatus::Applied);
        Bundle second{imu.read(), gps.read(), battery.read(), temperature.read()};
        return std::pair{first, second};
    };
    EXPECT_EQ(read_pair(), read_pair());
}

TEST(SimulatedSensors, DifferentSeedChangesMeasurementNotTruth) {
    const auto spin_up = [](std::uint64_t seed) {
        Clock clock;
        simulation::SpacecraftModel<Clock> model(clock);
        model.set_truth(truth_at_origin());
        simulation::SensorNoise noise;
        noise.mission_seed = seed;
        noise.acceleration = 1000;
        simulation::SimulatedImu<Clock> imu(model, noise);
        const auto state = model.state_now();
        const auto sample = imu.read();
        return std::pair{state, sample.acceleration};
    };
    const auto first = spin_up(1);
    const auto second = spin_up(2);
    ASSERT_TRUE(first.first.has_value());
    ASSERT_TRUE(second.first.has_value());
    EXPECT_EQ(*first.first, *second.first);
    EXPECT_NE(first.second, second.second);
}

TEST(SimulatedSensors, SensorStreamsDoNotCross) {
    simulation::SensorNoise noise;
    noise.mission_seed = 7;
    noise.acceleration = 80;
    noise.position = 40;
    Clock clock_a;
    Clock clock_b;
    simulation::SpacecraftModel<Clock> model_a(clock_a);
    simulation::SpacecraftModel<Clock> model_b(clock_b);
    model_a.set_truth(truth_at_origin());
    model_b.set_truth(truth_at_origin());
    simulation::SimulatedImu<Clock> imu_a(model_a, noise);
    simulation::SimulatedGps<Clock> gps_a(model_a, noise);
    simulation::SimulatedGps<Clock> gps_b(model_b, noise);
    for (int draw = 0; draw < 20; ++draw) {
        (void)imu_a.read();
    }
    EXPECT_EQ(gps_a.read(), gps_b.read());
}

TEST(SimulatedSensors, KnownSeedProducesNonZeroOffsets) {
    // Independent SplitMix64 trace for mission seed 3, IMU stream, amplitude 5.
    // Acceleration offsets are 1, 3, 2. Angular-rate offsets are -5, -3, -3.
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    auto truth = truth_at_origin();
    truth.acceleration = hardware::AccelerationUmps2{0, 0, 0};
    truth.angular_rate = hardware::AngularRateUradps{0, 0, 0};
    model.set_truth(truth);
    simulation::SensorNoise noise;
    noise.mission_seed = 3;
    noise.acceleration = 5;
    noise.angular_rate = 5;
    simulation::SimulatedImu<Clock> imu(model, noise);
    const auto sample = imu.read();
    EXPECT_EQ(sample.acceleration, (hardware::AccelerationUmps2{1, 3, 2}));
    EXPECT_EQ(sample.angular_rate, (hardware::AngularRateUradps{-5, -3, -3}));
    EXPECT_NE(sample.acceleration, truth.acceleration);
}

TEST(SimulatedSensors, UnitAmplitudeAndNegativeAmplitude) {
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    auto truth = truth_at_origin();
    truth.acceleration = hardware::AccelerationUmps2{10, 10, 10};
    model.set_truth(truth);
    simulation::SensorNoise unit;
    unit.mission_seed = 7;
    unit.acceleration = 1;
    simulation::SimulatedImu<Clock> imu(model, unit);
    // Mission seed 7, IMU stream, amplitude 1: offsets -1, -1, 1.
    EXPECT_EQ(imu.read().acceleration, (hardware::AccelerationUmps2{9, 9, 11}));

    simulation::SensorNoise negative;
    negative.mission_seed = 7;
    negative.acceleration = -4;
    simulation::SimulatedImu<Clock> negated(model, negative);
    simulation::SimulatedImu<Clock> quiet(model);
    EXPECT_EQ(negated.read().acceleration, quiet.read().acceleration);
    EXPECT_EQ(negated.read().status, hardware::SensorStatus::Valid);
}

TEST(SimulatedSensors, TooLargeAmplitudeDoesNotDrawAndIsInvalid) {
    constexpr auto kTooLarge = ((std::numeric_limits<std::int64_t>::max() - 1) / 2) + 1;
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    model.set_truth(truth_at_origin());
    simulation::SensorNoise noise;
    noise.mission_seed = 3;
    noise.acceleration = kTooLarge;
    simulation::SimulatedImu<Clock> imu(model, noise);
    const auto before = model.state_now();
    ASSERT_TRUE(before.has_value());
    const auto sample = imu.read();
    EXPECT_EQ(sample.status, hardware::SensorStatus::Invalid);
    EXPECT_EQ(sample.acceleration, before->acceleration);
    EXPECT_EQ(sample.angular_rate, before->angular_rate);
    const auto again = imu.read();
    EXPECT_EQ(again.status, hardware::SensorStatus::Invalid);
    EXPECT_EQ(again.acceleration, before->acceleration);
}

TEST(SimulatedSensors, NoiseSaturatesAtInt64Limits) {
    constexpr auto kMax = std::numeric_limits<std::int64_t>::max();
    constexpr auto kMin = std::numeric_limits<std::int64_t>::min();
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    auto truth = truth_at_origin();
    truth.acceleration = hardware::AccelerationUmps2{kMax, 0, 0};
    truth.angular_rate = hardware::AngularRateUradps{kMin, 0, 0};
    model.set_truth(truth);
    simulation::SensorNoise noise;
    noise.mission_seed = 3;
    noise.acceleration = 5;
    noise.angular_rate = 5;
    simulation::SimulatedImu<Clock> imu(model, noise);
    const auto sample = imu.read();
    EXPECT_EQ(sample.status, hardware::SensorStatus::Valid);
    EXPECT_EQ(sample.acceleration.x, kMax);
    EXPECT_EQ(sample.acceleration.y, 3);
    EXPECT_EQ(sample.acceleration.z, 2);
    EXPECT_EQ(sample.angular_rate.x, kMin);
    EXPECT_EQ(sample.angular_rate.y, -3);
    EXPECT_EQ(sample.angular_rate.z, -3);
}

TEST(SimulatedSensors, ConstructionOrderDoesNotChangeGpsStream) {
    simulation::SensorNoise noise;
    noise.mission_seed = 1;
    noise.position = 4;
    Clock clock_gps_first;
    Clock clock_imu_first;
    simulation::SpacecraftModel<Clock> gps_first_model(clock_gps_first);
    simulation::SpacecraftModel<Clock> imu_first_model(clock_imu_first);
    gps_first_model.set_truth(truth_at_origin());
    imu_first_model.set_truth(truth_at_origin());
    simulation::SimulatedGps<Clock> gps_before_imu(gps_first_model, noise);
    simulation::SimulatedImu<Clock> imu_after(gps_first_model, noise);
    simulation::SimulatedImu<Clock> imu_before(imu_first_model, noise);
    simulation::SimulatedGps<Clock> gps_after_imu(imu_first_model, noise);
    (void)imu_after;
    (void)imu_before.read();
    const auto first = gps_before_imu.read();
    const auto second = gps_after_imu.read();
    EXPECT_EQ(first, second);
    EXPECT_NE(first.position, (hardware::PositionUm{0, 0, 0}));
}

TEST(SimulatedSensors, ReadDoesNotChangeTruth) {
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    model.set_truth(truth_at_origin());
    simulation::SensorNoise noise;
    noise.mission_seed = 4;
    noise.acceleration = 20;
    noise.position = 20;
    simulation::SimulatedImu<Clock> imu(model, noise);
    simulation::SimulatedGps<Clock> gps(model, noise);
    const auto truth_before = model.truth();
    const auto state_before = model.state_now();
    ASSERT_TRUE(state_before.has_value());
    (void)imu.read();
    (void)gps.read();
    const auto truth_after = model.truth();
    const auto state_after = model.state_now();
    ASSERT_TRUE(state_after.has_value());
    EXPECT_EQ(truth_before.position, truth_after.position);
    EXPECT_EQ(truth_before.velocity, truth_after.velocity);
    EXPECT_EQ(truth_before.attitude, truth_after.attitude);
    EXPECT_EQ(truth_before.angular_rate, truth_after.angular_rate);
    EXPECT_EQ(truth_before.acceleration, truth_after.acceleration);
    EXPECT_EQ(truth_before.epoch, truth_after.epoch);
    EXPECT_EQ(*state_before, *state_after);
}
