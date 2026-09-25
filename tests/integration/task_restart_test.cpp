#include "ares/core/task_supervisor.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST(TaskRestart, HandoffDoesNotOverlapGenerations) {
    ares::core::ManualClock clock;
    ares::core::NavigationRestart restart;
    std::vector<int> sequence;
    std::atomic<int> depth{0};
    int max_depth = 0;
    auto id = ares::core::TaskId::make("navigation");
    ASSERT_TRUE(id.has_value());
    ares::core::PeriodicTask<ares::core::ManualClock> task(
        *id, ares::core::TaskTiming{100ms, 100ms},
        [&](ares::core::ManualClock::time_point, std::stop_token) {
            const int now = depth.fetch_add(1) + 1;
            max_depth = std::max(max_depth, now);
            sequence.push_back(static_cast<int>(restart.generation.load()));
            depth.fetch_sub(1);
            sequence.push_back(-1);
        },
        clock, {});
    ASSERT_EQ(task.arm(ares::core::ManualClock::time_point{}), ares::core::ArmStatus::Armed);
    EXPECT_EQ(task.poll(), ares::core::PollResult::Ran);
    EXPECT_TRUE(restart.request());
    EXPECT_FALSE(restart.request());
    EXPECT_TRUE(restart.handoff(task, clock, {}));
    EXPECT_EQ(restart.generation.load(), 1U);
    EXPECT_FALSE(restart.handoff(task, clock, {}));
    ASSERT_EQ(clock.advance(100ms), ares::core::AdvanceStatus::Applied);
    EXPECT_EQ(task.poll(), ares::core::PollResult::Ran);
    EXPECT_EQ(max_depth, 1);
    ASSERT_EQ(sequence.size(), 4U);
    EXPECT_EQ(sequence[0], 0);
    EXPECT_EQ(sequence[1], -1);
    EXPECT_EQ(sequence[2], 1);
    EXPECT_EQ(sequence[3], -1);
}

TEST(TaskRestart, StopPreventsTheNextGeneration) {
    ares::core::ManualClock clock;
    ares::core::NavigationRestart restart;
    auto id = ares::core::TaskId::make("navigation");
    ASSERT_TRUE(id.has_value());
    ares::core::PeriodicTask<ares::core::ManualClock> task(
        *id, ares::core::TaskTiming{100ms, 100ms},
        [](ares::core::ManualClock::time_point, std::stop_token) {}, clock, {});
    ASSERT_EQ(task.arm(ares::core::ManualClock::time_point{}), ares::core::ArmStatus::Armed);
    EXPECT_EQ(task.poll(), ares::core::PollResult::Ran);
    EXPECT_TRUE(restart.request());
    std::stop_source stop;
    stop.request_stop();
    EXPECT_FALSE(restart.handoff(task, clock, stop.get_token()));
    EXPECT_EQ(restart.generation.load(), 0U);
}

TEST(TaskRestart, SupervisorRestartUsesTheSameWorker) {
    ares::core::ManualClock clock;
    ares::core::TaskSupervisor<ares::core::ManualClock> supervisor(clock);
    std::atomic<int> depth{0};
    std::atomic<int> max_depth{0};
    std::atomic<std::uint32_t> worked_generation{0};
    ASSERT_EQ(supervisor.add("navigation", ares::core::TaskTiming{1h, 1h},
                             [&](ares::core::ManualClock::time_point, std::stop_token) {
                                 const int now = depth.fetch_add(1) + 1;
                                 int seen = max_depth.load();
                                 while (seen < now && !max_depth.compare_exchange_weak(seen, now)) {
                                 }
                                 worked_generation.store(supervisor.navigation_generation());
                                 depth.fetch_sub(1);
                             },
                             {}),
              ares::core::AddStatus::Ok);
    ASSERT_EQ(supervisor.start(), ares::core::StartStatus::Ok);
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (supervisor.completed_cycles() < 1) {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline);
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(20ms);
    EXPECT_TRUE(supervisor.request_navigation_restart());
    EXPECT_FALSE(supervisor.request_navigation_restart());
    ASSERT_EQ(clock.advance(1h), ares::core::AdvanceStatus::Applied);
    while (supervisor.navigation_generation() != 1U) {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline);
        std::this_thread::yield();
    }
    ASSERT_EQ(clock.advance(1h), ares::core::AdvanceStatus::Applied);
    while (worked_generation.load() != 1U) {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline);
        std::this_thread::yield();
    }
    EXPECT_EQ(max_depth.load(), 1);
    supervisor.shutdown();
}

TEST(TaskRestart, ShutdownWhileRestartIsPendingDoesNotArmAnotherGeneration) {
    ares::core::ManualClock clock;
    ares::core::TaskSupervisor<ares::core::ManualClock> supervisor(clock);
    ASSERT_EQ(supervisor.add("navigation", ares::core::TaskTiming{1h, 1h},
                             [](ares::core::ManualClock::time_point, std::stop_token) {}, {}),
              ares::core::AddStatus::Ok);
    ASSERT_EQ(supervisor.start(), ares::core::StartStatus::Ok);
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (supervisor.completed_cycles() < 1) {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline);
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(20ms);
    EXPECT_TRUE(supervisor.request_navigation_restart());
    supervisor.shutdown();
    EXPECT_EQ(supervisor.navigation_generation(), 0U);
}
