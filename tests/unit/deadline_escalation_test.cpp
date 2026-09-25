#include "ares/core/logger.hpp"
#include "ares/flight/fdir.hpp"
#include "ares/flight/fdir_limits.hpp"

#include <sstream>

#include <gtest/gtest.h>

namespace flight = ares::flight;
namespace hardware = ares::hardware;
using namespace std::chrono_literals;
using Clock = ares::core::ManualClock;
using Time = Clock::time_point;

static_assert(flight::limits::kDeadlineWarningAfter == 3U);
static_assert(flight::limits::kDeadlineCriticalAfter == 5U);

namespace {

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

    [[nodiscard]] const flight::FaultRecord<Time>* deadline(flight::FaultSource source) const {
        return fdir.registry().find(flight::FaultType::DeadlineMiss, source);
    }

    [[nodiscard]] std::size_t updates_for(flight::FaultSource source) const {
        std::size_t count = 0;
        for (const flight::SystemEvent<Time>& event : events.snapshot()) {
            const auto* updated = std::get_if<flight::FaultUpdatedEvent<Time>>(&event);
            if (updated != nullptr && updated->type == flight::FaultType::DeadlineMiss &&
                updated->source == source) {
                ++count;
            }
        }
        return count;
    }

    flight::PolicyDecision miss(flight::FaultSource source) {
        const flight::RegistryStatus status =
            fdir.observe_deadline(source, flight::DeadlineFact::Missed, stamp());
        EXPECT_TRUE(status == flight::RegistryStatus::Activated ||
                    status == flight::RegistryStatus::Updated);
        return fdir.apply(executive);
    }
};

} // namespace

TEST(DeadlineEscalation, OneAndTwoMissesStayAdvisoryAndNominal) {
    Harness harness;
    harness.reach_nominal();
    (void)harness.miss(flight::FaultSource::NavigationTask);
    const flight::FaultRecord<Time>* once = harness.deadline(flight::FaultSource::NavigationTask);
    ASSERT_NE(once, nullptr);
    EXPECT_TRUE(once->active);
    EXPECT_EQ(once->consecutive_count, 1U);
    EXPECT_EQ(once->severity, flight::FaultSeverity::Advisory);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Nominal);
    EXPECT_EQ(harness.updates_for(flight::FaultSource::NavigationTask), 0U);

    (void)harness.miss(flight::FaultSource::NavigationTask);
    EXPECT_EQ(once->consecutive_count, 2U);
    EXPECT_EQ(once->severity, flight::FaultSeverity::Advisory);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Nominal);
    EXPECT_EQ(harness.updates_for(flight::FaultSource::NavigationTask), 0U);
}

TEST(DeadlineEscalation, ThirdMissBecomesWarningAndMayDegrade) {
    Harness harness;
    harness.reach_nominal();
    (void)harness.miss(flight::FaultSource::NavigationTask);
    (void)harness.miss(flight::FaultSource::NavigationTask);
    const flight::PolicyDecision decision = harness.miss(flight::FaultSource::NavigationTask);
    const flight::FaultRecord<Time>* record = harness.deadline(flight::FaultSource::NavigationTask);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->consecutive_count, 3U);
    EXPECT_EQ(record->severity, flight::FaultSeverity::Warning);
    EXPECT_EQ(decision.action, flight::RecoveryAction::ContinueDegraded);
    ASSERT_TRUE(decision.requested_mode.has_value());
    EXPECT_EQ(*decision.requested_mode, flight::SpacecraftMode::Degraded);
    ASSERT_TRUE(decision.transition.has_value());
    EXPECT_EQ(*decision.transition, flight::TransitionStatus::Accepted);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Degraded);
    EXPECT_EQ(harness.updates_for(flight::FaultSource::NavigationTask), 1U);
}

TEST(DeadlineEscalation, FourthMissStaysWarningWithoutAnotherUpdate) {
    Harness harness;
    harness.reach_nominal();
    (void)harness.miss(flight::FaultSource::NavigationTask);
    (void)harness.miss(flight::FaultSource::NavigationTask);
    (void)harness.miss(flight::FaultSource::NavigationTask);
    const flight::PolicyDecision decision = harness.miss(flight::FaultSource::NavigationTask);
    const flight::FaultRecord<Time>* record = harness.deadline(flight::FaultSource::NavigationTask);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->consecutive_count, 4U);
    EXPECT_EQ(record->severity, flight::FaultSeverity::Warning);
    EXPECT_EQ(harness.updates_for(flight::FaultSource::NavigationTask), 1U);
    EXPECT_FALSE(decision.requested_mode.has_value());
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Degraded);
}

TEST(DeadlineEscalation, FifthMissBecomesCriticalAndMayEnterSafeMode) {
    Harness harness;
    harness.reach_nominal();
    for (int sample = 0; sample < 4; ++sample) {
        (void)harness.miss(flight::FaultSource::NavigationTask);
    }
    const flight::PolicyDecision decision = harness.miss(flight::FaultSource::NavigationTask);
    const flight::FaultRecord<Time>* record = harness.deadline(flight::FaultSource::NavigationTask);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->consecutive_count, 5U);
    EXPECT_EQ(record->occurrence_count, 5U);
    EXPECT_EQ(record->severity, flight::FaultSeverity::Critical);
    EXPECT_EQ(decision.action, flight::RecoveryAction::EnterSafeMode);
    ASSERT_TRUE(decision.requested_mode.has_value());
    EXPECT_EQ(*decision.requested_mode, flight::SpacecraftMode::SafeMode);
    ASSERT_TRUE(decision.transition.has_value());
    EXPECT_EQ(*decision.transition, flight::TransitionStatus::Accepted);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::SafeMode);
    EXPECT_EQ(harness.updates_for(flight::FaultSource::NavigationTask), 2U);

    (void)harness.miss(flight::FaultSource::NavigationTask);
    EXPECT_EQ(record->consecutive_count, 6U);
    EXPECT_EQ(record->severity, flight::FaultSeverity::Critical);
    EXPECT_EQ(harness.updates_for(flight::FaultSource::NavigationTask), 2U);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::SafeMode);
}

