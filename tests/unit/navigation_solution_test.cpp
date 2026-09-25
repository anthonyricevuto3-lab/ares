#include "ares/flight/example_tasks.hpp"
#include "ares/flight/navigation.hpp"
#include "ares/hardware/interfaces.hpp"

#include <chrono>
#include <sstream>

#include <gtest/gtest.h>

namespace flight = ares::flight;
namespace hardware = ares::hardware;
using namespace std::chrono_literals;
using Clock = ares::core::ManualClock;
using Time = Clock::time_point;

constexpr flight::NavigationAgeLimits kWide{.imu = 1s, .gps = 1s};

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

    const auto solution =
        flight::combine_navigation(imu, gps, ares::core::ClockStatus::Ok, Time{}, kWide);
    EXPECT_EQ(solution.position, gps.position);
    EXPECT_EQ(solution.velocity, gps.velocity);
    EXPECT_EQ(solution.acceleration, imu.acceleration);
    EXPECT_EQ(solution.angular_rate, imu.angular_rate);
    EXPECT_EQ(solution.status, hardware::SensorStatus::Stale);
    EXPECT_EQ(solution.usability, flight::SampleUsability::Stale);
    EXPECT_EQ(solution.imu_usability, flight::SampleUsability::Usable);
    EXPECT_EQ(solution.gps_usability, flight::SampleUsability::Stale);
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
    flight::NavigationCadence<Clock> navigation(logger, imu, gps, clock, kWide);

    navigation(Time{}, {});
    EXPECT_EQ(navigation.solution().position, gps_sample.position);
    EXPECT_EQ(navigation.solution().angular_rate, imu_sample.angular_rate);
    EXPECT_EQ(navigation.solution().status, hardware::SensorStatus::Valid);
    EXPECT_EQ(navigation.solution().usability, flight::SampleUsability::Usable);
    EXPECT_EQ(navigation.cycles(), 1U);
}

TEST(NavigationFreshness, StaleImuPropagates) {
    Clock clock;
    ASSERT_EQ(clock.advance(500ms), ares::core::AdvanceStatus::Applied);
    std::ostringstream output;
    ares::core::Logger<Clock> logger(output, clock, ares::core::LogLevel::Info);
    hardware::ImuSample<Time> imu_sample;
    imu_sample.status = hardware::SensorStatus::Valid;
    imu_sample.time = Time{};
    imu_sample.acceleration = hardware::AccelerationUmps2{9, 0, 0};
    hardware::GpsSample<Time> gps_sample;
    gps_sample.status = hardware::SensorStatus::Valid;
    gps_sample.time = Time{500ms};
    gps_sample.position = hardware::PositionUm{1, 0, 0};
    FixedImu imu(imu_sample);
    FixedGps gps(gps_sample);
    flight::NavigationAgeLimits limits{.imu = 100ms, .gps = 1s};
    flight::NavigationCadence<Clock> navigation(logger, imu, gps, clock, limits);

    navigation(clock.now().time, {});
    EXPECT_EQ(navigation.solution().status, hardware::SensorStatus::Valid);
    EXPECT_EQ(navigation.solution().usability, flight::SampleUsability::Stale);
    EXPECT_EQ(navigation.solution().acceleration, imu_sample.acceleration);
    EXPECT_EQ(navigation.solution().position, gps_sample.position);
}

TEST(NavigationFreshness, StaleGpsPropagates) {
    Clock clock;
    ASSERT_EQ(clock.advance(2s), ares::core::AdvanceStatus::Applied);
    std::ostringstream output;
    ares::core::Logger<Clock> logger(output, clock, ares::core::LogLevel::Info);
    hardware::ImuSample<Time> imu_sample;
    imu_sample.status = hardware::SensorStatus::Valid;
    imu_sample.time = Time{2s};
    hardware::GpsSample<Time> gps_sample;
    gps_sample.status = hardware::SensorStatus::Valid;
    gps_sample.time = Time{};
    gps_sample.position = hardware::PositionUm{4, 0, 0};
    FixedImu imu(imu_sample);
    FixedGps gps(gps_sample);
    flight::NavigationAgeLimits limits{.imu = 1s, .gps = 1s};
    flight::NavigationCadence<Clock> navigation(logger, imu, gps, clock, limits);

    navigation(clock.now().time, {});
    EXPECT_EQ(navigation.solution().usability, flight::SampleUsability::Stale);
    EXPECT_EQ(navigation.solution().position, gps_sample.position);
}

