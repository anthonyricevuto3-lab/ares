#include "ares/flight/flight_tasks.hpp"
#include "ares/flight/power_manager.hpp"
#include "ares/flight/thermal_monitor.hpp"
#include "ares/simulation/sensors.hpp"

#include <array>
#include <chrono>
#include <sstream>
#include <vector>

#include <gtest/gtest.h>

namespace flight = ares::flight;
namespace hardware = ares::hardware;
namespace simulation = ares::simulation;
using namespace std::chrono_literals;
using Clock = ares::core::ManualClock;
using Time = Clock::time_point;

namespace {

struct Snapshot {
    flight::NavigationSolution<Time> navigation{};
    hardware::BatterySample<Time> battery{};
    hardware::TemperatureSample<Time> temperature{};
    constexpr bool operator==(const Snapshot&) const = default;
};

simulation::SimulationTruth<Clock> initial() {
    simulation::SimulationTruth<Clock> truth;
    truth.epoch = Time{};
    truth.velocity = hardware::VelocityUmps{500, -100, 0};
    truth.angular_rate = hardware::AngularRateUradps{10, 0, 0};
    truth.acceleration = hardware::AccelerationUmps2{0, 0, -1000};
    truth.voltage = hardware::Millivolts{12600};
    truth.current = hardware::Milliamps{80};
    truth.state_of_charge = hardware::MilliPercent{90000};
    truth.temperature = hardware::Millicelsius{18000};
    return truth;
}

std::vector<Snapshot> run_sequence() {
    Clock clock;
    std::ostringstream output;
    ares::core::Logger<Clock> logger(output, clock, ares::core::LogLevel::Info);
    simulation::SpacecraftModel<Clock> model(clock);
    model.set_truth(initial());
    simulation::SimulatedImu<Clock> imu(model);
    simulation::SimulatedGps<Clock> gps(model);
    simulation::SimulatedBatteryMonitor<Clock> battery(model);
    simulation::SimulatedTemperatureSensor<Clock> temperature(model);
    constexpr flight::NavigationAgeLimits kAges{.imu = 1s, .gps = 1s};
    flight::NavigationCadence<Clock> navigation(logger, imu, gps, clock, kAges);
    flight::PowerManager<Clock> power(battery, clock, 1s);
    flight::ThermalMonitor<Clock> thermal(temperature, clock, 1s);

    std::vector<Snapshot> samples;
    const auto capture = [&] {
        navigation(clock.now().time, {});
        samples.push_back(Snapshot{navigation.solution(), power.sample(), thermal.sample()});
    };
    capture();
    EXPECT_EQ(clock.advance(1s), ares::core::AdvanceStatus::Applied);
    capture();
    EXPECT_EQ(clock.advance(1s), ares::core::AdvanceStatus::Applied);
    capture();
    return samples;
}

} // namespace

TEST(SimulatedSpacecraft, ManualClockSequenceIsPredictable) {
    const auto samples = run_sequence();
    ASSERT_EQ(samples.size(), 3U);
    EXPECT_EQ(samples[0].navigation.position, (hardware::PositionUm{0, 0, 0}));
    EXPECT_EQ(samples[1].navigation.position, (hardware::PositionUm{500, -100, 0}));
    EXPECT_EQ(samples[2].navigation.position, (hardware::PositionUm{1000, -200, 0}));
    EXPECT_EQ(samples[2].navigation.time, Time{2s});
    EXPECT_EQ(samples[2].navigation.angular_rate, (hardware::AngularRateUradps{10, 0, 0}));
    EXPECT_EQ(samples[2].navigation.status, hardware::SensorStatus::Valid);
    EXPECT_EQ(samples[2].navigation.usability, flight::SampleUsability::Usable);
    EXPECT_EQ(samples[2].battery.voltage.count, 12600);
    EXPECT_EQ(samples[2].temperature.temperature.count, 18000);
}

TEST(SimulatedSpacecraft, SameInitialStateAndClockSequenceMatch) {
    EXPECT_EQ(run_sequence(), run_sequence());
}

TEST(SimulatedSpacecraft, SameSeedAndClockSequenceMatch) {
    const auto run_noisy = [] {
        Clock clock;
        simulation::SpacecraftModel<Clock> model(clock);
        model.set_truth(initial());
        simulation::SensorNoise noise;
        noise.mission_seed = 17;
        noise.acceleration = 30;
        noise.angular_rate = 5;
        noise.position = 20;
        noise.velocity = 4;
        noise.voltage = 15;
        noise.current = 2;
        noise.state_of_charge = 25;
        noise.temperature = 10;
        simulation::SimulatedImu<Clock> imu(model, noise);
        simulation::SimulatedGps<Clock> gps(model, noise);
        simulation::SimulatedBatteryMonitor<Clock> battery(model, noise);
        simulation::SimulatedTemperatureSensor<Clock> temperature(model, noise);
        struct Cycle {
            hardware::ImuSample<Time> imu{};
            hardware::GpsSample<Time> gps{};
            hardware::BatterySample<Time> battery{};
            hardware::TemperatureSample<Time> temperature{};
            hardware::SpacecraftState<Time> truth{};
            constexpr bool operator==(const Cycle&) const = default;
        };
        std::array<Cycle, 3> cycles{};
        for (Cycle& cycle : cycles) {
            const auto state = model.state_now();
            EXPECT_TRUE(state.has_value());
            cycle.truth = *state;
            cycle.imu = imu.read();
            cycle.gps = gps.read();
            cycle.battery = battery.read();
            cycle.temperature = temperature.read();
            EXPECT_EQ(clock.advance(1s), ares::core::AdvanceStatus::Applied);
        }
        return cycles;
    };
    const auto first = run_noisy();
    const auto second = run_noisy();
    EXPECT_EQ(first, second);
    EXPECT_NE(first[1].imu.acceleration, first[1].truth.acceleration);
    EXPECT_EQ(first[1].truth, second[1].truth);
}
