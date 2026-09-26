#include "ares/flight/flight_tasks.hpp"
#include "ares/flight/gps_selector.hpp"
#include "ares/simulation/sensors.hpp"

#include <chrono>
#include <sstream>

#include <gtest/gtest.h>

namespace flight = ares::flight;
namespace hardware = ares::hardware;
namespace simulation = ares::simulation;
using Clock = ares::core::ManualClock;
using Time = Clock::time_point;

namespace {

class FixedGps final : public hardware::IGps<Time> {
public:
    explicit FixedGps(std::int64_t position) : position_(position) {}

    [[nodiscard]] hardware::GpsSample<Time> read() const override {
        hardware::GpsSample<Time> sample;
        sample.position = hardware::PositionUm{position_, 0, 0};
        sample.time = Time{};
        sample.status = hardware::SensorStatus::Valid;
        return sample;
    }

private:
    std::int64_t position_;
};

class CountingGps final : public hardware::IGps<Time> {
public:
    explicit CountingGps(std::int64_t position) : position_(position) {}

    [[nodiscard]] hardware::GpsSample<Time> read() const override {
        ++reads_;
        hardware::GpsSample<Time> sample;
        sample.position = hardware::PositionUm{position_, 0, 0};
        sample.time = Time{};
        sample.status = hardware::SensorStatus::Valid;
        return sample;
    }

    [[nodiscard]] int reads() const noexcept { return reads_; }

private:
    std::int64_t position_;
    mutable int reads_{0};
};

class FixedImu final : public hardware::IImu<Time> {
public:
    [[nodiscard]] hardware::ImuSample<Time> read() const override {
        hardware::ImuSample<Time> sample;
        sample.time = Time{};
        sample.status = hardware::SensorStatus::Valid;
        return sample;
    }
};

} // namespace

TEST(GpsSelector, PrimaryIsSelectedUntilFailover) {
    FixedGps primary{10};
    FixedGps backup{40};
    flight::GpsSelector<Clock> selector(primary, backup);
    const flight::GpsSelector<Clock>::Sample first = selector.read_both();
    EXPECT_EQ(first.selection, flight::GpsSelection::Primary);
    EXPECT_EQ(first.selected.position.x, 10);
    EXPECT_EQ(first.backup.position.x, 40);
    selector.failover_to_backup();
    const flight::GpsSelector<Clock>::Sample second = selector.read_both();
    EXPECT_EQ(second.selection, flight::GpsSelection::Backup);
    EXPECT_TRUE(second.primary_isolated);
    EXPECT_EQ(second.selected.position.x, 40);
    EXPECT_EQ(second.primary.position.x, 10);
}

TEST(GpsStreams, PrimaryAndBackupDrawsDoNotCross) {
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    simulation::SimulationTruth<Clock> truth;
    truth.epoch = Time{};
    model.set_truth(truth);
    simulation::SensorNoise noise;
    noise.mission_seed = 11;
    noise.position = 5;
    simulation::SimulatedGps<Clock> primary(model, noise);
    simulation::SimulatedGps<Clock> backup(model, noise, nullptr,
                                           simulation::SensorStream::BackupGps,
                                           simulation::ChaosTarget::BackupGps);
    const hardware::GpsSample<Time> primary_first = primary.read();
    const hardware::GpsSample<Time> backup_first = backup.read();
    const hardware::GpsSample<Time> backup_second = backup.read();
    simulation::SimulatedGps<Clock> backup_alone(model, noise, nullptr,
                                                 simulation::SensorStream::BackupGps,
                                                 simulation::ChaosTarget::BackupGps);
    EXPECT_EQ(backup_alone.read().position.x, backup_first.position.x);
    EXPECT_EQ(backup_alone.read().position.x, backup_second.position.x);
    simulation::SimulatedGps<Clock> primary_alone(model, noise);
    EXPECT_EQ(primary_alone.read().position.x, primary_first.position.x);
}

TEST(NavigationCadence, SolutionUsesTheSelectedBackupSample) {
    FixedGps primary{10};
    FixedGps backup{40};
    flight::GpsSelector<Clock> selector(primary, backup);
    Clock clock;
    std::ostringstream output;
    ares::core::Logger<Clock> logger(output, clock);
    FixedImu imu;
    flight::NavigationCadence<Clock> navigation(
        logger, imu, selector, clock,
        flight::NavigationAgeLimits{std::chrono::seconds{1}, std::chrono::seconds{1}});
    selector.failover_to_backup();
    navigation(Time{}, {});
    EXPECT_EQ(navigation.solution().position.x, 40);
    EXPECT_EQ(navigation.primary_gps().position.x, 10);
    EXPECT_EQ(navigation.backup_gps().position.x, 40);
}

TEST(NavigationCadence, ReadsEachGpsOnceForEitherSelection) {
    Clock clock;
    std::ostringstream output;
    ares::core::Logger<Clock> logger(output, clock);
    FixedImu imu;
    const flight::NavigationAgeLimits limits{std::chrono::seconds{1}, std::chrono::seconds{1}};

    const auto check = [&](bool backup_selected) {
        CountingGps primary{10};
        CountingGps backup{40};
        flight::GpsSelector<Clock> selector(primary, backup);
        if (backup_selected) {
            selector.failover_to_backup();
        }
        flight::NavigationCadence<Clock> navigation(logger, imu, selector, clock, limits);
        navigation(Time{}, {});
        EXPECT_EQ(primary.reads(), 1);
        EXPECT_EQ(backup.reads(), 1);
        EXPECT_EQ(navigation.solution().position.x, backup_selected ? 40 : 10);
        EXPECT_EQ(navigation.primary_gps().position.x, 10);
        EXPECT_EQ(navigation.backup_gps().position.x, 40);
        EXPECT_EQ(primary.reads(), 1);
        EXPECT_EQ(backup.reads(), 1);
    };
    check(false);
    check(true);
}
