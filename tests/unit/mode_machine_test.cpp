#include "ares/flight/mode_machine.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace core = ares::core;
namespace flight = ares::flight;
using namespace std::chrono_literals;
using Machine = flight::ModeMachine<core::ManualClock>;
using Time = core::ManualClock::time_point;

namespace {

constexpr std::array<flight::SpacecraftMode, 7> kModes{
    flight::SpacecraftMode::Boot,      flight::SpacecraftMode::Initialization,
    flight::SpacecraftMode::Standby,   flight::SpacecraftMode::Nominal,
    flight::SpacecraftMode::Degraded,  flight::SpacecraftMode::SafeMode,
    flight::SpacecraftMode::Emergency,
};

[[nodiscard]] bool oracle(flight::SpacecraftMode from, flight::SpacecraftMode to) {
    switch (from) {
    case flight::SpacecraftMode::Boot:
        return to == flight::SpacecraftMode::Initialization;
    case flight::SpacecraftMode::Initialization:
        return to == flight::SpacecraftMode::Standby;
    case flight::SpacecraftMode::Standby:
        return to == flight::SpacecraftMode::Nominal;
    case flight::SpacecraftMode::Nominal:
        return to == flight::SpacecraftMode::Standby || to == flight::SpacecraftMode::Degraded ||
               to == flight::SpacecraftMode::SafeMode || to == flight::SpacecraftMode::Emergency;
    case flight::SpacecraftMode::Degraded:
        return to == flight::SpacecraftMode::Nominal || to == flight::SpacecraftMode::SafeMode ||
               to == flight::SpacecraftMode::Emergency;
    case flight::SpacecraftMode::SafeMode:
        return to == flight::SpacecraftMode::Standby || to == flight::SpacecraftMode::Emergency;
    case flight::SpacecraftMode::Emergency:
        return to == flight::SpacecraftMode::SafeMode;
    }
    return false;
}

[[nodiscard]] std::vector<flight::SpacecraftMode> path_to(flight::SpacecraftMode target) {
    using flight::SpacecraftMode;
    switch (target) {
    case SpacecraftMode::Boot:
        return {};
    case SpacecraftMode::Initialization:
        return {SpacecraftMode::Initialization};
    case SpacecraftMode::Standby:
        return {SpacecraftMode::Initialization, SpacecraftMode::Standby};
    case SpacecraftMode::Nominal:
        return {SpacecraftMode::Initialization, SpacecraftMode::Standby, SpacecraftMode::Nominal};
    case SpacecraftMode::Degraded:
        return {SpacecraftMode::Initialization, SpacecraftMode::Standby, SpacecraftMode::Nominal,
                SpacecraftMode::Degraded};
    case SpacecraftMode::SafeMode:
        return {SpacecraftMode::Initialization, SpacecraftMode::Standby, SpacecraftMode::Nominal,
                SpacecraftMode::SafeMode};
    case SpacecraftMode::Emergency:
        return {SpacecraftMode::Initialization, SpacecraftMode::Standby, SpacecraftMode::Nominal,
                SpacecraftMode::Emergency};
    }
    return {};
}

void reach(Machine& machine, flight::SpacecraftMode target) {
    for (const flight::SpacecraftMode step : path_to(target)) {
        ASSERT_EQ(machine.transition(step), flight::TransitionStatus::Accepted);
    }
    ASSERT_EQ(machine.mode(), target);
}

} // namespace

TEST(ModeMachine, ExhaustiveTransitionsMatchTheOracle) {
    for (const flight::SpacecraftMode from : kModes) {
        for (const flight::SpacecraftMode to : kModes) {
            SCOPED_TRACE(std::string(flight::to_string(from)) + " -> " +
                         std::string(flight::to_string(to)));
            core::ManualClock clock;
            std::vector<flight::ModeChangedEvent<Time>> changes;
            Machine machine(clock);
            machine.set_on_change([&changes](const flight::ModeChangedEvent<Time>& event) {
                changes.push_back(event);
            });
            reach(machine, from);
            changes.clear();

            const bool legal = oracle(from, to);
            EXPECT_EQ(machine.can_transition(to), legal);
            const auto status = machine.transition(to);
            if (legal) {
                EXPECT_EQ(status, flight::TransitionStatus::Accepted);
                EXPECT_EQ(machine.mode(), to);
                ASSERT_EQ(changes.size(), 1U);
                EXPECT_EQ(changes.front().from, from);
                EXPECT_EQ(changes.front().to, to);
            } else {
                EXPECT_EQ(status, flight::TransitionStatus::Rejected);
                EXPECT_EQ(machine.mode(), from);
                EXPECT_TRUE(changes.empty());
            }
        }
    }
}

TEST(ModeMachine, RecordsTheClockTimeOfALegalTransition) {
    core::ManualClock clock;
    ASSERT_EQ(clock.advance(42ms), core::AdvanceStatus::Applied);
    std::vector<flight::ModeChangedEvent<Time>> changes;
    Machine machine(clock);
    machine.set_on_change(
        [&changes](const flight::ModeChangedEvent<Time>& event) { changes.push_back(event); });

    ASSERT_EQ(machine.transition(flight::SpacecraftMode::Initialization),
              flight::TransitionStatus::Accepted);
    ASSERT_EQ(changes.size(), 1U);
    EXPECT_EQ(changes.front().time, Time{42ms});
}

