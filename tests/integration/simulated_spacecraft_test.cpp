#include "ares/flight/example_tasks.hpp"
#include "ares/flight/power_manager.hpp"
#include "ares/flight/thermal_monitor.hpp"
#include "ares/simulation/sensors.hpp"

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
    flight::NavigationCadence<Clock> navigation(logger, imu, gps);
    flight::PowerManager<Time> power(battery);
    flight::ThermalMonitor<Time> thermal(temperature);

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
    EXPECT_EQ(samples[2].battery.voltage.count, 12600);
    EXPECT_EQ(samples[2].temperature.temperature.count, 18000);
}

TEST(SimulatedSpacecraft, SameInitialStateAndClockSequenceMatch) {
    EXPECT_EQ(run_sequence(), run_sequence());
}
