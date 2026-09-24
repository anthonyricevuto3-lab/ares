#include "ares/core/task_supervisor.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <semaphore>
#include <stop_token>
#include <thread>

#include <gtest/gtest.h>

namespace core = ares::core;
using namespace std::chrono_literals;
using Supervisor = core::TaskSupervisor<core::ManualClock>;

TEST(TaskSupervisor, ShutdownWithNoTasksIsANoOp) {
    core::ManualClock clock;
    Supervisor supervisor(clock);
    EXPECT_EQ(supervisor.worker_count(), 0U);
    EXPECT_EQ(supervisor.start(), core::StartStatus::Ok);
    EXPECT_TRUE(supervisor.started());
    supervisor.shutdown();
    EXPECT_EQ(supervisor.stopped_workers(), 0U);
    EXPECT_EQ(supervisor.start(), core::StartStatus::AlreadyStarted);
}

TEST(TaskSupervisor, RejectsInvalidWorkAndRegistrationAfterStart) {
    core::ManualClock clock;
    Supervisor supervisor(clock);
    EXPECT_EQ(supervisor.add("", core::TaskTiming{1ms, 1ms},
                             [](core::ManualClock::time_point, std::stop_token) {}, {}),
              core::AddStatus::Invalid);
    EXPECT_EQ(
        supervisor.add("nav", core::TaskTiming{1ms, 1ms}, core::TaskWork<core::ManualClock>{}, {}),
        core::AddStatus::Invalid);
    ASSERT_EQ(supervisor.add("nav", core::TaskTiming{1ms, 1ms},
                             [](core::ManualClock::time_point, std::stop_token) {}, {}),
              core::AddStatus::Ok);
    ASSERT_EQ(supervisor.start(), core::StartStatus::Ok);
    EXPECT_EQ(supervisor.add("health", core::TaskTiming{1ms, 1ms},
                             [](core::ManualClock::time_point, std::stop_token) {}, {}),
              core::AddStatus::Started);
    supervisor.shutdown();
    EXPECT_EQ(supervisor.stopped_workers(), 1U);
    EXPECT_EQ(supervisor.worker_count(), 1U);
}

TEST(TaskSupervisor, FullTableRejectsAnotherWorker) {
    core::ManualClock clock;
    Supervisor supervisor(clock);
    for (std::size_t index = 0; index < Supervisor::kMaxWorkers; ++index) {
        ASSERT_EQ(supervisor.add("nav", core::TaskTiming{1ms, 1ms},
                                 [](core::ManualClock::time_point, std::stop_token) {}, {}),
                  core::AddStatus::Ok);
    }
    EXPECT_EQ(supervisor.add("extra", core::TaskTiming{1ms, 1ms},
                             [](core::ManualClock::time_point, std::stop_token) {}, {}),
              core::AddStatus::Full);
}

TEST(TaskSupervisor, StopUnblocksAWaitingTaskAndJoinsIt) {
    core::ManualClock clock;
    std::binary_semaphore entered{0};
    Supervisor supervisor(clock);
    ASSERT_EQ(supervisor.add(
                  "slow", core::TaskTiming{1h, 1h},
                  [&](core::ManualClock::time_point, std::stop_token) { entered.release(); }, {}),
              core::AddStatus::Ok);
    ASSERT_EQ(supervisor.start(), core::StartStatus::Ok);
    ASSERT_TRUE(entered.try_acquire_for(5s));

    const core::ShutdownReport report = supervisor.shutdown();
    EXPECT_EQ(report.missed_grace, 0U);
    EXPECT_EQ(supervisor.stopped_workers(), supervisor.worker_count());
    EXPECT_EQ(supervisor.health(0), core::WorkerHealth::Stopped);
    const auto frozen = supervisor.completed_cycles();
    ASSERT_EQ(clock.advance(2h), core::AdvanceStatus::Applied);
    EXPECT_EQ(supervisor.completed_cycles(), frozen);
}

TEST(TaskSupervisor, ThrowingTaskDoesNotTerminateAndStopsTheSupervisor) {
    const auto previous = std::set_terminate([] { std::abort(); });
    core::ManualClock clock;
    std::binary_semaphore entered{0};
    Supervisor supervisor(clock);
    ASSERT_EQ(supervisor.add("boom", core::TaskTiming{1h, 1h},
                             [&](core::ManualClock::time_point, std::stop_token) {
                                 entered.release();
                                 throw std::runtime_error("task failed");
                             },
                             {}),
              core::AddStatus::Ok);
    ASSERT_EQ(supervisor.add("peer", core::TaskTiming{1h, 1h},
                             [](core::ManualClock::time_point, std::stop_token) {}, {}),
              core::AddStatus::Ok);
    ASSERT_EQ(supervisor.start(), core::StartStatus::Ok);
    ASSERT_TRUE(entered.try_acquire_for(5s));
    supervisor.shutdown();
    ASSERT_TRUE(supervisor.fault(0).has_value());
    EXPECT_EQ(*supervisor.fault(0), core::WorkerFault::Exception);
    EXPECT_EQ(supervisor.health(0), core::WorkerHealth::Faulted);
    EXPECT_EQ(supervisor.health(1), core::WorkerHealth::Stopped);
    std::set_terminate(previous);
}

