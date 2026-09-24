#include "ares/flight/executive.hpp"

#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace core = ares::core;
namespace flight = ares::flight;
using Executive = flight::FlightExecutive<core::ManualClock>;

namespace {

class ExecutiveFixture : public ::testing::Test {
protected:
    core::ManualClock clock;
    std::ostringstream output;
    core::Logger<core::ManualClock> logger{output, clock};
    core::EventLog<flight::SystemEvent<core::ManualClock::time_point>> events;
    Executive executive{clock, logger, events};
};

template <typename Event>
std::vector<Event>
of_type(const std::vector<flight::SystemEvent<core::ManualClock::time_point>>& recorded) {
    std::vector<Event> selected;
    for (const flight::SystemEvent<core::ManualClock::time_point>& event : recorded) {
        if (const auto* value = std::get_if<Event>(&event)) {
            selected.push_back(*value);
        }
    }
    return selected;
}

} // namespace

TEST_F(ExecutiveFixture, BootEntersStandbyThroughInitialization) {
    ASSERT_EQ(executive.boot_to_standby().status, flight::TransitionStatus::Accepted);
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::Standby);

    const auto modes =
        of_type<flight::ModeChangedEvent<core::ManualClock::time_point>>(events.snapshot());
    ASSERT_EQ(modes.size(), 2U);
    EXPECT_EQ(modes[0].from, flight::SpacecraftMode::Boot);
    EXPECT_EQ(modes[0].to, flight::SpacecraftMode::Initialization);
    EXPECT_EQ(modes[1].from, flight::SpacecraftMode::Initialization);
    EXPECT_EQ(modes[1].to, flight::SpacecraftMode::Standby);
    EXPECT_NE(output.str().find("Boot -> Initialization"), std::string::npos);
    EXPECT_NE(output.str().find("Initialization -> Standby"), std::string::npos);
}

TEST_F(ExecutiveFixture, SecondBootIsRejected) {
    ASSERT_EQ(executive.boot_to_standby().status, flight::TransitionStatus::Accepted);
    const flight::BootResult again = executive.boot_to_standby();
    EXPECT_EQ(again.status, flight::TransitionStatus::Rejected);
    EXPECT_EQ(again.mode, flight::SpacecraftMode::Standby);
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::Standby);
    const auto rejections =
        of_type<flight::ModeTransitionRejected<core::ManualClock::time_point>>(events.snapshot());
    ASSERT_EQ(rejections.size(), 1U);
    EXPECT_EQ(rejections.front().from, flight::SpacecraftMode::Standby);
    EXPECT_EQ(rejections.front().attempted, flight::SpacecraftMode::Standby);
    EXPECT_EQ(rejections.front().reason, flight::TransitionRejectReason::BootRejected);
    EXPECT_NE(output.str().find("boot rejected while in Standby"), std::string::npos);
    EXPECT_EQ(output.str().find("Standby -> Initialization"), std::string::npos);
}

TEST_F(ExecutiveFixture, FullEventLogKeepsTheNewestModeChange) {
    for (std::size_t index = 0; index < events.capacity; ++index) {
        using Log = core::EventLog<flight::SystemEvent<core::ManualClock::time_point>>;
        ASSERT_EQ(events.publish(flight::SystemEvent<core::ManualClock::time_point>{
                      flight::CommandEvent<core::ManualClock::time_point>{
                          flight::Command::StartMission, false, clock.now().time}}),
                  (Log::Status::Stored));
    }
    ASSERT_EQ(executive.boot_to_standby().status, flight::TransitionStatus::Accepted);
    EXPECT_TRUE(events.overflowed());
    EXPECT_GE(events.overwrite_count(), 2U);
    const auto modes =
        of_type<flight::ModeChangedEvent<core::ManualClock::time_point>>(events.snapshot());
    ASSERT_GE(modes.size(), 2U);
    EXPECT_EQ(modes.back().to, flight::SpacecraftMode::Standby);
    EXPECT_NE(output.str().find("overwrote the oldest record"), std::string::npos);
}

TEST_F(ExecutiveFixture, StartMissionFromStandbyEntersNominal) {
    ASSERT_EQ(executive.boot_to_standby().status, flight::TransitionStatus::Accepted);
    EXPECT_EQ(executive.accept(flight::Command::StartMission), flight::CommandStatus::Accepted);
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::Nominal);

    const auto recorded = events.snapshot();
    const auto commands = of_type<flight::CommandEvent<core::ManualClock::time_point>>(recorded);
    ASSERT_EQ(commands.size(), 1U);
    EXPECT_TRUE(commands.front().accepted);
    EXPECT_EQ(commands.front().command, flight::Command::StartMission);

    const auto modes = of_type<flight::ModeChangedEvent<core::ManualClock::time_point>>(recorded);
    ASSERT_EQ(modes.size(), 3U);
    EXPECT_EQ(modes.back().from, flight::SpacecraftMode::Standby);
    EXPECT_EQ(modes.back().to, flight::SpacecraftMode::Nominal);
    EXPECT_NE(output.str().find("StartMission accepted"), std::string::npos);
}

