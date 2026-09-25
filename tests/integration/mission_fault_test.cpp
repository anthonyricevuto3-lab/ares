#include "ares/application.hpp"
#include "ares/core/task_supervisor.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <semaphore>
#include <sstream>
#include <string>
#include <stop_token>

#include <gtest/gtest.h>

namespace core = ares::core;
using namespace std::chrono_literals;

namespace {

int run_with(ares::InjectedFault fault, const char* duration_ms) {
    char arg0[] = "ares";
    char arg1[] = "--duration-ms";
    char arg2[16]{};
    for (std::size_t index = 0; duration_ms[index] != '\0' && index + 1 < sizeof(arg2); ++index) {
        arg2[index] = duration_ms[index];
    }
    char* argv[] = {arg0, arg1, arg2};
    std::ostringstream captured;
    std::streambuf* const previous = std::cout.rdbuf(captured.rdbuf());
    const int code = ares::run(3, argv, fault);
    std::cout.rdbuf(previous);
    return code;
}

} // namespace

TEST(MissionExit, NormalRunIsSuccess) {
    EXPECT_EQ(run_with(ares::InjectedFault::None, "0"), ares::to_int(ares::ExitCode::Success));
}

TEST(MissionExit, ThrowingWorkerThroughRunIsNonzero) {
    const auto previous = std::set_terminate([] { std::abort(); });
    const int code = run_with(ares::InjectedFault::WorkerThrows, "60000");
    std::set_terminate(previous);
    EXPECT_EQ(code, ares::to_int(ares::ExitCode::WorkerException));
    EXPECT_NE(code, 0);
}

TEST(MissionExit, ScheduleFaultThroughRunIsNonzero) {
    const int code = run_with(ares::InjectedFault::ScheduleOverflow, "60000");
    EXPECT_EQ(code, ares::to_int(ares::ExitCode::ScheduleFault));
    EXPECT_NE(code, 0);
}

struct CapturedRun {
    int code{0};
    std::string log{};
};

CapturedRun capture_run(ares::InjectedFault fault, const char* duration_ms) {
    char arg0[] = "ares";
    char arg1[] = "--duration-ms";
    char arg2[16]{};
    for (std::size_t index = 0; duration_ms[index] != '\0' && index + 1 < sizeof(arg2); ++index) {
        arg2[index] = duration_ms[index];
    }
    char* argv[] = {arg0, arg1, arg2};
    std::ostringstream captured;
    std::streambuf* const previous = std::cout.rdbuf(captured.rdbuf());
    const int code = ares::run(3, argv, fault);
    std::cout.rdbuf(previous);
    return CapturedRun{code, captured.str()};
}

TEST(MissionExit, NormalScenarioReportsCompleted) {
    const CapturedRun run = capture_run(ares::InjectedFault::None, "0");
    EXPECT_EQ(run.code, ares::to_int(ares::ExitCode::Success));
    EXPECT_NE(run.log.find("completed=1"), std::string::npos);
    EXPECT_EQ(run.log.find("completed=0"), std::string::npos);
}

TEST(MissionExit, WorkerExceptionReportsNotCompleted) {
    const auto previous = std::set_terminate([] { std::abort(); });
    const CapturedRun run = capture_run(ares::InjectedFault::WorkerThrows, "60000");
    std::set_terminate(previous);
    EXPECT_EQ(run.code, ares::to_int(ares::ExitCode::WorkerException));
    EXPECT_NE(run.log.find("completed=0"), std::string::npos);
    EXPECT_EQ(run.log.find("completed=1"), std::string::npos);
}

TEST(MissionExit, ScheduleFaultReportsNotCompleted) {
    const CapturedRun run = capture_run(ares::InjectedFault::ScheduleOverflow, "60000");
    EXPECT_EQ(run.code, ares::to_int(ares::ExitCode::ScheduleFault));
    EXPECT_NE(run.log.find("completed=0"), std::string::npos);
    EXPECT_EQ(run.log.find("completed=1"), std::string::npos);
}

TEST(MissionExit, ScheduleFaultKeepsTheDeadlineMiss) {
    core::ManualClock clock;
    const core::Duration almost = core::Duration::max() - 50ns;
    ASSERT_EQ(clock.advance(almost), core::AdvanceStatus::Applied);
    std::binary_semaphore entered{0};
    core::TaskSupervisor<core::ManualClock> supervisor(clock);
    ASSERT_EQ(supervisor.add("edge", core::TaskTiming{100ns, 10ns},
                             [&](core::ManualClock::time_point, std::stop_token) {
                                 EXPECT_EQ(clock.advance(11ns), core::AdvanceStatus::Applied);
                                 entered.release();
                             },
                             {}),
              core::AddStatus::Ok);
    ASSERT_EQ(supervisor.start(), core::StartStatus::Ok);
    ASSERT_TRUE(entered.try_acquire_for(5s));
    (void)supervisor.shutdown();
    ASSERT_TRUE(supervisor.fault(0).has_value());
    EXPECT_EQ(*supervisor.fault(0), core::WorkerFault::Schedule);
    const auto record = supervisor.deadline_record(0);
    ASSERT_TRUE(record.has_value());
    EXPECT_TRUE(record->recorded);
    EXPECT_TRUE(record->missed);
}
