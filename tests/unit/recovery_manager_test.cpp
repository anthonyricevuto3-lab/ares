#include "ares/core/logger.hpp"
#include "ares/flight/fdir.hpp"
#include "ares/flight/recovery.hpp"

#include <limits>
#include <sstream>

#include <gtest/gtest.h>

namespace flight = ares::flight;
using Clock = ares::core::ManualClock;
using Time = Clock::time_point;

namespace {

flight::NavigationRecoveryFact critical_navigation(std::uint32_t generation,
                                                   std::uint32_t consecutive) {
    flight::NavigationRecoveryFact fact;
    fact.active = true;
    fact.critical = true;
    fact.consecutive = consecutive;
    fact.generation = generation;
    return fact;
}

std::size_t recovery_events(const ares::core::EventLog<flight::SystemEvent<Time>>& events) {
    std::size_t count = 0;
    for (const flight::SystemEvent<Time>& event : events.snapshot()) {
        if (std::holds_alternative<flight::RecoveryEvent<Time>>(event)) {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST(RecoveryManager, RestartStaysExecutingUntilTheGenerationAdvances) {
    flight::RecoveryManager<Time> recovery;
    ares::core::EventLog<flight::SystemEvent<Time>> events;
    const flight::GpsRecoveryFact gps{};
    const flight::RecoveryCommand first =
        recovery.observe(critical_navigation(0, 5), gps, Time{}, events);
    EXPECT_TRUE(first.restart_navigation);
    EXPECT_EQ(recovery.navigation_state(), flight::RecoveryState::Executing);
    EXPECT_EQ(recovery_events(events), 0U);

    const flight::RecoveryCommand repeat =
        recovery.observe(critical_navigation(0, 6), gps, Time{}, events);
    EXPECT_TRUE(repeat.restart_navigation);
    EXPECT_EQ(recovery.navigation_attempts(), 0);
    EXPECT_EQ(recovery_events(events), 0U);

    flight::NavigationRecoveryFact acknowledged = critical_navigation(1, 6);
    const flight::RecoveryCommand started = recovery.observe(acknowledged, gps, Time{}, events);
    EXPECT_FALSE(started.restart_navigation);
    EXPECT_EQ(recovery.navigation_state(), flight::RecoveryState::Verifying);
    EXPECT_EQ(recovery.navigation_attempts(), 1);
    EXPECT_EQ(recovery_events(events), 1U);
}

TEST(RecoveryManager, ThreeOnTimeCompletionsFromTheNewGenerationSucceed) {
    flight::RecoveryManager<Time> recovery;
    ares::core::EventLog<flight::SystemEvent<Time>> events;
    const flight::GpsRecoveryFact gps{};
    (void)recovery.observe(critical_navigation(0, 5), gps, Time{}, events);
    flight::NavigationRecoveryFact fact = critical_navigation(1, 5);
    (void)recovery.observe(fact, gps, Time{}, events);

    fact.observation_pending = true;
    fact.observation_generation = 0;
    fact.on_time_run = 3;
    (void)recovery.observe(fact, gps, Time{}, events);
    EXPECT_EQ(recovery.navigation_state(), flight::RecoveryState::Verifying);

    fact.observation_generation = 1;
    fact.on_time_run = 2;
    (void)recovery.observe(fact, gps, Time{}, events);
    EXPECT_EQ(recovery.navigation_state(), flight::RecoveryState::Verifying);

    fact.on_time_run = 3;
    (void)recovery.observe(fact, gps, Time{}, events);
    EXPECT_EQ(recovery.navigation_state(), flight::RecoveryState::Succeeded);
    EXPECT_EQ(recovery_events(events), 2U);
}

TEST(RecoveryManager, TwoRestartAttemptsThenOneFailure) {
    flight::RecoveryManager<Time> recovery;
    ares::core::EventLog<flight::SystemEvent<Time>> events;
    const flight::GpsRecoveryFact gps{};
    (void)recovery.observe(critical_navigation(0, 5), gps, Time{}, events);

    flight::NavigationRecoveryFact fact = critical_navigation(1, 5);
    (void)recovery.observe(fact, gps, Time{}, events);
    fact.consecutive = 10;
    const flight::RecoveryCommand second = recovery.observe(fact, gps, Time{}, events);
    EXPECT_TRUE(second.restart_navigation);
    EXPECT_EQ(recovery.navigation_attempts(), 1);

    fact.generation = 2;
    fact.consecutive = 10;
    (void)recovery.observe(fact, gps, Time{}, events);
    EXPECT_EQ(recovery.navigation_attempts(), 2);
    fact.consecutive = 15;
    const flight::RecoveryCommand failed = recovery.observe(fact, gps, Time{}, events);
    EXPECT_FALSE(failed.restart_navigation);
    EXPECT_EQ(recovery.navigation_state(), flight::RecoveryState::Failed);
    EXPECT_EQ(recovery_events(events), 3U);

    (void)recovery.observe(fact, gps, Time{}, events);
    EXPECT_EQ(recovery.navigation_attempts(), 2);
    EXPECT_EQ(recovery_events(events), 3U);
}

TEST(RecoveryManager, GenerationOverflowFailsWithoutWrapping) {
    flight::RecoveryManager<Time> recovery;
    ares::core::EventLog<flight::SystemEvent<Time>> events;
    const auto overflow = critical_navigation(std::numeric_limits<std::uint32_t>::max(), 5);
    const flight::RecoveryCommand command = recovery.observe(overflow, {}, Time{}, events);
    EXPECT_FALSE(command.restart_navigation);
    EXPECT_EQ(recovery.navigation_state(), flight::RecoveryState::Failed);
    EXPECT_EQ(recovery_events(events), 1U);
}

TEST(RecoveryManager, FailedEpisodeBlocksUntilTheDeadlineClears) {
    flight::RecoveryManager<Time> recovery;
    ares::core::EventLog<flight::SystemEvent<Time>> events;
    (void)recovery.observe(critical_navigation(std::numeric_limits<std::uint32_t>::max(), 5), {},
                           Time{}, events);
    EXPECT_TRUE(recovery.blocks_operational_progress());
    flight::NavigationRecoveryFact quiet;
    (void)recovery.observe(quiet, {}, Time{}, events);
    EXPECT_EQ(recovery.navigation_state(), flight::RecoveryState::Idle);
    EXPECT_FALSE(recovery.blocks_operational_progress());
}

TEST(RecoveryManager, GpsFailoverVerifiesBackupAndDoesNotFailBack) {
    flight::RecoveryManager<Time> recovery;
    ares::core::EventLog<flight::SystemEvent<Time>> events;
    flight::GpsRecoveryFact gps;
    gps.primary_selected = true;
    gps.primary_persistent = false;
    gps.backup_unusable_run = 3;
    EXPECT_FALSE(recovery.observe({}, gps, Time{}, events).failover_to_backup);

    gps.primary_persistent = true;
    const flight::RecoveryCommand started = recovery.observe({}, gps, Time{}, events);
    EXPECT_TRUE(started.failover_to_backup);
    EXPECT_TRUE(recovery.primary_isolated());
    EXPECT_EQ(recovery.gps_state(), flight::RecoveryState::Verifying);
    EXPECT_EQ(recovery.gps_attempts(), 1);

    gps.primary_selected = false;
    gps.backup_usable_run = 3;
    gps.backup_unusable_run = 0;
    (void)recovery.observe({}, gps, Time{}, events);
    EXPECT_EQ(recovery.gps_state(), flight::RecoveryState::Succeeded);
    EXPECT_TRUE(recovery.suppress_primary_warning());
    EXPECT_FALSE(recovery.blocks_operational_progress());

    gps.primary_persistent = false;
    gps.primary_selected = true;
    (void)recovery.observe({}, gps, Time{}, events);
    EXPECT_EQ(recovery.gps_state(), flight::RecoveryState::Succeeded);
    EXPECT_EQ(recovery.gps_attempts(), 1);
    EXPECT_EQ(recovery_events(events), 2U);
}

TEST(RecoveryManager, UnusableBackupFailsTheSingleSwitch) {
    flight::RecoveryManager<Time> recovery;
    ares::core::EventLog<flight::SystemEvent<Time>> events;
    flight::GpsRecoveryFact gps;
    gps.primary_selected = true;
    gps.primary_persistent = true;
    (void)recovery.observe({}, gps, Time{}, events);
    gps.primary_selected = false;
    gps.backup_unusable_run = 3;
    (void)recovery.observe({}, gps, Time{}, events);
    EXPECT_EQ(recovery.gps_state(), flight::RecoveryState::Failed);
    EXPECT_TRUE(recovery.blocks_operational_progress());
    (void)recovery.observe({}, gps, Time{}, events);
    EXPECT_EQ(recovery.gps_attempts(), 1);
    EXPECT_EQ(recovery_events(events), 2U);
}

TEST(FdirController, SafeModeWaitsForNavigationVerification) {
    Clock clock;
    std::ostringstream output;
    ares::core::Logger<Clock> logger(output, clock);
    ares::core::EventLog<flight::SystemEvent<Time>> events;
    flight::FlightExecutive<Clock> executive(clock, logger, events);
    flight::FdirController<Clock> fdir(events);
    ASSERT_EQ(executive.boot_to_standby().status, flight::TransitionStatus::Accepted);
    ASSERT_EQ(executive.accept(flight::Command::StartMission), flight::CommandStatus::Accepted);

    const Time time = clock.now().time;
    for (int miss = 0; miss < 5; ++miss) {
        EXPECT_NE(fdir.observe_deadline(flight::FaultSource::NavigationTask,
                                        flight::DeadlineFact::Missed, time),
                  flight::RegistryStatus::RejectedSource);
    }
    const flight::PolicyDecision entered = fdir.apply(executive);
    EXPECT_EQ(entered.action, flight::RecoveryAction::EnterSafeMode);
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::SafeMode);

    flight::FaultMailbox<Clock>::Snapshot sample;
    const flight::RecoveryCommand restart = fdir.advance_recovery(sample, 0, true, time);
    EXPECT_TRUE(restart.restart_navigation);
    const flight::PolicyDecision held = fdir.apply(executive);
    EXPECT_FALSE(held.requested_mode.has_value());
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::SafeMode);

    EXPECT_EQ(fdir.observe_deadline(flight::FaultSource::NavigationTask,
                                    flight::DeadlineFact::OnTime, time),
              flight::RegistryStatus::Cleared);
    sample.navigation.pending = true;
    sample.navigation.generation = 1;
    sample.navigation.on_time_run = 1;
    (void)fdir.advance_recovery(sample, 1, true, time);
    EXPECT_EQ(fdir.recovery().navigation_state(), flight::RecoveryState::Verifying);
    (void)fdir.apply(executive);
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::SafeMode);

    sample.navigation.on_time_run = 3;
    (void)fdir.advance_recovery(sample, 1, true, time);
    EXPECT_EQ(fdir.recovery().navigation_state(), flight::RecoveryState::Succeeded);
    for (int cycle = 0; cycle < 3; ++cycle) {
        (void)fdir.apply(executive);
    }
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::Standby);
}