TEST(TaskSupervisor, UncooperativeWorkIsReportedHungUntilItReturns) {
    core::ManualClock clock;
    std::binary_semaphore entered{0};
    std::binary_semaphore release{0};
    Supervisor supervisor(clock);
    ASSERT_EQ(supervisor.add("hung", core::TaskTiming{1h, 1h},
                             [&](core::ManualClock::time_point, std::stop_token) {
                                 entered.release();
                                 release.acquire();
                             },
                             {}),
              core::AddStatus::Ok);
    ASSERT_EQ(supervisor.start(), core::StartStatus::Ok);
    ASSERT_TRUE(entered.try_acquire_for(5s));
    supervisor.request_stop();
    EXPECT_EQ(supervisor.health(0), core::WorkerHealth::Hung);
    release.release();
    supervisor.join();
    EXPECT_EQ(supervisor.health(0), core::WorkerHealth::Stopped);
}

namespace {

std::atomic<int> launch_fail_budget{0};

void fail_second_worker_once(std::size_t index) {
    if (index == 1 && launch_fail_budget.fetch_sub(1) > 0) {
        throw std::runtime_error("launch failed");
    }
}

} // namespace

TEST(TaskSupervisor, FailedLaunchRollsBackToARestartableState) {
    core::ManualClock clock;
    launch_fail_budget.store(1);
    std::atomic<int> runs{0};
    Supervisor supervisor(clock, fail_second_worker_once);
    ASSERT_EQ(supervisor.add("one", core::TaskTiming{1h, 1h},
                             [&](core::ManualClock::time_point, std::stop_token) { ++runs; }, {}),
              core::AddStatus::Ok);
    ASSERT_EQ(supervisor.add("two", core::TaskTiming{1h, 1h},
                             [&](core::ManualClock::time_point, std::stop_token) { ++runs; }, {}),
              core::AddStatus::Ok);
    EXPECT_EQ(supervisor.start(), core::StartStatus::Failed);
    EXPECT_FALSE(supervisor.started());
    EXPECT_EQ(runs.load(), 0);
    EXPECT_EQ(supervisor.health(0), core::WorkerHealth::Stopped);
    ASSERT_TRUE(supervisor.fault(0).has_value());
    EXPECT_EQ(*supervisor.fault(0), core::WorkerFault::None);
    EXPECT_FALSE(supervisor.fault(9).has_value());
    EXPECT_EQ(supervisor.health(9), core::WorkerHealth::Invalid);
    EXPECT_EQ(supervisor.start(), core::StartStatus::Ok);
    EXPECT_TRUE(supervisor.started());
    supervisor.shutdown();
    EXPECT_EQ(supervisor.stopped_workers(), 2U);
}

TEST(TaskSupervisor, WorkObservesCancellation) {
    core::ManualClock clock;
    std::binary_semaphore entered{0};
    std::atomic<bool> saw_stop{false};
    Supervisor supervisor(clock);
    ASSERT_EQ(supervisor.add("watch", core::TaskTiming{1h, 1h},
                             [&](core::ManualClock::time_point, std::stop_token stop) {
                                 entered.release();
                                 std::mutex mutex;
                                 std::condition_variable_any cv;
                                 std::stop_callback callback(stop, [&cv] { cv.notify_all(); });
                                 std::unique_lock lock(mutex);
                                 cv.wait(lock, stop, [] { return false; });
                                 saw_stop.store(stop.stop_requested());
                             },
                             {}),
              core::AddStatus::Ok);
    ASSERT_EQ(supervisor.start(), core::StartStatus::Ok);
    ASSERT_TRUE(entered.try_acquire_for(5s));
    supervisor.shutdown();
    EXPECT_TRUE(saw_stop.load());
    EXPECT_EQ(supervisor.health(0), core::WorkerHealth::Stopped);
}

TEST(TaskSupervisor, ShutdownReportsAMissedGraceThatLaterExits) {
    core::ManualClock clock;
    std::binary_semaphore entered{0};
    std::binary_semaphore release{0};
    Supervisor supervisor(clock);
    ASSERT_EQ(supervisor.add("hung", core::TaskTiming{1h, 1h},
                             [&](core::ManualClock::time_point, std::stop_token) {
                                 entered.release();
                                 release.acquire();
                             },
                             {}),
              core::AddStatus::Ok);
    ASSERT_EQ(supervisor.start(), core::StartStatus::Ok);
    ASSERT_TRUE(entered.try_acquire_for(5s));

    core::ShutdownReport report;
    std::jthread stopper([&] { report = supervisor.shutdown(); });
    std::this_thread::sleep_for(Supervisor::kStopGrace + 30ms);
    release.release();
    stopper.join();
    ASSERT_EQ(report.considered, 1U);
    EXPECT_EQ(report.missed_grace, 1U);
    EXPECT_TRUE(report.workers[0].missed_grace);
    EXPECT_TRUE(report.workers[0].exited);
    EXPECT_EQ(report.workers[0].phase_after_grace, core::WorkerPhase::InWork);
    EXPECT_EQ(supervisor.health(0), core::WorkerHealth::Stopped);
}
