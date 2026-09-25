#include "ares/core/logger.hpp"
#include "ares/flight/fdir.hpp"
#include "ares/flight/fdir_limits.hpp"

#include <sstream>
#include <stop_token>

#include <gtest/gtest.h>

namespace flight = ares::flight;
namespace hardware = ares::hardware;
using Clock = ares::core::ManualClock;
using Time = Clock::time_point;

static_assert(flight::limits::kSafeModeRecoveryCycles == 3U);

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
    }

    [[nodiscard]] Time stamp() {
        const ares::core::ClockSample<Time> now = clock.now();
        EXPECT_EQ(now.status, ares::core::ClockStatus::Ok);
        return now.time;
    }

    void enter_safe_on_battery() {
        if (executive.mode() == flight::SpacecraftMode::Boot) {
            reach_nominal();
        }
        ASSERT_EQ(fdir.observe_battery(flight::SampleUsability::Usable, hardware::Millivolts{10999},
                                       stamp())
                      .power,
                  flight::RegistryStatus::Activated);
        const flight::PolicyDecision decision = fdir.apply(executive);
        ASSERT_EQ(decision.action, flight::RecoveryAction::EnterSafeMode);
        ASSERT_EQ(executive.mode(), flight::SpacecraftMode::SafeMode);
    }

    void clear_battery() {
        ASSERT_EQ(fdir.observe_battery(flight::SampleUsability::Usable, hardware::Millivolts{11501},
                                       stamp())
                      .power,
                  flight::RegistryStatus::Cleared);
    }
};

} // namespace

TEST(SafeModeRecovery, ThirdHealthyCycleRequestsStandby) {
    Harness harness;
    harness.enter_safe_on_battery();
    harness.clear_battery();

    const flight::PolicyDecision first = harness.fdir.apply(harness.executive);
    const flight::PolicyDecision second = harness.fdir.apply(harness.executive);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::SafeMode);
    EXPECT_FALSE(first.requested_mode.has_value());
    EXPECT_FALSE(second.requested_mode.has_value());
    EXPECT_EQ(harness.fdir.safe_recovery_streak(), 2U);

    const flight::PolicyDecision third = harness.fdir.apply(harness.executive);
    EXPECT_EQ(third.action, flight::RecoveryAction::RecoverToStandby);
    ASSERT_TRUE(third.requested_mode.has_value());
    EXPECT_EQ(*third.requested_mode, flight::SpacecraftMode::Standby);
    ASSERT_TRUE(third.transition.has_value());
    EXPECT_EQ(*third.transition, flight::TransitionStatus::Accepted);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Standby);
    EXPECT_NE(*third.requested_mode, flight::SpacecraftMode::Nominal);
}

TEST(SafeModeRecovery, CriticalDuringStabilizationResetsTheCount) {
    Harness harness;
    harness.enter_safe_on_battery();
    harness.clear_battery();
    (void)harness.fdir.apply(harness.executive);
    (void)harness.fdir.apply(harness.executive);
    ASSERT_EQ(harness.fdir.safe_recovery_streak(), 2U);

    ASSERT_EQ(harness.fdir
                  .observe_battery(flight::SampleUsability::Usable, hardware::Millivolts{10999},
                                   harness.stamp())
                  .power,
              flight::RegistryStatus::Activated);
    const flight::PolicyDecision reset = harness.fdir.apply(harness.executive);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::SafeMode);
    EXPECT_EQ(harness.fdir.safe_recovery_streak(), 0U);
    EXPECT_EQ(reset.action, flight::RecoveryAction::EnterSafeMode);
    EXPECT_FALSE(reset.requested_mode.has_value());

    harness.clear_battery();
    (void)harness.fdir.apply(harness.executive);
    EXPECT_EQ(harness.fdir.safe_recovery_streak(), 1U);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::SafeMode);
}

TEST(SafeModeRecovery, PersistentWarningBlocksStandby) {
    Harness harness;
    harness.reach_nominal();
    for (int sample = 0; sample < 3; ++sample) {
        (void)harness.fdir.observe_sensor(flight::FaultSource::Gps, flight::SampleUsability::Stale,
                                          harness.stamp());
    }
    ASSERT_EQ(harness.fdir
                  .observe_battery(flight::SampleUsability::Usable, hardware::Millivolts{10999},
                                   harness.stamp())
                  .power,
              flight::RegistryStatus::Activated);
    ASSERT_EQ(harness.fdir.apply(harness.executive).action, flight::RecoveryAction::EnterSafeMode);
    ASSERT_EQ(harness.executive.mode(), flight::SpacecraftMode::SafeMode);
    harness.clear_battery();

    for (int cycle = 0; cycle < 3; ++cycle) {
        const flight::PolicyDecision decision = harness.fdir.apply(harness.executive);
        EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::SafeMode);
        EXPECT_EQ(decision.action, flight::RecoveryAction::ContinueDegraded);
        EXPECT_FALSE(decision.requested_mode.has_value());
    }
    EXPECT_EQ(harness.fdir.safe_recovery_streak(), 0U);
}

