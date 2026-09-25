#include "ares/core/clock.hpp"
#include "ares/flight/fault_mailbox.hpp"

#include <gtest/gtest.h>

namespace flight = ares::flight;
namespace hardware = ares::hardware;

TEST(FaultMailbox, LatestSensorObservationReplacesThePendingOne) {
    flight::FaultMailbox<ares::core::ManualClock> mailbox;
    mailbox.publish_sensor(flight::FaultSource::PrimaryGps, flight::SampleUsability::Stale);
    mailbox.publish_sensor(flight::FaultSource::PrimaryGps, flight::SampleUsability::Usable);
    mailbox.publish_sensor(flight::FaultSource::Imu, flight::SampleUsability::Invalid);

    const auto sample = mailbox.consume();
    EXPECT_TRUE(sample.gps.pending);
    EXPECT_TRUE(sample.gps.usable_seen);
    EXPECT_EQ(sample.gps.usability, flight::SampleUsability::Usable);
    EXPECT_TRUE(sample.imu.pending);
    EXPECT_FALSE(sample.imu.usable_seen);
    EXPECT_EQ(sample.imu.usability, flight::SampleUsability::Invalid);
    EXPECT_FALSE(sample.temperature.pending);
    EXPECT_FALSE(sample.battery.pending);

    const auto empty = mailbox.consume();
    EXPECT_FALSE(empty.gps.pending);
    EXPECT_FALSE(empty.gps.usable_seen);
    EXPECT_FALSE(empty.imu.pending);
    EXPECT_FALSE(empty.imu.usable_seen);
}

TEST(FaultMailbox, DeadlineMissSticksUntilConsumed) {
    flight::FaultMailbox<ares::core::ManualClock> mailbox;
    mailbox.publish_deadline(flight::FaultSource::NavigationTask, true);
    mailbox.publish_deadline(flight::FaultSource::NavigationTask, false);

    const auto pending = mailbox.consume();
    EXPECT_TRUE(pending.navigation.pending);
    EXPECT_TRUE(pending.navigation.missed);

    mailbox.publish_deadline(flight::FaultSource::NavigationTask, false);
    const auto recovered = mailbox.consume();
    EXPECT_TRUE(recovered.navigation.pending);
    EXPECT_FALSE(recovered.navigation.missed);
}

TEST(FaultMailbox, BatteryAndTaskSlotsStaySeparate) {
    flight::FaultMailbox<ares::core::ManualClock> mailbox;
    mailbox.publish_battery(flight::SampleUsability::Usable, hardware::Millivolts{10999});
    mailbox.publish_deadline(flight::FaultSource::HealthTask, true);
    mailbox.publish_deadline(flight::FaultSource::CommunicationsTask, false);
    mailbox.publish_sensor(flight::FaultSource::Battery, flight::SampleUsability::Stale);

    const auto sample = mailbox.consume();
    EXPECT_TRUE(sample.battery.pending);
    EXPECT_EQ(sample.battery.voltage.count, 10999);
    EXPECT_EQ(sample.battery.usability, flight::SampleUsability::Usable);
    EXPECT_TRUE(sample.health.pending);
    EXPECT_TRUE(sample.health.missed);
    EXPECT_TRUE(sample.communications.pending);
    EXPECT_FALSE(sample.communications.missed);
    EXPECT_FALSE(sample.navigation.pending);
}

TEST(FaultMailbox, OnTimeRunKeepsTheFinalConsecutiveStreak) {
    flight::FaultMailbox<ares::core::ManualClock> mailbox;
    mailbox.publish_deadline(flight::FaultSource::NavigationTask, false, 1);
    mailbox.publish_deadline(flight::FaultSource::NavigationTask, false, 1);
    EXPECT_EQ(mailbox.consume().navigation.on_time_run, 2);

    flight::FaultMailbox<ares::core::ManualClock> interrupted;
    interrupted.publish_deadline(flight::FaultSource::NavigationTask, false, 1);
    interrupted.publish_deadline(flight::FaultSource::NavigationTask, true, 1);
    EXPECT_EQ(interrupted.consume().navigation.on_time_run, 0);

    flight::FaultMailbox<ares::core::ManualClock> after_miss;
    after_miss.publish_deadline(flight::FaultSource::NavigationTask, true, 1);
    after_miss.publish_deadline(flight::FaultSource::NavigationTask, false, 1);
    const auto one = after_miss.consume();
    EXPECT_TRUE(one.navigation.missed);
    EXPECT_EQ(one.navigation.on_time_run, 1);

    flight::FaultMailbox<ares::core::ManualClock> reset_then_one;
    reset_then_one.publish_deadline(flight::FaultSource::NavigationTask, false, 1);
    reset_then_one.publish_deadline(flight::FaultSource::NavigationTask, true, 1);
    reset_then_one.publish_deadline(flight::FaultSource::NavigationTask, false, 1);
    EXPECT_EQ(reset_then_one.consume().navigation.on_time_run, 1);

    flight::FaultMailbox<ares::core::ManualClock> two_after_miss;
    two_after_miss.publish_deadline(flight::FaultSource::NavigationTask, true, 1);
    two_after_miss.publish_deadline(flight::FaultSource::NavigationTask, false, 1);
    two_after_miss.publish_deadline(flight::FaultSource::NavigationTask, false, 1);
    EXPECT_EQ(two_after_miss.consume().navigation.on_time_run, 2);
}

TEST(FaultMailbox, DeadlineGenerationChangeDropsThePreviousRun) {
    flight::FaultMailbox<ares::core::ManualClock> mailbox;
    mailbox.publish_deadline(flight::FaultSource::NavigationTask, false, 4);
    mailbox.publish_deadline(flight::FaultSource::NavigationTask, false, 4);
    mailbox.publish_deadline(flight::FaultSource::NavigationTask, false, 5);
    const auto sample = mailbox.consume();
    EXPECT_EQ(sample.navigation.generation, 5U);
    EXPECT_EQ(sample.navigation.on_time_run, 1);
}

TEST(FaultMailbox, BackupRunIsPhysicalAndSurvivesConsumeUntilRebaseline) {
    flight::FaultMailbox<ares::core::ManualClock> mailbox;
    mailbox.publish_sensor(flight::FaultSource::BackupGps, flight::SampleUsability::Usable);
    mailbox.publish_sensor(flight::FaultSource::BackupGps, flight::SampleUsability::Usable);
    mailbox.publish_sensor(flight::FaultSource::BackupGps, flight::SampleUsability::Stale);
    const auto broken = mailbox.consume();
    EXPECT_EQ(broken.backup_gps.usable_run, 0);
    EXPECT_EQ(broken.backup_gps.unusable_run, 1);
    EXPECT_TRUE(broken.backup_gps.pending);

    mailbox.publish_sensor(flight::FaultSource::BackupGps, flight::SampleUsability::Usable);
    EXPECT_EQ(mailbox.consume().backup_gps.usable_run, 1);
    mailbox.rebaseline_backup_run();
    EXPECT_EQ(mailbox.consume().backup_gps.usable_run, 0);
}
