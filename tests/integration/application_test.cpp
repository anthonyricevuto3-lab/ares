#include "ares/application.hpp"
#include "ares/core/bounded_log.hpp"
#include "ares/core/task_events.hpp"
#include "ares/flight/recovery.hpp"

#include <iostream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

TEST(Application, HelpExitsZero) {
    char arg0[] = "ares";
    char arg1[] = "--help";
    char* argv[] = {arg0, arg1};
    std::ostringstream captured;
    std::streambuf* const previous = std::cout.rdbuf(captured.rdbuf());
    const int code = ares::run(2, argv);
    std::cout.rdbuf(previous);

    EXPECT_EQ(code, 0);
    EXPECT_NE(captured.str().find("Usage:"), std::string::npos);
}

TEST(Application, UnknownArgumentExitsTwo) {
    char arg0[] = "ares";
    char arg1[] = "--orbit";
    char* argv[] = {arg0, arg1};
    std::ostringstream captured;
    std::streambuf* const previous = std::cerr.rdbuf(captured.rdbuf());
    const int code = ares::run(2, argv);
    std::cerr.rdbuf(previous);

    EXPECT_EQ(code, 2);
    EXPECT_NE(captured.str().find("unknown argument"), std::string::npos);
}

TEST(Application, ZeroDurationBootsAndShutsDown) {
    char arg0[] = "ares";
    char arg1[] = "--duration-ms";
    char arg2[] = "0";
    char* argv[] = {arg0, arg1, arg2};
    std::ostringstream captured;
    std::streambuf* const previous = std::cout.rdbuf(captured.rdbuf());
    const int code = ares::run(3, argv);
    std::cout.rdbuf(previous);

    EXPECT_EQ(code, 0);
    const std::string text = captured.str();
    EXPECT_NE(text.find("Boot -> Initialization"), std::string::npos);
    EXPECT_NE(text.find("Initialization -> Standby"), std::string::npos);
    EXPECT_NE(text.find("StartMission accepted"), std::string::npos);
    EXPECT_NE(text.find("Standby -> Nominal"), std::string::npos);
    EXPECT_NE(text.find("shutdown complete"), std::string::npos);
    EXPECT_NE(text.find("miss_overwrites=0"), std::string::npos);
    EXPECT_NE(text.find("ARES 1.0.0\n"), std::string::npos);
    EXPECT_NE(text.find("scenario: nominal\n"), std::string::npos);
    EXPECT_NE(text.find("seed: 0\n"), std::string::npos);
    EXPECT_NE(text.find("final_mode: Nominal\n"), std::string::npos);
    EXPECT_NE(text.find("active_gps: primary\n"), std::string::npos);
    EXPECT_NE(text.find("recording: disabled\n"), std::string::npos);
    EXPECT_NE(text.find("exit: Success\n"), std::string::npos);
}

TEST(Application, ListScenariosDoesNotStartAMission) {
    char arg0[] = "ares";
    char arg1[] = "--list-scenarios";
    char* argv[] = {arg0, arg1};
    std::ostringstream captured;
    std::streambuf* const previous = std::cout.rdbuf(captured.rdbuf());
    const int code = ares::run(2, argv);
    std::cout.rdbuf(previous);
    EXPECT_EQ(code, 0);
    EXPECT_NE(captured.str().find("gps_stale"), std::string::npos);
    EXPECT_EQ(captured.str().find("tasks started"), std::string::npos);
}

TEST(Application, RecordOpenFailureDoesNotStartAMission) {
    char arg0[] = "ares";
    char arg1[] = "--record";
    char arg2[] = "no_such_ares_record_dir/mission.bin";
    char* argv[] = {arg0, arg1, arg2};
    std::ostringstream errors;
    std::ostringstream output;
    std::streambuf* const previous_err = std::cerr.rdbuf(errors.rdbuf());
    std::streambuf* const previous_out = std::cout.rdbuf(output.rdbuf());
    const int code = ares::run(3, argv);
    std::cerr.rdbuf(previous_err);
    std::cout.rdbuf(previous_out);
    EXPECT_EQ(code, ares::to_int(ares::ExitCode::RecorderFailed));
    EXPECT_NE(errors.str().find("record open failed"), std::string::npos);
    EXPECT_EQ(output.str().find("tasks started"), std::string::npos);
}