TEST(FdirController, IsolatedPrimaryWarningDoesNotBlockStandby) {
    Clock clock;
    std::ostringstream output;
    ares::core::Logger<Clock> logger(output, clock);
    ares::core::EventLog<flight::SystemEvent<Time>> events;
    flight::FlightExecutive<Clock> executive(clock, logger, events);
    flight::FdirController<Clock> fdir(events);
    ASSERT_EQ(executive.boot_to_standby().status, flight::TransitionStatus::Accepted);
    ASSERT_EQ(executive.accept(flight::Command::StartMission), flight::CommandStatus::Accepted);
    const Time time = clock.now().time;
    for (int sample = 0; sample < 3; ++sample) {
        EXPECT_NE(fdir.observe_sensor(flight::FaultSource::PrimaryGps,
                                      flight::SampleUsability::Stale, time),
                  flight::RegistryStatus::RejectedSource);
    }
    EXPECT_EQ(fdir.apply(executive).action, flight::RecoveryAction::ContinueDegraded);

    flight::FaultMailbox<Clock>::Snapshot mailbox;
    EXPECT_TRUE(fdir.advance_recovery(mailbox, 0, true, time).failover_to_backup);
    mailbox.backup_gps.usable_run = 3;
    (void)fdir.advance_recovery(mailbox, 0, false, time);
    EXPECT_EQ(fdir.recovery().gps_state(), flight::RecoveryState::Succeeded);
    EXPECT_EQ(fdir.apply(executive).requested_mode, flight::SpacecraftMode::Nominal);
    const flight::FaultRecord<Time>* primary =
        fdir.registry().find(flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps);
    ASSERT_NE(primary, nullptr);
    EXPECT_TRUE(primary->active);
}