TEST(ModeMachine, StartsInBootWithoutAnEvent) {
    core::ManualClock clock;
    Machine machine(clock);
    EXPECT_EQ(machine.mode(), flight::SpacecraftMode::Boot);
    EXPECT_FALSE(machine.can_transition(flight::SpacecraftMode::Nominal));
}

TEST(ModeMachine, IllegalTransitionEmitsRejectionAndKeepsTheMode) {
    core::ManualClock clock;
    ASSERT_EQ(clock.advance(7ms), core::AdvanceStatus::Applied);
    std::vector<flight::ModeChangedEvent<Time>> changes;
    std::vector<flight::ModeTransitionRejected<Time>> rejections;
    Machine machine(clock);
    machine.set_on_change(
        [&changes](const flight::ModeChangedEvent<Time>& event) { changes.push_back(event); });
    machine.set_on_reject([&rejections](const flight::ModeTransitionRejected<Time>& event) {
        rejections.push_back(event);
    });

    EXPECT_EQ(machine.transition(flight::SpacecraftMode::Nominal),
              flight::TransitionStatus::Rejected);
    EXPECT_EQ(machine.transition(flight::SpacecraftMode::Nominal),
              flight::TransitionStatus::Rejected);
    EXPECT_EQ(machine.mode(), flight::SpacecraftMode::Boot);
    EXPECT_TRUE(changes.empty());
    ASSERT_EQ(rejections.size(), 2U);
    EXPECT_EQ(rejections[0], (flight::ModeTransitionRejected<Time>{
                                 flight::SpacecraftMode::Boot, flight::SpacecraftMode::Nominal,
                                 flight::TransitionRejectReason::Illegal, Time{7ms}}));
    EXPECT_EQ(rejections[1].reason, flight::TransitionRejectReason::Illegal);
}

TEST(ModeMachine, UnknownModeIsRejectedWithoutAChange) {
    core::ManualClock clock;
    const auto unknown = static_cast<flight::SpacecraftMode>(255);
    EXPECT_FALSE(flight::is_known_mode(unknown));
    std::vector<flight::ModeTransitionRejected<Time>> rejections;
    Machine machine(clock);
    machine.set_on_reject([&rejections](const flight::ModeTransitionRejected<Time>& event) {
        rejections.push_back(event);
    });

    EXPECT_FALSE(machine.can_transition(unknown));
    EXPECT_EQ(machine.transition(unknown), flight::TransitionStatus::Rejected);
    EXPECT_EQ(machine.mode(), flight::SpacecraftMode::Boot);
    ASSERT_EQ(rejections.size(), 1U);
    EXPECT_EQ(rejections.front().reason, flight::TransitionRejectReason::InvalidMode);
    EXPECT_EQ(rejections.front().attempted, unknown);
}

TEST(ModeMachine, ReentrantLegalTransitionDoesNotReplaceTheCommittedMode) {
    core::ManualClock clock;
    Machine machine(clock);
    flight::TransitionStatus nested = flight::TransitionStatus::Accepted;
    bool query_during_callback = false;
    int changes = 0;
    std::vector<flight::ModeTransitionRejected<Time>> rejections;
    machine.set_on_change([&](const flight::ModeChangedEvent<Time>& event) {
        ++changes;
        if (event.to != flight::SpacecraftMode::Initialization) {
            return;
        }
        query_during_callback = machine.can_transition(flight::SpacecraftMode::Standby);
        nested = machine.transition(flight::SpacecraftMode::Standby);
    });
    machine.set_on_reject([&](const flight::ModeTransitionRejected<Time>& event) {
        rejections.push_back(event);
        EXPECT_EQ(machine.transition(flight::SpacecraftMode::Nominal),
                  flight::TransitionStatus::Rejected);
    });

    ASSERT_EQ(machine.transition(flight::SpacecraftMode::Initialization),
              flight::TransitionStatus::Accepted);
    EXPECT_FALSE(query_during_callback);
    EXPECT_EQ(nested, flight::TransitionStatus::Rejected);
    EXPECT_EQ(changes, 1);
    EXPECT_EQ(machine.mode(), flight::SpacecraftMode::Initialization);
    ASSERT_EQ(rejections.size(), 1U);
    EXPECT_EQ(rejections.front().reason, flight::TransitionRejectReason::Reentrant);
    EXPECT_EQ(rejections.front().from, flight::SpacecraftMode::Initialization);
    EXPECT_EQ(rejections.front().attempted, flight::SpacecraftMode::Standby);

    EXPECT_EQ(machine.transition(flight::SpacecraftMode::Standby),
              flight::TransitionStatus::Accepted);
    EXPECT_EQ(machine.mode(), flight::SpacecraftMode::Standby);
}

TEST(ModeMachine, HandlerExceptionDoesNotStickTheReentrancyGuard) {
    core::ManualClock clock;
    Machine machine(clock);
    machine.set_on_change([](const flight::ModeChangedEvent<Time>& event) {
        if (event.to == flight::SpacecraftMode::Initialization) {
            throw std::runtime_error("handler failed");
        }
    });

    EXPECT_THROW(static_cast<void>(machine.transition(flight::SpacecraftMode::Initialization)),
                 std::runtime_error);
    EXPECT_EQ(machine.mode(), flight::SpacecraftMode::Initialization);
    EXPECT_EQ(machine.transition(flight::SpacecraftMode::Standby),
              flight::TransitionStatus::Accepted);
    EXPECT_EQ(machine.mode(), flight::SpacecraftMode::Standby);
}