TEST(SafeModeRecovery, SaturationBlocksStandby) {
    ares::core::ManualClock clock;
    std::ostringstream output;
    ares::core::Logger<Clock> logger(output, clock);
    ares::core::EventLog<flight::SystemEvent<Time>> events;
    flight::FlightExecutive<Clock> executive(clock, logger, events);
    flight::FdirController<Clock, 1> fdir(events);

    ASSERT_EQ(executive.boot_to_standby().status, flight::TransitionStatus::Accepted);
    ASSERT_EQ(executive.accept(flight::Command::StartMission), flight::CommandStatus::Accepted);
    const ares::core::ClockSample<Time> now = clock.now();
    ASSERT_EQ(now.status, ares::core::ClockStatus::Ok);
    ASSERT_EQ(fdir.observe_deadline(flight::FaultSource::NavigationTask,
                                    flight::DeadlineFact::Missed, now.time),
              flight::RegistryStatus::Activated);
    EXPECT_EQ(fdir.observe_deadline(flight::FaultSource::HealthTask, flight::DeadlineFact::Missed,
                                    now.time),
              flight::RegistryStatus::RejectedFull);
    EXPECT_TRUE(fdir.registry().saturated());
    ASSERT_EQ(fdir.apply(executive).action, flight::RecoveryAction::EnterSafeMode);
    ASSERT_EQ(executive.mode(), flight::SpacecraftMode::SafeMode);
    ASSERT_EQ(fdir.observe_deadline(flight::FaultSource::NavigationTask,
                                    flight::DeadlineFact::OnTime, now.time),
              flight::RegistryStatus::Cleared);

    for (int cycle = 0; cycle < 3; ++cycle) {
        const flight::PolicyDecision decision = fdir.apply(executive);
        EXPECT_EQ(executive.mode(), flight::SpacecraftMode::SafeMode);
        EXPECT_EQ(decision.action, flight::RecoveryAction::EnterSafeMode);
        EXPECT_FALSE(decision.requested_mode.has_value());
    }
    EXPECT_EQ(fdir.safe_recovery_streak(), 0U);
    EXPECT_TRUE(fdir.registry().saturated());
}

TEST(SafeModeRecovery, RejectedStandbyRequestDoesNotClaimSuccess) {
    Harness harness;
    harness.enter_safe_on_battery();
    harness.clear_battery();
    (void)harness.fdir.apply(harness.executive);
    (void)harness.fdir.apply(harness.executive);
    ASSERT_EQ(harness.fdir.safe_recovery_streak(), 2U);

    flight::PolicyDecision nested;
    harness.executive.set_mode_callback_probe(
        [&] { nested = harness.fdir.apply(harness.executive); });
    EXPECT_EQ(harness.executive.request_mode(flight::SpacecraftMode::Nominal),
              flight::TransitionStatus::Rejected);

    EXPECT_EQ(nested.action, flight::RecoveryAction::RecoverToStandby);
    ASSERT_TRUE(nested.requested_mode.has_value());
    EXPECT_EQ(*nested.requested_mode, flight::SpacecraftMode::Standby);
    ASSERT_TRUE(nested.transition.has_value());
    EXPECT_EQ(*nested.transition, flight::TransitionStatus::Rejected);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::SafeMode);
    EXPECT_EQ(harness.fdir.safe_recovery_streak(), 3U);
}

TEST(SafeModeRecovery, StandbyDoesNotRequestNominalInTheSameOrNextCycle) {
    Harness harness;
    harness.enter_safe_on_battery();
    harness.clear_battery();
    (void)harness.fdir.apply(harness.executive);
    (void)harness.fdir.apply(harness.executive);
    const flight::PolicyDecision recovered = harness.fdir.apply(harness.executive);
    ASSERT_EQ(harness.executive.mode(), flight::SpacecraftMode::Standby);
    ASSERT_TRUE(recovered.requested_mode.has_value());
    EXPECT_EQ(*recovered.requested_mode, flight::SpacecraftMode::Standby);

    const flight::PolicyDecision next = harness.fdir.apply(harness.executive);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Standby);
    EXPECT_EQ(next.action, flight::RecoveryAction::None);
    EXPECT_FALSE(next.requested_mode.has_value());
    EXPECT_FALSE(next.transition.has_value());
}