TEST(FdirController, PreSwitchBackupUsableRunDoesNotVerifyFailover) {
    Clock clock;
    std::ostringstream output;
    ares::core::Logger<Clock> logger(output, clock);
    ares::core::EventLog<flight::SystemEvent<Time>> events;
    flight::FlightExecutive<Clock> executive(clock, logger, events);
    flight::FdirController<Clock> fdir(events);
    flight::FaultMailbox<Clock> mailbox;
    ASSERT_EQ(executive.boot_to_standby().status, flight::TransitionStatus::Accepted);
    ASSERT_EQ(executive.accept(flight::Command::StartMission), flight::CommandStatus::Accepted);

    for (int sample = 0; sample < 3; ++sample) {
        mailbox.publish_sensor(flight::FaultSource::BackupGps, flight::SampleUsability::Usable);
    }

    const std::stop_token running;
    for (int cycle = 0; cycle < 2; ++cycle) {
        mailbox.publish_sensor(flight::FaultSource::PrimaryGps, flight::SampleUsability::Stale);
        (void)run_fdir_cycle(fdir, mailbox, executive, clock.now(), running, 0, true);
        EXPECT_EQ(fdir.recovery().gps_state(), flight::RecoveryState::Idle);
    }
    mailbox.publish_sensor(flight::FaultSource::PrimaryGps, flight::SampleUsability::Stale);
    (void)run_fdir_cycle(fdir, mailbox, executive, clock.now(), running, 0, true);
    EXPECT_TRUE(fdir.last_recovery_command().failover_to_backup);
    EXPECT_EQ(fdir.recovery().gps_state(), flight::RecoveryState::Verifying);
    EXPECT_EQ(fdir.recovery().gps_attempts(), 1);

    for (int sample = 1; sample <= 2; ++sample) {
        mailbox.publish_sensor(flight::FaultSource::BackupGps, flight::SampleUsability::Usable);
        (void)run_fdir_cycle(fdir, mailbox, executive, clock.now(), running, 0, false);
        EXPECT_EQ(fdir.recovery().gps_state(), flight::RecoveryState::Verifying) << sample;
    }
    mailbox.publish_sensor(flight::FaultSource::BackupGps, flight::SampleUsability::Usable);
    (void)run_fdir_cycle(fdir, mailbox, executive, clock.now(), running, 0, false);
    EXPECT_EQ(fdir.recovery().gps_state(), flight::RecoveryState::Succeeded);
    EXPECT_EQ(fdir.recovery().gps_attempts(), 1);
}