TEST(NavigationFreshness, InvalidAndUnavailablePropagate) {
    Clock clock;
    std::ostringstream output;
    ares::core::Logger<Clock> logger(output, clock, ares::core::LogLevel::Info);
    hardware::ImuSample<Time> imu_sample;
    imu_sample.status = hardware::SensorStatus::Invalid;
    hardware::GpsSample<Time> gps_sample;
    gps_sample.status = hardware::SensorStatus::Valid;
    FixedImu invalid_imu(imu_sample);
    FixedGps gps(gps_sample);
    flight::NavigationCadence<Clock> invalid_nav(logger, invalid_imu, gps, clock, kWide);
    invalid_nav(Time{}, {});
    EXPECT_EQ(invalid_nav.solution().status, hardware::SensorStatus::Invalid);
    EXPECT_EQ(invalid_nav.solution().usability, flight::SampleUsability::Invalid);

    imu_sample.status = hardware::SensorStatus::Unavailable;
    FixedImu unavailable_imu(imu_sample);
    flight::NavigationCadence<Clock> unavailable_nav(logger, unavailable_imu, gps, clock, kWide);
    unavailable_nav(Time{}, {});
    EXPECT_EQ(unavailable_nav.solution().usability, flight::SampleUsability::Unavailable);
}

TEST(NavigationFreshness, FutureTimestampIsNotUsable) {
    Clock clock;
    std::ostringstream output;
    ares::core::Logger<Clock> logger(output, clock, ares::core::LogLevel::Info);
    hardware::ImuSample<Time> imu_sample;
    imu_sample.status = hardware::SensorStatus::Valid;
    imu_sample.time = Time{5s};
    hardware::GpsSample<Time> gps_sample;
    gps_sample.status = hardware::SensorStatus::Valid;
    gps_sample.time = Time{};
    FixedImu imu(imu_sample);
    FixedGps gps(gps_sample);
    flight::NavigationCadence<Clock> navigation(logger, imu, gps, clock, kWide);

    navigation(Time{}, {});
    EXPECT_EQ(navigation.solution().status, hardware::SensorStatus::Valid);
    EXPECT_EQ(navigation.solution().usability, flight::SampleUsability::Future);
    EXPECT_FALSE(navigation.last_usable().has_value());
}

TEST(NavigationFreshness, AgeLimitIsUsableAndOneTickLaterIsStale) {
    Clock clock;
    ASSERT_EQ(clock.advance(100ms), ares::core::AdvanceStatus::Applied);
    std::ostringstream output;
    ares::core::Logger<Clock> logger(output, clock, ares::core::LogLevel::Info);
    hardware::ImuSample<Time> imu_sample;
    imu_sample.status = hardware::SensorStatus::Valid;
    imu_sample.time = Time{};
    imu_sample.acceleration = hardware::AccelerationUmps2{8, 0, 0};
    hardware::GpsSample<Time> gps_sample;
    gps_sample.status = hardware::SensorStatus::Valid;
    gps_sample.time = Time{100ms};
    gps_sample.position = hardware::PositionUm{5, 0, 0};
    FixedImu imu(imu_sample);
    FixedGps gps(gps_sample);
    flight::NavigationAgeLimits limits{.imu = 100ms, .gps = 1s};
    flight::NavigationCadence<Clock> navigation(logger, imu, gps, clock, limits);

    navigation(clock.now().time, {});
    EXPECT_EQ(navigation.solution().usability, flight::SampleUsability::Usable);
    ASSERT_TRUE(navigation.last_usable().has_value());
    EXPECT_EQ(navigation.last_usable()->acceleration.x, 8);

    ASSERT_EQ(clock.advance(1ns), ares::core::AdvanceStatus::Applied);
    navigation(clock.now().time, {});
    EXPECT_EQ(navigation.solution().status, hardware::SensorStatus::Valid);
    EXPECT_EQ(navigation.solution().usability, flight::SampleUsability::Stale);
    EXPECT_EQ(navigation.solution().acceleration.x, 8);
    ASSERT_TRUE(navigation.last_usable().has_value());
    EXPECT_EQ(navigation.last_usable()->acceleration.x, 8);
    EXPECT_EQ(navigation.last_usable()->usability, flight::SampleUsability::Usable);
}

