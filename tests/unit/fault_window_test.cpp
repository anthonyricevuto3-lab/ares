#include "ares/core/logger.hpp"
#include "ares/flight/fdir.hpp"

#include <sstream>
#include <stop_token>

#include <gtest/gtest.h>

namespace flight = ares::flight;
using namespace std::chrono_literals;
using Clock = ares::core::ManualClock;
using Time = Clock::time_point;

namespace {

struct Harness {
    Clock clock{};
    std::ostringstream output{};
    ares::core::Logger<Clock> logger;
    ares::core::EventLog<flight::SystemEvent<Time>> events{};
    flight::FlightExecutive<Clock> executive;
    flight::FdirController<Clock> fdir;
    flight::FaultMailbox<Clock> mailbox{};

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

    void seed_gps_stale(int count) {
        for (int index = 0; index < count; ++index) {
            ASSERT_EQ(clock.advance(1s), ares::core::AdvanceStatus::Applied);
            const flight::RegistryStatus status = fdir.observe_sensor(
                flight::FaultSource::PrimaryGps, flight::SampleUsability::Stale, stamp());
            EXPECT_EQ(status, index == 0 ? flight::RegistryStatus::Activated
                                         : flight::RegistryStatus::Updated);
        }
    }

    [[nodiscard]] const flight::FaultRecord<Time>* gps(flight::FaultType type) const {
        return fdir.registry().find(type, flight::FaultSource::PrimaryGps);
    }

    [[nodiscard]] flight::PolicyDecision consume() {
        return flight::run_fdir_cycle(fdir, mailbox, executive, clock.now(), {});
    }
};

} // namespace

TEST(FaultWindow, UsableThenStaleRestartsACountOfOne) {
    Harness harness;
    harness.reach_nominal();
    harness.seed_gps_stale(1);
    ASSERT_EQ(harness.gps(flight::FaultType::SensorStale)->consecutive_count, 1U);

    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps,
                                   flight::SampleUsability::Usable);
    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps, flight::SampleUsability::Stale);
    (void)harness.consume();

    const flight::FaultRecord<Time>* stale = harness.gps(flight::FaultType::SensorStale);
    ASSERT_NE(stale, nullptr);
    EXPECT_TRUE(stale->active);
    EXPECT_EQ(stale->consecutive_count, 1U);
    EXPECT_EQ(stale->occurrence_count, 2U);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Nominal);
}

TEST(FaultWindow, UsableThenStaleRestartsACountOfTwoAndStaysNominal) {
    Harness harness;
    harness.reach_nominal();
    harness.seed_gps_stale(2);
    ASSERT_EQ(harness.gps(flight::FaultType::SensorStale)->consecutive_count, 2U);

    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps,
                                   flight::SampleUsability::Usable);
    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps, flight::SampleUsability::Stale);
    const flight::PolicyDecision decision = harness.consume();

    const flight::FaultRecord<Time>* stale = harness.gps(flight::FaultType::SensorStale);
    ASSERT_NE(stale, nullptr);
    EXPECT_TRUE(stale->active);
    EXPECT_EQ(stale->consecutive_count, 1U);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Nominal);
    EXPECT_EQ(decision.action, flight::RecoveryAction::None);
    EXPECT_FALSE(decision.requested_mode.has_value());
}

TEST(FaultWindow, StaleUsableStaleInOneWindowStartsAtOne) {
    Harness harness;
    harness.reach_nominal();
    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps, flight::SampleUsability::Stale);
    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps,
                                   flight::SampleUsability::Usable);
    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps, flight::SampleUsability::Stale);

    const auto sample = harness.mailbox.consume();
    EXPECT_TRUE(sample.gps.pending);
    EXPECT_TRUE(sample.gps.usable_seen);
    EXPECT_EQ(sample.gps.usability, flight::SampleUsability::Stale);
    harness.fdir.ingest(sample, harness.stamp());

    const flight::FaultRecord<Time>* stale = harness.gps(flight::FaultType::SensorStale);
    ASSERT_NE(stale, nullptr);
    EXPECT_TRUE(stale->active);
    EXPECT_EQ(stale->consecutive_count, 1U);
    EXPECT_EQ(stale->occurrence_count, 1U);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Nominal);
}