TEST(FdirController, OpenNavigationRecoveryBlocksDegradedReturnToNominal) {
    Clock clock;
    std::ostringstream output;
    ares::core::Logger<Clock> logger(output, clock);
    ares::core::EventLog<flight::SystemEvent<Time>> events;
    flight::FlightExecutive<Clock> executive(clock, logger, events);
    flight::FdirController<Clock> fdir(events);
    ASSERT_EQ(executive.boot_to_standby().status, flight::TransitionStatus::Accepted);
    ASSERT_EQ(executive.accept(flight::Command::StartMission), flight::CommandStatus::Accepted);
    const Time time = clock.now().time;
    for (int sample = 0; sample < 3; ++sample) {
        EXPECT_NE(
            fdir.observe_sensor(flight::FaultSource::Imu, flight::SampleUsability::Stale, time),
            flight::RegistryStatus::RejectedSource);
    }
    EXPECT_EQ(fdir.apply(executive).requested_mode, flight::SpacecraftMode::Degraded);
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::Degraded);

    for (int miss = 0; miss < 5; ++miss) {
        EXPECT_NE(fdir.observe_deadline(flight::FaultSource::NavigationTask,
                                        flight::DeadlineFact::Missed, time),
                  flight::RegistryStatus::RejectedSource);
    }
    flight::FaultMailbox<Clock>::Snapshot sample;
    EXPECT_TRUE(fdir.advance_recovery(sample, 0, true, time).restart_navigation);
    EXPECT_EQ(fdir.recovery().navigation_state(), flight::RecoveryState::Executing);

    EXPECT_EQ(fdir.observe_sensor(flight::FaultSource::Imu, flight::SampleUsability::Usable, time),
              flight::RegistryStatus::Cleared);
    EXPECT_EQ(fdir.observe_deadline(flight::FaultSource::NavigationTask,
                                    flight::DeadlineFact::OnTime, time),
              flight::RegistryStatus::Cleared);
    (void)fdir.advance_recovery(sample, 0, true, time);
    EXPECT_EQ(fdir.recovery().navigation_state(), flight::RecoveryState::Executing);
    EXPECT_EQ(fdir.evaluate(executive.mode()).requested_mode, flight::SpacecraftMode::Nominal);
    const flight::PolicyDecision executing = fdir.apply(executive);
    EXPECT_FALSE(executing.requested_mode.has_value());
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::Degraded);

    sample.navigation.pending = true;
    sample.navigation.generation = 1;
    sample.navigation.on_time_run = 1;
    (void)fdir.advance_recovery(sample, 1, true, time);
    EXPECT_EQ(fdir.recovery().navigation_state(), flight::RecoveryState::Verifying);
    EXPECT_EQ(fdir.evaluate(executive.mode()).requested_mode, flight::SpacecraftMode::Nominal);
    EXPECT_FALSE(fdir.apply(executive).requested_mode.has_value());
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::Degraded);

    sample.navigation.on_time_run = 2;
    (void)fdir.advance_recovery(sample, 1, true, time);
    EXPECT_EQ(fdir.recovery().navigation_state(), flight::RecoveryState::Verifying);
    EXPECT_FALSE(fdir.apply(executive).requested_mode.has_value());
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::Degraded);

    sample.navigation.on_time_run = 3;
    (void)fdir.advance_recovery(sample, 1, true, time);
    EXPECT_EQ(fdir.recovery().navigation_state(), flight::RecoveryState::Succeeded);
    EXPECT_EQ(fdir.apply(executive).requested_mode, flight::SpacecraftMode::Nominal);
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::Nominal);
}