namespace {

class MutableImu final : public hardware::IImu<Time> {
public:
    hardware::ImuSample<Time> sample{};
    [[nodiscard]] hardware::ImuSample<Time> read() const override { return sample; }
};

class MutableGps final : public hardware::IGps<Time> {
public:
    hardware::GpsSample<Time> sample{};
    [[nodiscard]] hardware::GpsSample<Time> read() const override { return sample; }
};

} // namespace

TEST(NavigationFreshness, UnusableSamplesDoNotReplaceLastUsable) {
    Clock clock;
    std::ostringstream output;
    ares::core::Logger<Clock> logger(output, clock, ares::core::LogLevel::Info);
    MutableImu imu;
    MutableGps gps;
    imu.sample.status = hardware::SensorStatus::Valid;
    imu.sample.acceleration = hardware::AccelerationUmps2{4, 0, 0};
    gps.sample.status = hardware::SensorStatus::Valid;
    gps.sample.position = hardware::PositionUm{10, 0, 0};
    flight::NavigationCadence<Clock> navigation(logger, imu, gps, clock, kWide);
    navigation(Time{}, {});
    ASSERT_TRUE(navigation.last_usable().has_value());
    EXPECT_EQ(navigation.last_usable()->position.x, 10);

    imu.sample.status = hardware::SensorStatus::Invalid;
    imu.sample.acceleration = hardware::AccelerationUmps2{99, 0, 0};
    gps.sample.position = hardware::PositionUm{20, 0, 0};
    navigation(Time{}, {});
    EXPECT_EQ(navigation.solution().usability, flight::SampleUsability::Invalid);
    EXPECT_EQ(navigation.solution().position.x, 20);
    EXPECT_EQ(navigation.last_usable()->position.x, 10);

    imu.sample.status = hardware::SensorStatus::Unavailable;
    navigation(Time{}, {});
    EXPECT_EQ(navigation.solution().usability, flight::SampleUsability::Unavailable);
    EXPECT_EQ(navigation.last_usable()->position.x, 10);
}

TEST(NavigationFreshness, ClockFailureIsTimeError) {
    // NavigationCadence forwards this status into combine_navigation. The cadence type is
    // explicitly instantiated only for ManualClock and SteadyClock, whose now() does not fail.
    hardware::ImuSample<Time> imu_sample;
    imu_sample.status = hardware::SensorStatus::Valid;
    hardware::GpsSample<Time> gps_sample;
    gps_sample.status = hardware::SensorStatus::Valid;
    gps_sample.position = hardware::PositionUm{3, 0, 0};
    const auto rejected = flight::combine_navigation(
        imu_sample, gps_sample, ares::core::ClockStatus::Unrepresentable, Time{}, kWide);
    EXPECT_EQ(rejected.status, hardware::SensorStatus::Valid);
    EXPECT_EQ(rejected.usability, flight::SampleUsability::TimeError);
    EXPECT_EQ(rejected.imu_usability, flight::SampleUsability::TimeError);
    EXPECT_EQ(rejected.gps_usability, flight::SampleUsability::TimeError);
    EXPECT_EQ(rejected.position.x, 3);
}