TEST(ApplicationExit, NamesMatchTheStableCodes) {
    EXPECT_EQ(ares::to_int(ares::ExitCode::Success), 0);
    EXPECT_EQ(ares::to_int(ares::ExitCode::BootFailed), 1);
    EXPECT_EQ(ares::to_int(ares::ExitCode::UsageError), 2);
    EXPECT_EQ(ares::to_int(ares::ExitCode::WorkerException), 3);
    EXPECT_EQ(ares::to_int(ares::ExitCode::ScheduleFault), 4);
    EXPECT_EQ(ares::to_int(ares::ExitCode::HookFault), 5);
    EXPECT_EQ(ares::to_int(ares::ExitCode::UnknownWorkerFault), 6);
    EXPECT_EQ(ares::to_int(ares::ExitCode::StartupFailed), 7);
    EXPECT_EQ(ares::to_int(ares::ExitCode::TimeError), 8);
    EXPECT_EQ(ares::to_int(ares::ExitCode::FaultHistoryOverflow), 9);
    EXPECT_EQ(ares::to_int(ares::ExitCode::RecorderFailed), 10);
    EXPECT_EQ(ares::exit_code_name(ares::ExitCode::RecorderFailed), "RecorderFailed");
    EXPECT_EQ(ares::exit_code_name(ares::ExitCode::Success), "Success");
}

TEST(ApplicationExit, CombinePrefersWorkerFaultOverMissHistory) {
    EXPECT_EQ(ares::combine_exit(ares::ExitCode::Success, false), ares::ExitCode::Success);
    EXPECT_EQ(ares::combine_exit(ares::ExitCode::Success, true),
              ares::ExitCode::FaultHistoryOverflow);
    EXPECT_EQ(ares::combine_exit(ares::ExitCode::WorkerException, true),
              ares::ExitCode::WorkerException);
}

TEST(ApplicationExit, RecoveryFailedLeavesSuccessWhenNoOtherFault) {
    using Time = ares::core::ManualClock::time_point;
    ares::flight::RecoveryManager<Time> recovery;
    ares::core::EventLog<ares::flight::SystemEvent<Time>> events;
    const ares::flight::GpsRecoveryFact gps{};
    auto critical = [](std::uint32_t generation, std::uint32_t consecutive) {
        ares::flight::NavigationRecoveryFact fact;
        fact.active = true;
        fact.critical = true;
        fact.generation = generation;
        fact.consecutive = consecutive;
        return fact;
    };
    (void)recovery.observe(critical(0, 5), gps, Time{}, events);
    ares::flight::NavigationRecoveryFact fact = critical(1, 5);
    (void)recovery.observe(fact, gps, Time{}, events);
    fact.consecutive = 10;
    (void)recovery.observe(fact, gps, Time{}, events);
    fact.generation = 2;
    fact.consecutive = 10;
    (void)recovery.observe(fact, gps, Time{}, events);
    fact.consecutive = 15;
    const ares::flight::RecoveryCommand failed = recovery.observe(fact, gps, Time{}, events);
    EXPECT_FALSE(failed.restart_navigation);
    EXPECT_EQ(recovery.navigation_state(), ares::flight::RecoveryState::Failed);
    EXPECT_EQ(recovery.navigation_attempts(), 2);

    ares::core::ManualClock clock;
    ares::core::TaskSupervisor<ares::core::ManualClock> supervisor(clock);
    EXPECT_EQ(supervisor.worker_count(), 0U);
    EXPECT_EQ(ares::exit_code_for(supervisor), ares::ExitCode::Success);
    EXPECT_NE(ares::exit_code_for(supervisor), ares::ExitCode::WorkerException);
    EXPECT_NE(ares::exit_code_for(supervisor), ares::ExitCode::ScheduleFault);

    using Miss = ares::core::DeadlineMissEvent<ares::core::SteadyClock::time_point>;
    ares::core::BoundedLog<Miss, 32> misses;
    EXPECT_FALSE(misses.overflowed());
    const ares::ExitCode outcome =
        ares::combine_exit(ares::exit_code_for(supervisor), misses.overflowed());
    EXPECT_EQ(outcome, ares::ExitCode::Success);
    EXPECT_NE(outcome, ares::ExitCode::RecorderFailed);
    EXPECT_NE(outcome, ares::ExitCode::FaultHistoryOverflow);
}