TEST_F(ExecutiveFixture, StartMissionFromBootIsRejected) {
    EXPECT_EQ(executive.accept(flight::Command::StartMission), flight::CommandStatus::Rejected);
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::Boot);
    const auto commands =
        of_type<flight::CommandEvent<core::ManualClock::time_point>>(events.snapshot());
    ASSERT_EQ(commands.size(), 1U);
    EXPECT_FALSE(commands.front().accepted);
    EXPECT_TRUE(of_type<flight::ModeChangedEvent<core::ManualClock::time_point>>(events.snapshot())
                    .empty());
    EXPECT_NE(output.str().find("StartMission rejected"), std::string::npos);
}

TEST_F(ExecutiveFixture, StartMissionFromNominalIsRejected) {
    ASSERT_EQ(executive.boot_to_standby().status, flight::TransitionStatus::Accepted);
    ASSERT_EQ(executive.accept(flight::Command::StartMission), flight::CommandStatus::Accepted);
    EXPECT_EQ(executive.accept(flight::Command::StartMission), flight::CommandStatus::Rejected);
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::Nominal);
}

TEST_F(ExecutiveFixture, StartMissionFromBootRecordsTheIllegalTransition) {
    EXPECT_EQ(executive.accept(flight::Command::StartMission), flight::CommandStatus::Rejected);
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::Boot);

    const auto rejections =
        of_type<flight::ModeTransitionRejected<core::ManualClock::time_point>>(events.snapshot());
    ASSERT_EQ(rejections.size(), 1U);
    EXPECT_EQ(rejections.front().from, flight::SpacecraftMode::Boot);
    EXPECT_EQ(rejections.front().attempted, flight::SpacecraftMode::Nominal);
    EXPECT_EQ(rejections.front().reason, flight::TransitionRejectReason::Illegal);
    EXPECT_NE(output.str().find("rejected (illegal)"), std::string::npos);
}

TEST_F(ExecutiveFixture, UnknownCommandIsRejectedWithoutAModeTransition) {
    const auto unknown = static_cast<flight::Command>(9);
    EXPECT_EQ(executive.accept(unknown), flight::CommandStatus::Rejected);
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::Boot);
    EXPECT_TRUE(of_type<flight::ModeChangedEvent<core::ManualClock::time_point>>(events.snapshot())
                    .empty());
    EXPECT_TRUE(
        of_type<flight::ModeTransitionRejected<core::ManualClock::time_point>>(events.snapshot())
            .empty());
    const auto commands =
        of_type<flight::CommandEvent<core::ManualClock::time_point>>(events.snapshot());
    ASSERT_EQ(commands.size(), 1U);
    EXPECT_FALSE(commands.front().accepted);
    EXPECT_EQ(commands.front().command, unknown);
}

TEST(ExecutiveLogging, ThrowingStreamDoesNotReplaceTheFlightResult) {
    class ThrowBuf : public std::streambuf {
    protected:
        int_type overflow(int_type) override { throw std::runtime_error("log failed"); }
        std::streamsize xsputn(const char*, std::streamsize) override {
            throw std::runtime_error("log failed");
        }
    } buffer;
    std::ostream out(&buffer);
    core::ManualClock clock;
    core::Logger<core::ManualClock> logger(out, clock);
    core::EventLog<flight::SystemEvent<core::ManualClock::time_point>> events;
    Executive executive(clock, logger, events);

    flight::BootResult boot;
    EXPECT_NO_THROW(boot = executive.boot_to_standby());
    EXPECT_EQ(boot.status, flight::TransitionStatus::Accepted);
    EXPECT_EQ(boot.mode, flight::SpacecraftMode::Standby);
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::Standby);
    EXPECT_EQ(of_type<flight::ModeChangedEvent<core::ManualClock::time_point>>(events.snapshot()).size(),
              2U);

    flight::BootResult again;
    EXPECT_NO_THROW(again = executive.boot_to_standby());
    EXPECT_EQ(again.status, flight::TransitionStatus::Rejected);
    EXPECT_EQ(again.mode, flight::SpacecraftMode::Standby);
    const auto rejections =
        of_type<flight::ModeTransitionRejected<core::ManualClock::time_point>>(events.snapshot());
    ASSERT_EQ(rejections.size(), 1U);
    EXPECT_EQ(rejections.front().reason, flight::TransitionRejectReason::BootRejected);

    const auto unknown = static_cast<flight::Command>(9);
    flight::CommandStatus rejected = flight::CommandStatus::Accepted;
    EXPECT_NO_THROW(rejected = executive.accept(unknown));
    EXPECT_EQ(rejected, flight::CommandStatus::Rejected);
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::Standby);

    flight::CommandStatus accepted = flight::CommandStatus::Rejected;
    EXPECT_NO_THROW(accepted = executive.accept(flight::Command::StartMission));
    EXPECT_EQ(accepted, flight::CommandStatus::Accepted);
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::Nominal);
    EXPECT_EQ(of_type<flight::ModeChangedEvent<core::ManualClock::time_point>>(events.snapshot()).size(),
              3U);
}