TEST(FaultWindow, InvalidThenUsableClearsSensorHealth) {
    Harness harness;
    harness.reach_nominal();
    harness.seed_gps_stale(1);

    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps,
                                   flight::SampleUsability::Invalid);
    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps,
                                   flight::SampleUsability::Usable);
    (void)harness.consume();

    const flight::FaultRecord<Time>* stale = harness.gps(flight::FaultType::SensorStale);
    ASSERT_NE(stale, nullptr);
    EXPECT_FALSE(stale->active);
    EXPECT_EQ(stale->consecutive_count, 0U);
    EXPECT_EQ(harness.gps(flight::FaultType::SensorInvalid), nullptr);
    EXPECT_EQ(harness.fdir.registry().active_count(), 0U);
}

TEST(FaultWindow, UsableThenInvalidClearsOldPersistence) {
    Harness harness;
    harness.reach_nominal();
    harness.seed_gps_stale(2);

    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps,
                                   flight::SampleUsability::Usable);
    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps,
                                   flight::SampleUsability::Invalid);
    (void)harness.consume();

    const flight::FaultRecord<Time>* stale = harness.gps(flight::FaultType::SensorStale);
    const flight::FaultRecord<Time>* invalid = harness.gps(flight::FaultType::SensorInvalid);
    ASSERT_NE(stale, nullptr);
    ASSERT_NE(invalid, nullptr);
    EXPECT_FALSE(stale->active);
    EXPECT_EQ(stale->consecutive_count, 0U);
    EXPECT_TRUE(invalid->active);
    EXPECT_EQ(invalid->consecutive_count, 1U);
    EXPECT_EQ(invalid->occurrence_count, 1U);
}

TEST(FaultWindow, UsableThenFutureClearsAndRaisesNothing) {
    Harness harness;
    harness.reach_nominal();
    harness.seed_gps_stale(2);

    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps,
                                   flight::SampleUsability::Usable);
    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps,
                                   flight::SampleUsability::Future);
    const flight::PolicyDecision decision = harness.consume();

    const flight::FaultRecord<Time>* stale = harness.gps(flight::FaultType::SensorStale);
    ASSERT_NE(stale, nullptr);
    EXPECT_FALSE(stale->active);
    EXPECT_EQ(stale->consecutive_count, 0U);
    EXPECT_EQ(stale->occurrence_count, 2U);
    EXPECT_EQ(harness.fdir.registry().active_count(), 0U);
    EXPECT_EQ(decision.action, flight::RecoveryAction::None);
    EXPECT_FALSE(decision.requested_mode.has_value());
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Nominal);
}

TEST(FaultWindow, UsableThenTimeErrorClearsAndRaisesNothing) {
    Harness harness;
    harness.reach_nominal();
    harness.seed_gps_stale(2);

    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps,
                                   flight::SampleUsability::Usable);
    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps,
                                   flight::SampleUsability::TimeError);
    (void)harness.consume();

    const flight::FaultRecord<Time>* stale = harness.gps(flight::FaultType::SensorStale);
    ASSERT_NE(stale, nullptr);
    EXPECT_FALSE(stale->active);
    EXPECT_EQ(stale->consecutive_count, 0U);
    EXPECT_EQ(harness.fdir.registry().active_count(), 0U);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Nominal);
}

TEST(FaultWindow, ThreeStaleConsumesStillReachDegraded) {
    Harness harness;
    harness.reach_nominal();
    flight::PolicyDecision last{};
    for (int sample = 0; sample < 3; ++sample) {
        harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps,
                                       flight::SampleUsability::Stale);
        last = harness.consume();
        const flight::FaultRecord<Time>* stale = harness.gps(flight::FaultType::SensorStale);
        ASSERT_NE(stale, nullptr);
        EXPECT_EQ(stale->consecutive_count, static_cast<std::uint32_t>(sample + 1));
        if (sample < 2) {
            EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Nominal);
            EXPECT_FALSE(last.requested_mode.has_value());
        }
    }
    EXPECT_EQ(harness.gps(flight::FaultType::SensorStale)->consecutive_count, 3U);
    EXPECT_EQ(last.action, flight::RecoveryAction::ContinueDegraded);
    ASSERT_TRUE(last.requested_mode.has_value());
    EXPECT_EQ(*last.requested_mode, flight::SpacecraftMode::Degraded);
    ASSERT_TRUE(last.transition.has_value());
    EXPECT_EQ(*last.transition, flight::TransitionStatus::Accepted);
    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Degraded);
}

