#include "ares/core/logger.hpp"
#include "ares/flight/command.hpp"
#include "ares/flight/fdir.hpp"

#include <ostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

namespace flight = ares::flight;
namespace hardware = ares::hardware;
using namespace std::chrono_literals;
using Clock = ares::core::ManualClock;
using Time = Clock::time_point;

namespace {

class ThrowingBuffer : public std::streambuf {
protected:
    int_type overflow(int_type) override { throw std::runtime_error("log failed"); }
    std::streamsize xsputn(const char*, std::streamsize) override {
        throw std::runtime_error("log failed");
    }
};

struct Harness {
    Clock clock{};
    std::ostringstream output{};
    ares::core::Logger<Clock> logger;
    ares::core::EventLog<flight::SystemEvent<Time>> events{};
    flight::FlightExecutive<Clock> executive;
    flight::FdirController<Clock> fdir;

    Harness() : logger(output, clock), executive(clock, logger, events), fdir(events) {}

    void reach_nominal() {
        ASSERT_EQ(executive.boot_to_standby().status, flight::TransitionStatus::Accepted);
        ASSERT_EQ(executive.accept(flight::Command::StartMission), flight::CommandStatus::Accepted);
        ASSERT_EQ(executive.mode(), flight::SpacecraftMode::Nominal);
    }

    [[nodiscard]] Time stamp() {
        const ares::core::ClockSample<Time> now = clock.now();
        EXPECT_EQ(now.status, ares::core::ClockStatus::Ok);
        return now.time;
    }

    [[nodiscard]] std::vector<flight::ModeChangedEvent<Time>> mode_changes() const {
        std::vector<flight::ModeChangedEvent<Time>> changes;
        for (const flight::SystemEvent<Time>& event : events.snapshot()) {
            if (const auto* change = std::get_if<flight::ModeChangedEvent<Time>>(&event)) {
                changes.push_back(*change);
            }
        }
        return changes;
    }
};

} // namespace

TEST(FdirScenario, PersistentGpsStaleMovesNominalToDegraded) {
    Harness harness;
    harness.reach_nominal();
    const std::size_t modes_at_nominal = harness.mode_changes().size();

    const Time first = harness.stamp();
    EXPECT_EQ(harness.fdir.observe_sensor(flight::FaultSource::PrimaryGps,
                                          flight::SampleUsability::Stale, first),
              flight::RegistryStatus::Activated);
    const flight::PolicyDecision first_decision = harness.fdir.apply(harness.executive);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Nominal);
    EXPECT_FALSE(first_decision.requested_mode.has_value());
    EXPECT_FALSE(first_decision.transition.has_value());
    EXPECT_EQ(harness.mode_changes().size(), modes_at_nominal);

    ASSERT_EQ(harness.clock.advance(1s), ares::core::AdvanceStatus::Applied);
    const Time second = harness.stamp();
    EXPECT_EQ(harness.fdir.observe_sensor(flight::FaultSource::PrimaryGps,
                                          flight::SampleUsability::Stale, second),
              flight::RegistryStatus::Updated);
    (void)harness.fdir.apply(harness.executive);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Nominal);

    ASSERT_EQ(harness.clock.advance(1s), ares::core::AdvanceStatus::Applied);
    const Time third = harness.stamp();
    EXPECT_EQ(harness.fdir.observe_sensor(flight::FaultSource::PrimaryGps,
                                          flight::SampleUsability::Stale, third),
              flight::RegistryStatus::Updated);
    const flight::PolicyDecision escalated = harness.fdir.apply(harness.executive);
    EXPECT_EQ(escalated.action, flight::RecoveryAction::ContinueDegraded);
    ASSERT_TRUE(escalated.requested_mode.has_value());
    EXPECT_EQ(*escalated.requested_mode, flight::SpacecraftMode::Degraded);
    ASSERT_TRUE(escalated.transition.has_value());
    EXPECT_EQ(*escalated.transition, flight::TransitionStatus::Accepted);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Degraded);

    const flight::FaultRecord<Time>* record = harness.fdir.registry().find(
        flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps);
    ASSERT_NE(record, nullptr);
    EXPECT_TRUE(record->active);
    EXPECT_EQ(record->first_detected, first);
    EXPECT_EQ(record->last_detected, third);
    EXPECT_EQ(record->occurrence_count, 3U);
    EXPECT_EQ(record->consecutive_count, 3U);

    const std::vector<flight::ModeChangedEvent<Time>> changes = harness.mode_changes();
    ASSERT_FALSE(changes.empty());
    EXPECT_EQ(changes.back().from, flight::SpacecraftMode::Nominal);
    EXPECT_EQ(changes.back().to, flight::SpacecraftMode::Degraded);
}