TEST(FdirController, FailedNavigationBlocksDegradedReturnToNominal) {
    Clock clock;
    std::ostringstream output;
    ares::core::Logger<Clock> logger(output, clock);
    ares::core::EventLog<flight::SystemEvent<Time>> events;
    flight::FlightExecutive<Clock> executive(clock, logger, events);
    flight::FdirController<Clock> fdir(events);
    ASSERT_EQ(executive.boot_to_standby().status, flight::TransitionStatus::Accepted);
    ASSERT_EQ(executive.accept(flight::Command::StartMission), flight::CommandStatus::Accepted);
    const Time time = clock.now().time;
    for (int sample = 0; sample < 3; ++sample) {
        EXPECT_NE(
            fdir.observe_sensor(flight::FaultSource::Imu, flight::SampleUsability::Stale, time),
            flight::RegistryStatus::RejectedSource);
    }
    EXPECT_EQ(fdir.apply(executive).requested_mode, flight::SpacecraftMode::Degraded);

    for (int miss = 0; miss < 5; ++miss) {
        EXPECT_NE(fdir.observe_deadline(flight::FaultSource::NavigationTask,
                                        flight::DeadlineFact::Missed, time),
                  flight::RegistryStatus::RejectedSource);
    }
    const flight::FaultMailbox<Clock>::Snapshot sample;
    (void)fdir.advance_recovery(sample, std::numeric_limits<std::uint32_t>::max(), true, time);
    EXPECT_EQ(fdir.recovery().navigation_state(), flight::RecoveryState::Failed);

    EXPECT_EQ(fdir.observe_sensor(flight::FaultSource::Imu, flight::SampleUsability::Usable, time),
              flight::RegistryStatus::Cleared);
    EXPECT_EQ(fdir.observe_deadline(flight::FaultSource::NavigationTask,
                                    flight::DeadlineFact::OnTime, time),
              flight::RegistryStatus::Cleared);
    EXPECT_EQ(fdir.recovery().navigation_state(), flight::RecoveryState::Failed);
    EXPECT_EQ(fdir.evaluate(executive.mode()).requested_mode, flight::SpacecraftMode::Nominal);
    const flight::PolicyDecision held = fdir.apply(executive);
    EXPECT_FALSE(held.requested_mode.has_value());
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::Degraded);
    EXPECT_EQ(fdir.recovery().navigation_state(), flight::RecoveryState::Failed);
}