TEST(FaultWindow, StaleFutureStaleContinuesAcrossConsumes) {
    Harness harness;
    harness.reach_nominal();
    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps, flight::SampleUsability::Stale);
    (void)harness.consume();
    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps,
                                   flight::SampleUsability::Future);
    (void)harness.consume();
    const flight::FaultRecord<Time>* during = harness.gps(flight::FaultType::SensorStale);
    ASSERT_NE(during, nullptr);
    EXPECT_TRUE(during->active);
    EXPECT_EQ(during->consecutive_count, 1U);

    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps, flight::SampleUsability::Stale);
    (void)harness.consume();
    EXPECT_TRUE(during->active);
    EXPECT_EQ(during->consecutive_count, 2U);
    EXPECT_EQ(during->occurrence_count, 2U);
}

TEST(FaultWindow, StaleTimeErrorStaleContinuesAcrossConsumes) {
    Harness harness;
    harness.reach_nominal();
    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps, flight::SampleUsability::Stale);
    (void)harness.consume();
    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps,
                                   flight::SampleUsability::TimeError);
    (void)harness.consume();
    const flight::FaultRecord<Time>* during = harness.gps(flight::FaultType::SensorStale);
    ASSERT_NE(during, nullptr);
    EXPECT_EQ(during->consecutive_count, 1U);

    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps, flight::SampleUsability::Stale);
    (void)harness.consume();
    EXPECT_EQ(during->consecutive_count, 2U);
    EXPECT_TRUE(during->active);
}

TEST(FaultWindow, StaleInvalidStaleRestartsEachRecord) {
    Harness harness;
    harness.reach_nominal();
    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps, flight::SampleUsability::Stale);
    (void)harness.consume();
    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps,
                                   flight::SampleUsability::Invalid);
    (void)harness.consume();

    const flight::FaultRecord<Time>* stale = harness.gps(flight::FaultType::SensorStale);
    const flight::FaultRecord<Time>* invalid = harness.gps(flight::FaultType::SensorInvalid);
    ASSERT_NE(stale, nullptr);
    ASSERT_NE(invalid, nullptr);
    EXPECT_FALSE(stale->active);
    EXPECT_EQ(stale->consecutive_count, 0U);
    EXPECT_TRUE(invalid->active);
    EXPECT_EQ(invalid->consecutive_count, 1U);

    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps, flight::SampleUsability::Stale);
    (void)harness.consume();
    EXPECT_TRUE(stale->active);
    EXPECT_EQ(stale->consecutive_count, 1U);
    EXPECT_EQ(stale->occurrence_count, 2U);
    EXPECT_FALSE(invalid->active);
    EXPECT_EQ(invalid->consecutive_count, 0U);
}

TEST(FaultWindow, StopBeforeConsumeLeavesTheObservationPending) {
    // Pending samples are intentionally not consumed after stop. This test
    // starts no worker, so shutdown cannot race or deadlock. In the mission
    // runtime the supervisor is destroyed first and joins publishers before
    // the mailbox is destroyed, which is when a still-pending sample is dropped.
    Harness harness;
    harness.reach_nominal();
    harness.mailbox.publish_sensor(flight::FaultSource::PrimaryGps, flight::SampleUsability::Stale);

    std::stop_source source;
    source.request_stop();
    const flight::PolicyDecision decision = flight::run_fdir_cycle(
        harness.fdir, harness.mailbox, harness.executive, harness.clock.now(), source.get_token());
    harness.mailbox.publish_deadline(flight::FaultSource::HealthTask, true);

    EXPECT_EQ(harness.executive.mode(), flight::SpacecraftMode::Nominal);
    EXPECT_EQ(harness.gps(flight::FaultType::SensorStale), nullptr);
    EXPECT_EQ(decision.action, flight::RecoveryAction::None);
    EXPECT_FALSE(decision.requested_mode.has_value());
    EXPECT_FALSE(decision.transition.has_value());

    const auto sample = harness.mailbox.consume();
    EXPECT_TRUE(sample.gps.pending);
    EXPECT_EQ(sample.gps.usability, flight::SampleUsability::Stale);
    EXPECT_TRUE(sample.health.pending);
    EXPECT_TRUE(sample.health.missed);
}