TEST(FdirScenario, RecoveredGpsReturnsDegradedToNominal) {
    Harness harness;
    harness.reach_nominal();
    for (int sample = 0; sample < 3; ++sample) {
        (void)harness.fdir.observe_sensor(flight::FaultSource::PrimaryGps,
                                          flight::SampleUsability::Stale, harness.stamp());
        (void)harness.fdir.apply(harness.executive);
        ASSERT_EQ(harness.clock.advance(1s), ares::core::AdvanceStatus::Applied);
    }
    ASSERT_EQ(harness.executive.mode(), flight::SpacecraftMode::Degraded);
    const flight::FaultRecord<Time>* before = harness.fdir.registry().find(
        flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps);
    ASSERT_NE(before, nullptr);
    const Time first = before->first_detected;
    const std::uint32_t occurrences = before->occurrence_count;

    const Time recovered_at = harness.stamp();
    EXPECT_EQ(harness.fdir.observe_sensor(flight::FaultSource::PrimaryGps,
                                          flight::SampleUsability::Usable, recovered_at),
              flight::RegistryStatus::Cleared);
    const flight::PolicyDecision decision = harness.fdir.apply(harness.executive);
    EXPECT_EQ(decision.action, flight::RecoveryAction::None);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Nominal);

    const flight::FaultRecord<Time>* record = harness.fdir.registry().find(
        flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps);
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(record->active);
    EXPECT_EQ(record->first_detected, first);
    EXPECT_EQ(record->occurrence_count, occurrences);
    EXPECT_EQ(record->consecutive_count, 0U);

    const std::vector<flight::ModeChangedEvent<Time>> changes = harness.mode_changes();
    ASSERT_GE(changes.size(), 2U);
    EXPECT_EQ(changes.back().from, flight::SpacecraftMode::Degraded);
    EXPECT_EQ(changes.back().to, flight::SpacecraftMode::Nominal);
}

TEST(FdirScenario, CriticalBatteryEntersSafeMode) {
    Harness harness;
    harness.reach_nominal();
    const Time time = harness.stamp();
    const auto observation = harness.fdir.observe_battery(flight::SampleUsability::Usable,
                                                          hardware::Millivolts{10999}, time);
    EXPECT_EQ(observation.power, flight::RegistryStatus::Activated);
    const flight::PolicyDecision decision = harness.fdir.apply(harness.executive);
    EXPECT_EQ(decision.action, flight::RecoveryAction::EnterSafeMode);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::SafeMode);

    const flight::FaultRecord<Time>* record =
        harness.fdir.registry().find(flight::FaultType::LowBattery, flight::FaultSource::Battery);
    ASSERT_NE(record, nullptr);
    EXPECT_TRUE(record->active);
    EXPECT_EQ(record->severity, flight::FaultSeverity::Critical);

    const std::vector<flight::ModeChangedEvent<Time>> changes = harness.mode_changes();
    ASSERT_FALSE(changes.empty());
    EXPECT_EQ(changes.back().from, flight::SpacecraftMode::Nominal);
    EXPECT_EQ(changes.back().to, flight::SpacecraftMode::SafeMode);
    for (const flight::ModeChangedEvent<Time>& change : changes) {
        EXPECT_NE(change.to, flight::SpacecraftMode::Emergency);
    }
}

TEST(FdirScenario, OneDeadlineMissDoesNotChangeMode) {
    Harness harness;
    harness.reach_nominal();
    const std::size_t before = harness.mode_changes().size();
    EXPECT_EQ(harness.fdir.observe_deadline(flight::FaultSource::CommunicationsTask,
                                            flight::DeadlineFact::Missed, harness.stamp()),
              flight::RegistryStatus::Activated);
    const flight::PolicyDecision decision = harness.fdir.apply(harness.executive);
    EXPECT_EQ(decision.action, flight::RecoveryAction::None);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Nominal);
    EXPECT_EQ(harness.mode_changes().size(), before);
    const flight::FaultRecord<Time>* record = harness.fdir.registry().find(
        flight::FaultType::DeadlineMiss, flight::FaultSource::CommunicationsTask);
    ASSERT_NE(record, nullptr);
    EXPECT_TRUE(record->active);
    EXPECT_EQ(record->severity, flight::FaultSeverity::Advisory);
}

