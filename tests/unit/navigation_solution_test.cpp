#include "ares/flight/example_tasks.hpp"
#include "ares/flight/navigation.hpp"
#include "ares/hardware/interfaces.hpp"

#include <sstream>

#include <gtest/gtest.h>

namespace flight = ares::flight;
namespace hardware = ares::hardware;
using Clock = ares::core::ManualClock;
using Time = Clock::time_point;

namespace {

class FixedImu final : public hardware::IImu<Time> {
public:
    explicit FixedImu(hardware::ImuSample<Time> sample) : sample_(sample) {}
    [[nodiscard]] hardware::ImuSample<Time> read() const override { return sample_; }

private:
    hardware::ImuSample<Time> sample_;
};

class FixedGps final : public hardware::IGps<Time> {
public:
    explicit FixedGps(hardware::GpsSample<Time> sample) : sample_(sample) {}
    [[nodiscard]] hardware::GpsSample<Time> read() const override { return sample_; }

private:
    hardware::GpsSample<Time> sample_;
};

} // namespace

TEST(NavigationSolution, CombinesInterfacesAndKeepsTheWorseStatus) {
    hardware::ImuSample<Time> imu;
    imu.acceleration = hardware::AccelerationUmps2{1, 2, 3};
    imu.angular_rate = hardware::AngularRateUradps{4, 5, 6};
    imu.time = Time{};
    imu.status = hardware::SensorStatus::Valid;
    hardware::GpsSample<Time> gps;
    gps.position = hardware::PositionUm{10, 20, 30};
    gps.velocity = hardware::VelocityUmps{7, 8, 9};
    gps.time = Time{};
    gps.status = hardware::SensorStatus::Stale;

    const auto solution = flight::combine_navigation(imu, gps);
    EXPECT_EQ(solution.position, gps.position);
    EXPECT_EQ(solution.velocity, gps.velocity);
    EXPECT_EQ(solution.acceleration, imu.acceleration);
    EXPECT_EQ(solution.angular_rate, imu.angular_rate);
    EXPECT_EQ(solution.status, hardware::SensorStatus::Stale);
}

TEST(NavigationCadence, ReadsOnlyThroughTheInterfaces) {
    Clock clock;
    std::ostringstream output;
    ares::core::Logger<Clock> logger(output, clock, ares::core::LogLevel::Info);
    hardware::ImuSample<Time> imu_sample;
    imu_sample.status = hardware::SensorStatus::Valid;
    imu_sample.angular_rate = hardware::AngularRateUradps{11, 0, 0};
    hardware::GpsSample<Time> gps_sample;
    gps_sample.status = hardware::SensorStatus::Valid;
    gps_sample.position = hardware::PositionUm{42, 0, 0};
    gps_sample.velocity = hardware::VelocityUmps{3, 0, 0};
    FixedImu imu(imu_sample);
    FixedGps gps(gps_sample);
    flight::NavigationCadence<Clock> navigation(logger, imu, gps);

    navigation(Time{}, {});
    EXPECT_EQ(navigation.solution().position, gps_sample.position);
    EXPECT_EQ(navigation.solution().angular_rate, imu_sample.angular_rate);
    EXPECT_EQ(navigation.solution().status, hardware::SensorStatus::Valid);
    EXPECT_EQ(navigation.cycles(), 1U);
}