TEST(DeadlineEscalation, OnTimeBreaksTheMissStreak) {
    Harness harness;
    harness.reach_nominal();
    (void)harness.miss(flight::FaultSource::NavigationTask);
    (void)harness.miss(flight::FaultSource::NavigationTask);
    EXPECT_EQ(harness.fdir.observe_deadline(flight::FaultSource::NavigationTask,
                                            flight::DeadlineFact::OnTime, harness.stamp()),
              flight::RegistryStatus::Cleared);
    const flight::FaultRecord<Time>* cleared =
        harness.deadline(flight::FaultSource::NavigationTask);
    ASSERT_NE(cleared, nullptr);
    EXPECT_FALSE(cleared->active);
    EXPECT_EQ(cleared->consecutive_count, 0U);
    EXPECT_EQ(cleared->occurrence_count, 2U);

    (void)harness.miss(flight::FaultSource::NavigationTask);
    EXPECT_TRUE(cleared->active);
    EXPECT_EQ(cleared->consecutive_count, 1U);
    EXPECT_EQ(cleared->occurrence_count, 3U);
    EXPECT_EQ(cleared->severity, flight::FaultSeverity::Advisory);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Nominal);
}

TEST(DeadlineEscalation, TaskStreaksStayIndependent) {
    Harness harness;
    harness.reach_nominal();
    (void)harness.miss(flight::FaultSource::NavigationTask);
    (void)harness.miss(flight::FaultSource::NavigationTask);
    (void)harness.miss(flight::FaultSource::NavigationTask);
    (void)harness.miss(flight::FaultSource::HealthTask);

    const flight::FaultRecord<Time>* navigation =
        harness.deadline(flight::FaultSource::NavigationTask);
    const flight::FaultRecord<Time>* health = harness.deadline(flight::FaultSource::HealthTask);
    ASSERT_NE(navigation, nullptr);
    ASSERT_NE(health, nullptr);
    EXPECT_EQ(navigation->consecutive_count, 3U);
    EXPECT_EQ(navigation->severity, flight::FaultSeverity::Warning);
    EXPECT_EQ(health->consecutive_count, 1U);
    EXPECT_EQ(health->severity, flight::FaultSeverity::Advisory);
    EXPECT_EQ(harness.deadline(flight::FaultSource::CommunicationsTask), nullptr);

    EXPECT_EQ(harness.fdir.observe_deadline(flight::FaultSource::HealthTask,
                                            flight::DeadlineFact::OnTime, harness.stamp()),
              flight::RegistryStatus::Cleared);
    EXPECT_EQ(navigation->consecutive_count, 3U);
    EXPECT_EQ(navigation->severity, flight::FaultSeverity::Warning);
    EXPECT_FALSE(health->active);
}

TEST(DeadlineEscalation, DeadlineWarningPlusSensorWarningStaysDegraded) {
    Harness harness;
    harness.reach_nominal();
    for (int sample = 0; sample < 3; ++sample) {
        ASSERT_EQ(harness.clock.advance(1s), ares::core::AdvanceStatus::Applied);
        (void)harness.fdir.observe_sensor(flight::FaultSource::PrimaryGps,
                                          flight::SampleUsability::Stale, harness.stamp());
        (void)harness.miss(flight::FaultSource::NavigationTask);
    }
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Degraded);
    EXPECT_EQ(harness.deadline(flight::FaultSource::NavigationTask)->severity,
              flight::FaultSeverity::Warning);
    EXPECT_EQ(harness.fdir.registry()
                  .find(flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps)
                  ->consecutive_count,
              3U);
}

TEST(DeadlineEscalation, DeadlineCriticalDominatesSensorWarning) {
    Harness harness;
    harness.reach_nominal();
    for (int sample = 0; sample < 3; ++sample) {
        (void)harness.fdir.observe_sensor(flight::FaultSource::PrimaryGps,
                                          flight::SampleUsability::Stale, harness.stamp());
    }
    for (int sample = 0; sample < 5; ++sample) {
        (void)harness.miss(flight::FaultSource::NavigationTask);
    }
    EXPECT_EQ(harness.deadline(flight::FaultSource::NavigationTask)->severity,
              flight::FaultSeverity::Critical);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::SafeMode);
}

TEST(DeadlineEscalation, BatteryCriticalDominatesDeadlineAdvisory) {
    Harness harness;
    harness.reach_nominal();
    (void)harness.miss(flight::FaultSource::CommunicationsTask);
    const flight::PolicyDecision decision = harness.fdir.apply(harness.executive);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Nominal);
    EXPECT_FALSE(decision.requested_mode.has_value());

    ASSERT_EQ(harness.fdir
                  .observe_battery(flight::SampleUsability::Usable, hardware::Millivolts{10999},
                                   harness.stamp())
                  .power,
              flight::RegistryStatus::Activated);
    const flight::PolicyDecision safe = harness.fdir.apply(harness.executive);
    EXPECT_EQ(safe.action, flight::RecoveryAction::EnterSafeMode);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::SafeMode);
    EXPECT_EQ(harness.deadline(flight::FaultSource::CommunicationsTask)->severity,
              flight::FaultSeverity::Advisory);
}