TEST(FdirScenario, LoggingFailureDoesNotBlockTheModeTransition) {
    ThrowingBuffer buffer;
    std::ostream broken(&buffer);
    Clock clock;
    ares::core::Logger<Clock> logger(broken, clock);
    ares::core::EventLog<flight::SystemEvent<Time>> events;
    flight::FlightExecutive<Clock> executive(clock, logger, events);
    flight::FdirController<Clock> fdir(events);

    ASSERT_EQ(executive.boot_to_standby().status, flight::TransitionStatus::Accepted);
    ASSERT_EQ(executive.accept(flight::Command::StartMission), flight::CommandStatus::Accepted);
    ASSERT_EQ(executive.mode(), flight::SpacecraftMode::Nominal);

    const auto observation =
        fdir.observe_battery(flight::SampleUsability::Usable, hardware::Millivolts{10000}, Time{});
    EXPECT_EQ(observation.power, flight::RegistryStatus::Activated);
    const flight::PolicyDecision decision = fdir.apply(executive);
    EXPECT_EQ(decision.action, flight::RecoveryAction::EnterSafeMode);
    ASSERT_TRUE(decision.requested_mode.has_value());
    EXPECT_EQ(*decision.requested_mode, flight::SpacecraftMode::SafeMode);
    ASSERT_TRUE(decision.transition.has_value());
    EXPECT_EQ(*decision.transition, flight::TransitionStatus::Accepted);
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::SafeMode);
}

TEST(FdirScenario, ReentrantPolicyRequestIsRejected) {
    Harness harness;
    ASSERT_EQ(harness.executive.boot_to_standby().status, flight::TransitionStatus::Accepted);
    for (int sample = 0; sample < 3; ++sample) {
        if (sample > 0) {
            ASSERT_EQ(harness.clock.advance(1s), ares::core::AdvanceStatus::Applied);
        }
        const flight::RegistryStatus status = harness.fdir.observe_sensor(
            flight::FaultSource::PrimaryGps, flight::SampleUsability::Stale, harness.stamp());
        EXPECT_EQ(status, sample == 0 ? flight::RegistryStatus::Activated
                                      : flight::RegistryStatus::Updated);
    }
    const flight::FaultRecord<Time>* before = harness.fdir.registry().find(
        flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps);
    ASSERT_NE(before, nullptr);
    ASSERT_EQ(before->consecutive_count, 3U);
    const std::uint32_t occurrence = before->occurrence_count;
    const Time last_detected = before->last_detected;

    flight::PolicyDecision nested;
    harness.executive.set_mode_callback_probe(
        [&] { nested = harness.fdir.apply(harness.executive); });
    ASSERT_EQ(harness.executive.accept(flight::Command::StartMission),
              flight::CommandStatus::Accepted);

    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Nominal);
    EXPECT_EQ(nested.action, flight::RecoveryAction::ContinueDegraded);
    ASSERT_TRUE(nested.requested_mode.has_value());
    EXPECT_EQ(*nested.requested_mode, flight::SpacecraftMode::Degraded);
    ASSERT_TRUE(nested.transition.has_value());
    EXPECT_EQ(*nested.transition, flight::TransitionStatus::Rejected);

    const flight::FaultRecord<Time>* after = harness.fdir.registry().find(
        flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps);
    ASSERT_NE(after, nullptr);
    EXPECT_TRUE(after->active);
    EXPECT_EQ(after->consecutive_count, 3U);
    EXPECT_EQ(after->occurrence_count, occurrence);
    EXPECT_EQ(after->last_detected, last_detected);

    bool saw_rejection = false;
    for (const flight::SystemEvent<Time>& event : harness.events.snapshot()) {
        const auto* rejected = std::get_if<flight::ModeTransitionRejected<Time>>(&event);
        if (rejected == nullptr) {
            continue;
        }
        EXPECT_EQ(rejected->from, flight::SpacecraftMode::Nominal);
        EXPECT_EQ(rejected->attempted, flight::SpacecraftMode::Degraded);
        EXPECT_EQ(rejected->reason, flight::TransitionRejectReason::Reentrant);
        saw_rejection = true;
    }
    EXPECT_TRUE(saw_rejection);
    for (const flight::ModeChangedEvent<Time>& change : harness.mode_changes()) {
        EXPECT_NE(change.to, flight::SpacecraftMode::Degraded);
    }
}
