#include "ares/core/clock.hpp"
#include "ares/core/deadline_monitor.hpp"

#include <chrono>

#include <gtest/gtest.h>

namespace core = ares::core;
using namespace std::chrono_literals;
using Time = core::ManualClock::time_point;

TEST(DeadlineMonitor, ExactDeadlineIsOnTime) {
    const Time scheduled{};
    EXPECT_FALSE(core::deadline_missed(scheduled, Time{10ms}, 10ms));
}

TEST(DeadlineMonitor, OneNanosecondLateIsAMiss) {
    const Time scheduled{};
    const Time completed{10ms + 1ns};
    EXPECT_TRUE(core::deadline_missed(scheduled, completed, 10ms));
}

TEST(DeadlineMonitor, ZeroElapsedIsOnTime) {
    const Time scheduled{};
    EXPECT_FALSE(core::deadline_missed(scheduled, scheduled, 1ns));
}

TEST(DeadlineMonitor, CompletionBeforeReleaseIsUnusable) {
    EXPECT_FALSE(core::deadline_missed(Time{5ms}, Time{}, 1ms));
    EXPECT_EQ(core::deadline_status(Time{5ms}, Time{}, 1ms), core::DeadlineStatus::Unusable);
}

TEST(DeadlineMonitor, NegativeDeadlineIsAMiss) {
    EXPECT_TRUE(core::deadline_missed(Time{}, Time{}, core::Duration{-1}));
}