TEST(SafeModeRecovery, RemainingCriticalKeepsSafeMode) {
    Harness harness;
    harness.enter_safe_on_battery();
    for (int sample = 0; sample < 5; ++sample) {
        ASSERT_EQ(harness.fdir.observe_deadline(flight::FaultSource::NavigationTask,
                                                flight::DeadlineFact::Missed, harness.stamp()),
                  sample == 0 ? flight::RegistryStatus::Activated
                              : flight::RegistryStatus::Updated);
    }
    harness.clear_battery();
    const flight::PolicyDecision decision = harness.fdir.apply(harness.executive);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::SafeMode);
    EXPECT_EQ(decision.action, flight::RecoveryAction::EnterSafeMode);
    EXPECT_FALSE(decision.requested_mode.has_value());
    EXPECT_EQ(harness.fdir.safe_recovery_streak(), 0U);
    EXPECT_EQ(harness.fdir.registry()
                  .find(flight::FaultType::DeadlineMiss, flight::FaultSource::NavigationTask)
                  ->severity,
              flight::FaultSeverity::Critical);
}

TEST(SafeModeRecovery, PersistentWarningAfterCriticalsClearDoesNotStartRecovery) {
    Harness harness;
    harness.reach_nominal();
    for (int sample = 0; sample < 3; ++sample) {
        (void)harness.fdir.observe_sensor(flight::FaultSource::Imu, flight::SampleUsability::Stale,
                                          harness.stamp());
    }
    harness.enter_safe_on_battery();
    harness.clear_battery();
    for (int cycle = 0; cycle < 3; ++cycle) {
        (void)harness.fdir.apply(harness.executive);
    }
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::SafeMode);
    EXPECT_EQ(harness.fdir.safe_recovery_streak(), 0U);

    ASSERT_EQ(harness.fdir.observe_sensor(flight::FaultSource::Imu, flight::SampleUsability::Usable,
                                          harness.stamp()),
              flight::RegistryStatus::Cleared);
    const flight::PolicyDecision started = harness.fdir.apply(harness.executive);
    EXPECT_EQ(harness.fdir.safe_recovery_streak(), 1U);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::SafeMode);
    EXPECT_FALSE(started.requested_mode.has_value());
}

TEST(SafeModeRecovery, ShortWarningDoesNotBlockStandby) {
    Harness harness;
    harness.enter_safe_on_battery();
    harness.clear_battery();
    ASSERT_EQ(harness.fdir.observe_sensor(flight::FaultSource::Gps, flight::SampleUsability::Stale,
                                          harness.stamp()),
              flight::RegistryStatus::Activated);

    (void)harness.fdir.apply(harness.executive);
    (void)harness.fdir.apply(harness.executive);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::SafeMode);
    EXPECT_EQ(harness.fdir.safe_recovery_streak(), 2U);

    const flight::PolicyDecision third = harness.fdir.apply(harness.executive);
    EXPECT_EQ(third.action, flight::RecoveryAction::RecoverToStandby);
    ASSERT_TRUE(third.requested_mode.has_value());
    EXPECT_EQ(*third.requested_mode, flight::SpacecraftMode::Standby);
    ASSERT_TRUE(third.transition.has_value());
    EXPECT_EQ(*third.transition, flight::TransitionStatus::Accepted);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Standby);
}

TEST(SafeModeRecovery, StopDoesNotAdvanceRecoveryOrConsumeDeadlines) {
    Harness harness;
    harness.enter_safe_on_battery();
    harness.clear_battery();
    (void)harness.fdir.apply(harness.executive);
    ASSERT_EQ(harness.fdir.safe_recovery_streak(), 1U);

    flight::FaultMailbox<Clock> mailbox;
    mailbox.publish_deadline(flight::FaultSource::NavigationTask, true);
    std::stop_source stop;
    stop.request_stop();
    const flight::PolicyDecision skipped = flight::run_fdir_cycle(
        harness.fdir, mailbox, harness.executive, harness.clock.now(), stop.get_token());
    EXPECT_EQ(harness.fdir.safe_recovery_streak(), 1U);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::SafeMode);
    EXPECT_FALSE(skipped.requested_mode.has_value());
    EXPECT_EQ(harness.fdir.registry().find(flight::FaultType::DeadlineMiss,
                                           flight::FaultSource::NavigationTask),
              nullptr);
    const auto pending = mailbox.consume();
    EXPECT_TRUE(pending.navigation.pending);
    EXPECT_TRUE(pending.navigation.missed);
}
