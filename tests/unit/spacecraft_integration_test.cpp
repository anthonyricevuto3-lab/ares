#include "ares/simulation/sensors.hpp"

#include <chrono>
#include <limits>

#include <gtest/gtest.h>

namespace hardware = ares::hardware;
namespace simulation = ares::simulation;
using namespace std::chrono_literals;
using Clock = ares::core::ManualClock;
using Time = Clock::time_point;

namespace {

constexpr std::int64_t kSixHourUm = 21600000000LL;
constexpr std::int64_t kOrbitalThirtySecondsUm = 234000000000LL;
// INT64_MIN / 1000000000, truncated toward zero. Not a call into integrate().
constexpr std::int64_t kInt64MinAtOneNanosecond = -9223372036LL;

simulation::SimulationTruth<Clock> at_epoch(hardware::VelocityUmps velocity,
                                            hardware::PositionUm position = {},
                                            hardware::AngularRateUradps rate = {}) {
    simulation::SimulationTruth<Clock> truth;
    truth.epoch = Time{};
    truth.velocity = velocity;
    truth.position = position;
    truth.angular_rate = rate;
    return truth;
}

} // namespace

TEST(SpacecraftIntegration, OneMeterPerSecondOverSixHours) {
    // 1'000'000 um/s * 21'600 s = 21'600'000'000 um. The opposite axis is the negation.
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    model.set_truth(at_epoch(hardware::VelocityUmps{1000000, -1000000, 0}, {},
                             hardware::AngularRateUradps{1000000, 0, 0}));
    ASSERT_EQ(clock.advance(6h), ares::core::AdvanceStatus::Applied);
    const auto state = model.state_now();
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->position.x, kSixHourUm);
    EXPECT_EQ(state->position.y, -kSixHourUm);
    EXPECT_EQ(state->position.z, 0);
    EXPECT_EQ(state->attitude.x, kSixHourUm);
}

TEST(SpacecraftIntegration, OrbitalRateOverThirtySeconds) {
    // 7'800'000'000 um/s * 30 s = 234'000'000'000 um.
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    model.set_truth(at_epoch(hardware::VelocityUmps{7800000000LL, 0, 0}));
    ASSERT_EQ(clock.advance(30s), ares::core::AdvanceStatus::Applied);
    const auto state = model.state_now();
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->position.x, kOrbitalThirtySecondsUm);
}

TEST(SpacecraftIntegration, Int64MinRateOverOneNanosecondIsRepresentable) {
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    const auto rate = std::numeric_limits<std::int64_t>::min();
    model.set_truth(at_epoch(hardware::VelocityUmps{rate, 0, 0}));
    ASSERT_EQ(clock.advance(1ns), ares::core::AdvanceStatus::Applied);
    const auto state = model.state_now();
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->position.x, kInt64MinAtOneNanosecond);
    EXPECT_NE(state->position.x, rate);
}

TEST(SpacecraftIntegration, AddingInt64MinToBase) {
    const auto kMin = std::numeric_limits<std::int64_t>::min();
    const auto read_x = [](std::int64_t base) {
        Clock clock;
        simulation::SpacecraftModel<Clock> model(clock);
        // One second makes the quotient equal the rate, which is INT64_MIN.
        model.set_truth(
            at_epoch(hardware::VelocityUmps{kMin, 0, 0}, hardware::PositionUm{base, 0, 0}));
        EXPECT_EQ(clock.advance(1s), ares::core::AdvanceStatus::Applied);
        const auto state = model.state_now();
        EXPECT_TRUE(state.has_value());
        return state ? state->position.x : 0;
    };
    EXPECT_EQ(read_x(0), kMin);
    EXPECT_EQ(read_x(42), kMin + 42);
    EXPECT_EQ(read_x(-1), kMin);
}

TEST(SpacecraftIntegration, PositiveOverflowSaturates) {
    const auto kMax = std::numeric_limits<std::int64_t>::max();
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    model.set_truth(at_epoch(hardware::VelocityUmps{kMax, 0, 0}));
    ASSERT_EQ(clock.advance(2s), ares::core::AdvanceStatus::Applied);
    const auto saturated = model.state_now();
    ASSERT_TRUE(saturated.has_value());
    EXPECT_EQ(saturated->position.x, kMax);

    Clock adding;
    simulation::SpacecraftModel<Clock> sum(adding);
    sum.set_truth(at_epoch(hardware::VelocityUmps{10, 0, 0}, hardware::PositionUm{kMax - 5, 0, 0}));
    ASSERT_EQ(adding.advance(1s), ares::core::AdvanceStatus::Applied);
    const auto added = sum.state_now();
    ASSERT_TRUE(added.has_value());
    EXPECT_EQ(added->position.x, kMax);
}

TEST(SpacecraftIntegration, NegativeOverflowSaturates) {
    const auto kMin = std::numeric_limits<std::int64_t>::min();
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    model.set_truth(at_epoch(hardware::VelocityUmps{kMin, 0, 0}));
    ASSERT_EQ(clock.advance(2s), ares::core::AdvanceStatus::Applied);
    const auto saturated = model.state_now();
    ASSERT_TRUE(saturated.has_value());
    EXPECT_EQ(saturated->position.x, kMin);

    Clock adding;
    simulation::SpacecraftModel<Clock> sum(adding);
    sum.set_truth(
        at_epoch(hardware::VelocityUmps{-10, 0, 0}, hardware::PositionUm{kMin + 5, 0, 0}));
    ASSERT_EQ(adding.advance(1s), ares::core::AdvanceStatus::Applied);
    const auto added = sum.state_now();
    ASSERT_TRUE(added.has_value());
    EXPECT_EQ(added->position.x, kMin);
}

TEST(SpacecraftIntegration, TimeBeforeEpochIsNotAState) {
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    auto truth = at_epoch(hardware::VelocityUmps{1000, 0, 0});
    truth.epoch = Time{1ns};
    model.set_truth(truth);
    EXPECT_FALSE(model.state_now().has_value());
    simulation::SimulatedImu<Clock> imu(model);
    simulation::SimulatedGps<Clock> gps(model);
    EXPECT_EQ(imu.read().status, hardware::SensorStatus::Unavailable);
    EXPECT_EQ(gps.read().status, hardware::SensorStatus::Unavailable);

    ASSERT_EQ(clock.advance(1ns), ares::core::AdvanceStatus::Applied);
    const auto state = model.state_now();
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->time, Time{1ns});
    EXPECT_EQ(state->position, (hardware::PositionUm{0, 0, 0}));
    EXPECT_EQ(imu.read().status, hardware::SensorStatus::Valid);
}
