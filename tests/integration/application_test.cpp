#include "ares/application.hpp"

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
    EXPECT_NE(text.find("ARES 0.7.0\n"), std::string::npos);
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
    std::ostringstream captured;
    std::streambuf* const previous = std::cerr.rdbuf(captured.rdbuf());
    const int code = ares::run(3, argv);
    std::cerr.rdbuf(previous);
    EXPECT_EQ(code, ares::to_int(ares::ExitCode::RecorderFailed));
    EXPECT_NE(captured.str().find("record open failed"), std::string::npos);
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
