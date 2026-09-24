#include "ares/core/clock.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
#include <stop_token>
#include <thread>

#include <gtest/gtest.h>

namespace core = ares::core;
using namespace std::chrono_literals;
using Time = core::ManualClock::time_point;

TEST(ManualClock, StartsAtEpochAndAdvancesForward) {
    core::ManualClock clock;
    EXPECT_EQ(clock.now().status, core::ClockStatus::Ok);
    EXPECT_EQ(clock.now().time, Time{});
    EXPECT_EQ(clock.advance(5ms), core::AdvanceStatus::Applied);
    EXPECT_EQ(clock.now().time, Time{5ms});
    EXPECT_EQ(clock.advance(core::Duration::zero()), core::AdvanceStatus::Applied);
    EXPECT_EQ(clock.now().time, Time{5ms});
}

TEST(CheckedTime, RejectsAStepPastTheRepresentableRange) {
    Time out{};
    EXPECT_FALSE(core::checked_time_add(Time{core::Duration::min()}, core::Duration{-1}, out));
    EXPECT_FALSE(core::checked_time_add(Time{core::Duration::max()}, core::Duration{1}, out));
    EXPECT_TRUE(core::checked_time_add(Time{}, core::Duration::max(), out));
    EXPECT_EQ(out, Time{core::Duration::max()});
}

TEST(ManualClock, RejectsNegativeAdvance) {
    core::ManualClock clock;
    ASSERT_EQ(clock.advance(2ms), core::AdvanceStatus::Applied);
    EXPECT_EQ(clock.advance(core::Duration{-1}), core::AdvanceStatus::Negative);
    EXPECT_EQ(clock.now().time, Time{2ms});
}

TEST(ManualClock, WaitReturnsImmediatelyWhenTargetIsAlreadyReached) {
    core::ManualClock clock;
    ASSERT_EQ(clock.advance(10ms), core::AdvanceStatus::Applied);
    EXPECT_EQ(clock.wait_until(Time{5ms}, std::stop_token{}), core::ClockStatus::Ok);
    EXPECT_EQ(clock.now().time, Time{10ms});
}

TEST(ManualClock, AdvanceReleasesWaiter) {
    core::ManualClock clock;
    std::jthread worker([&clock] {
        EXPECT_EQ(clock.wait_until(Time{1h}, std::stop_token{}), core::ClockStatus::Ok);
    });
    ASSERT_EQ(clock.advance(1h), core::AdvanceStatus::Applied);
    worker.join();
    EXPECT_EQ(clock.now().time, Time{1h});
}

TEST(ManualClock, StopReleasesWaiterWithoutAdvancing) {
    core::ManualClock clock;
    std::stop_source source;
    std::jthread worker([&clock, &source] {
        EXPECT_EQ(clock.wait_until(Time{1h}, source.get_token()), core::ClockStatus::Ok);
    });
    source.request_stop();
    worker.join();
    EXPECT_EQ(clock.now().time, Time{});
}

TEST(SteadyClock, NowIsMonotonic) {
    core::SteadyClock clock;
    const core::ClockSample<core::SteadyClock::time_point> first = clock.now();
    const core::ClockSample<core::SteadyClock::time_point> second = clock.now();
    ASSERT_EQ(first.status, core::ClockStatus::Ok);
    ASSERT_EQ(second.status, core::ClockStatus::Ok);
    EXPECT_LE(first.time, second.time);
}

TEST(SteadyClock, AlreadyStoppedWaitReturns) {
    core::SteadyClock clock;
    std::stop_source source;
    source.request_stop();
    const auto sample = clock.now();
    ASSERT_EQ(sample.status, core::ClockStatus::Ok);
    EXPECT_EQ(clock.wait_until(sample.time + 1h, source.get_token()), core::ClockStatus::Ok);
}

TEST(HostClockConversion, AcceptsARepresentableNanosecondValue) {
    core::Duration out{};
    EXPECT_TRUE(core::checked_duration_convert(core::Duration{42}, out));
    EXPECT_EQ(out, core::Duration{42});
    std::chrono::milliseconds coarse{};
    EXPECT_TRUE(core::checked_duration_convert(core::Duration{2'500'000}, coarse));
    EXPECT_EQ(coarse, 2ms);
}

TEST(HostClockConversion, RejectsADurationThatCannotBecomeNanoseconds) {
    using Seconds = std::chrono::duration<std::int64_t>;
    core::Duration out{};
    EXPECT_FALSE(
        core::checked_duration_convert(Seconds{std::numeric_limits<std::int64_t>::max()}, out));
    EXPECT_TRUE(core::checked_duration_convert(Seconds{1}, out));
    EXPECT_EQ(out, 1s);
}
