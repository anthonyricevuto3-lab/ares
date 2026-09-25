#include "ares/core/event_log.hpp"
#include "ares/flight/fdir.hpp"
#include "ares/flight/fdir_limits.hpp"

#include <gtest/gtest.h>

namespace flight = ares::flight;
namespace hardware = ares::hardware;
using namespace std::chrono_literals;
using Time = ares::core::ManualClock::time_point;

static_assert(flight::limits::kLowBatteryActivate.count == 11000);
static_assert(flight::limits::kLowBatteryClear.count == 11500);
static_assert(flight::limits::kWarningPersistence == 3);

namespace {

class Harness {
public:
    flight::FdirController<ares::core::ManualClock>& fdir() { return fdir_; }
    [[nodiscard]] const flight::FaultRecord<Time>* find(flight::FaultType type,
                                                        flight::FaultSource source) const {
        return fdir_.registry().find(type, source);
    }
    [[nodiscard]] std::size_t count_events() const {
        std::size_t activated = 0;
        std::size_t updated = 0;
        std::size_t cleared = 0;
        for (const flight::SystemEvent<Time>& event : events_.snapshot()) {
            if (std::holds_alternative<flight::FaultActivatedEvent<Time>>(event)) {
                ++activated;
            } else if (std::holds_alternative<flight::FaultUpdatedEvent<Time>>(event)) {
                ++updated;
            } else if (std::holds_alternative<flight::FaultClearedEvent<Time>>(event)) {
                ++cleared;
            }
        }
        activated_ = activated;
        updated_ = updated;
        cleared_ = cleared;
        return events_.size();
    }
    [[nodiscard]] std::size_t activated() const { return activated_; }
    [[nodiscard]] std::size_t updated() const { return updated_; }
    [[nodiscard]] std::size_t cleared() const { return cleared_; }

private:
    ares::core::EventLog<flight::SystemEvent<Time>> events_{};
    flight::FdirController<ares::core::ManualClock> fdir_{events_};
    mutable std::size_t activated_{0};
    mutable std::size_t updated_{0};
    mutable std::size_t cleared_{0};
};

} // namespace

TEST(FaultDetection, SensorUsabilityMapsToTheMatchingFault) {
    EXPECT_EQ(flight::detect_sensor(flight::SampleUsability::Unavailable).action,
              flight::SensorDetectionAction::Raise);
    EXPECT_EQ(flight::detect_sensor(flight::SampleUsability::Unavailable).type,
              flight::FaultType::SensorUnavailable);
    EXPECT_EQ(flight::detect_sensor(flight::SampleUsability::Invalid).type,
              flight::FaultType::SensorInvalid);
    EXPECT_EQ(flight::detect_sensor(flight::SampleUsability::Stale).type,
              flight::FaultType::SensorStale);
    EXPECT_EQ(flight::detect_sensor(flight::SampleUsability::Usable).action,
              flight::SensorDetectionAction::Clear);
    EXPECT_EQ(flight::detect_sensor(flight::SampleUsability::Future).action,
              flight::SensorDetectionAction::Hold);
    EXPECT_EQ(flight::detect_sensor(flight::SampleUsability::TimeError).action,
              flight::SensorDetectionAction::Hold);
}

TEST(FaultDetection, UnavailableInvalidAndStaleActivateSeparateTypes) {
    Harness harness;
    const Time time = Time{} + 1s;
    EXPECT_EQ(harness.fdir().observe_sensor(flight::FaultSource::Imu,
                                            flight::SampleUsability::Unavailable, time),
              flight::RegistryStatus::Activated);
    EXPECT_EQ(harness.fdir().observe_sensor(flight::FaultSource::PrimaryGps,
                                            flight::SampleUsability::Invalid, time),
              flight::RegistryStatus::Activated);
    EXPECT_EQ(harness.fdir().observe_sensor(flight::FaultSource::Temperature,
                                            flight::SampleUsability::Stale, time),
              flight::RegistryStatus::Activated);

    const flight::FaultRecord<Time>* imu =
        harness.find(flight::FaultType::SensorUnavailable, flight::FaultSource::Imu);
    const flight::FaultRecord<Time>* gps =
        harness.find(flight::FaultType::SensorInvalid, flight::FaultSource::PrimaryGps);
    const flight::FaultRecord<Time>* thermal =
        harness.find(flight::FaultType::SensorStale, flight::FaultSource::Temperature);
    ASSERT_NE(imu, nullptr);
    ASSERT_NE(gps, nullptr);
    ASSERT_NE(thermal, nullptr);
    EXPECT_TRUE(imu->active);
    EXPECT_TRUE(gps->active);
    EXPECT_TRUE(thermal->active);
    EXPECT_EQ(imu->severity, flight::FaultSeverity::Warning);
    EXPECT_EQ(gps->severity, flight::FaultSeverity::Warning);
    EXPECT_EQ(thermal->severity, flight::FaultSeverity::Warning);
}

TEST(FaultDetection, UsableClearsTheActiveSensorFaultAndKeepsHistory) {
    Harness harness;
    const Time stale_at = Time{} + 2s;
    const Time valid_at = Time{} + 3s;
    ASSERT_EQ(harness.fdir().observe_sensor(flight::FaultSource::PrimaryGps,
                                            flight::SampleUsability::Stale, stale_at),
              flight::RegistryStatus::Activated);
    EXPECT_EQ(harness.fdir().observe_sensor(flight::FaultSource::PrimaryGps,
                                            flight::SampleUsability::Usable, valid_at),
              flight::RegistryStatus::Cleared);
    const flight::FaultRecord<Time>* record =
        harness.find(flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps);
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(record->active);
    EXPECT_EQ(record->first_detected, stale_at);
    EXPECT_EQ(record->last_detected, stale_at);
    EXPECT_EQ(record->occurrence_count, 1U);
    EXPECT_EQ(record->consecutive_count, 0U);
    (void)harness.count_events();
    EXPECT_EQ(harness.activated(), 1U);
    EXPECT_EQ(harness.cleared(), 1U);
    EXPECT_EQ(harness.updated(), 0U);
}

TEST(FaultDetection, FutureAndTimeErrorDoNotRaiseOrClear) {
    Harness harness;
    const Time time = Time{} + 1s;
    ASSERT_EQ(harness.fdir().observe_sensor(flight::FaultSource::PrimaryGps,
                                            flight::SampleUsability::Stale, time),
              flight::RegistryStatus::Activated);
    EXPECT_EQ(harness.fdir().observe_sensor(flight::FaultSource::PrimaryGps,
                                            flight::SampleUsability::Future, time + 1s),
              flight::RegistryStatus::Unchanged);
    EXPECT_EQ(harness.fdir().observe_sensor(flight::FaultSource::PrimaryGps,
                                            flight::SampleUsability::TimeError, time + 2s),
              flight::RegistryStatus::Unchanged);
    const flight::FaultRecord<Time>* record =
        harness.find(flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps);
    ASSERT_NE(record, nullptr);
    EXPECT_TRUE(record->active);
    EXPECT_EQ(record->occurrence_count, 1U);
    EXPECT_EQ(record->last_detected, time);
    EXPECT_EQ(harness.find(flight::FaultType::SensorInvalid, flight::FaultSource::PrimaryGps),
              nullptr);
    EXPECT_EQ(harness.find(flight::FaultType::SensorUnavailable, flight::FaultSource::PrimaryGps),
              nullptr);
}

TEST(FaultDetection, StatusChangeReplacesTheSensorFault) {
    Harness harness;
    const Time stale_at = Time{} + 1s;
    const Time invalid_at = Time{} + 2s;
    ASSERT_EQ(harness.fdir().observe_sensor(flight::FaultSource::Imu,
                                            flight::SampleUsability::Stale, stale_at),
              flight::RegistryStatus::Activated);
    EXPECT_EQ(harness.fdir().observe_sensor(flight::FaultSource::Imu,
                                            flight::SampleUsability::Invalid, invalid_at),
              flight::RegistryStatus::Activated);
    const flight::FaultRecord<Time>* stale =
        harness.find(flight::FaultType::SensorStale, flight::FaultSource::Imu);
    const flight::FaultRecord<Time>* invalid =
        harness.find(flight::FaultType::SensorInvalid, flight::FaultSource::Imu);
    ASSERT_NE(stale, nullptr);
    ASSERT_NE(invalid, nullptr);
    EXPECT_FALSE(stale->active);
    EXPECT_EQ(stale->occurrence_count, 1U);
    EXPECT_TRUE(invalid->active);
    EXPECT_EQ(invalid->first_detected, invalid_at);
}

TEST(FaultDetection, RepeatedStaleUpdatesOneRecordWithoutAnEventEachCycle) {
    Harness harness;
    const Time first = Time{} + 10s;
    ASSERT_EQ(harness.fdir().observe_sensor(flight::FaultSource::PrimaryGps,
                                            flight::SampleUsability::Stale, first),
              flight::RegistryStatus::Activated);
    EXPECT_EQ(harness.fdir().observe_sensor(flight::FaultSource::PrimaryGps,
                                            flight::SampleUsability::Stale, first + 1s),
              flight::RegistryStatus::Updated);
    EXPECT_EQ(harness.fdir().observe_sensor(flight::FaultSource::PrimaryGps,
                                            flight::SampleUsability::Stale, first + 2s),
              flight::RegistryStatus::Updated);
    const flight::FaultRecord<Time>* record =
        harness.find(flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->first_detected, first);
    EXPECT_EQ(record->last_detected, first + 2s);
    EXPECT_EQ(record->occurrence_count, 3U);
    EXPECT_EQ(record->consecutive_count, 3U);
    (void)harness.count_events();
    EXPECT_EQ(harness.activated(), 1U);
    EXPECT_EQ(harness.updated(), 1U);
    EXPECT_EQ(harness.fdir().registry().occupied_count(), 1U);
}

TEST(FaultDetection, TaskSourceIsRejectedForSensorObservation) {
    Harness harness;
    EXPECT_EQ(harness.fdir().observe_sensor(flight::FaultSource::NavigationTask,
                                            flight::SampleUsability::Stale, Time{} + 1s),
              flight::RegistryStatus::RejectedSource);
    EXPECT_EQ(harness.fdir().registry().occupied_count(), 0U);
}

TEST(FaultDetection, DeadlineMissIsTaskSpecificAndOnTimeClearsIt) {
    Harness harness;
    const Time missed_at = Time{} + 5s;
    const Time on_time = Time{} + 6s;
    EXPECT_EQ(harness.fdir().observe_deadline(flight::FaultSource::PrimaryGps,
                                              flight::DeadlineFact::Missed, missed_at),
              flight::RegistryStatus::RejectedSource);
    EXPECT_EQ(harness.fdir().observe_deadline(flight::FaultSource::NavigationTask,
                                              flight::DeadlineFact::Missed, missed_at),
              flight::RegistryStatus::Activated);
    EXPECT_EQ(harness.fdir().observe_deadline(flight::FaultSource::HealthTask,
                                              flight::DeadlineFact::Missed, missed_at),
              flight::RegistryStatus::Activated);
    const flight::FaultRecord<Time>* navigation =
        harness.find(flight::FaultType::DeadlineMiss, flight::FaultSource::NavigationTask);
    const flight::FaultRecord<Time>* health =
        harness.find(flight::FaultType::DeadlineMiss, flight::FaultSource::HealthTask);
    ASSERT_NE(navigation, nullptr);
    ASSERT_NE(health, nullptr);
    EXPECT_EQ(navigation->severity, flight::FaultSeverity::Advisory);
    EXPECT_TRUE(navigation->active);
    EXPECT_TRUE(health->active);

    EXPECT_EQ(harness.fdir().observe_deadline(flight::FaultSource::NavigationTask,
                                              flight::DeadlineFact::Unusable, on_time),
              flight::RegistryStatus::Unchanged);
    EXPECT_TRUE(
        harness.find(flight::FaultType::DeadlineMiss, flight::FaultSource::NavigationTask)->active);

    EXPECT_EQ(harness.fdir().observe_deadline(flight::FaultSource::NavigationTask,
                                              flight::DeadlineFact::OnTime, on_time),
              flight::RegistryStatus::Cleared);
    EXPECT_FALSE(
        harness.find(flight::FaultType::DeadlineMiss, flight::FaultSource::NavigationTask)->active);
    EXPECT_TRUE(health->active);
    EXPECT_EQ(navigation->occurrence_count, 1U);
    EXPECT_EQ(navigation->first_detected, missed_at);
}

TEST(FaultDetection, LowBatteryUsesHysteresisAndIgnoresUnusableSamples) {
    Harness harness;
    const Time time = Time{} + 1s;
    const hardware::Millivolts below{10999};
    const hardware::Millivolts at_activate{11000};
    const hardware::Millivolts in_band{11200};
    const hardware::Millivolts at_clear{11500};
    const hardware::Millivolts above{11501};

    EXPECT_EQ(flight::detect_low_battery(flight::SampleUsability::Unavailable, below, {}),
              flight::BatteryCommand::Hold);
    EXPECT_EQ(flight::detect_low_battery(flight::SampleUsability::Usable, at_activate, {}),
              flight::BatteryCommand::Hold);
    EXPECT_EQ(flight::detect_low_battery(flight::SampleUsability::Usable, below, {}),
              flight::BatteryCommand::Activate);
    EXPECT_EQ(flight::detect_low_battery(flight::SampleUsability::Usable, above, {}),
              flight::BatteryCommand::Clear);

    auto held = harness.fdir().observe_battery(flight::SampleUsability::Stale, below, time);
    EXPECT_EQ(held.power, flight::RegistryStatus::Unchanged);
    EXPECT_EQ(held.sensor, flight::RegistryStatus::Activated);
    EXPECT_EQ(harness.find(flight::FaultType::LowBattery, flight::FaultSource::Battery), nullptr);
    EXPECT_TRUE(harness.find(flight::FaultType::SensorStale, flight::FaultSource::Battery)->active);

    auto activated =
        harness.fdir().observe_battery(flight::SampleUsability::Usable, below, time + 1s);
    EXPECT_EQ(activated.power, flight::RegistryStatus::Activated);
    EXPECT_EQ(activated.sensor, flight::RegistryStatus::Cleared);
    const flight::FaultRecord<Time>* low =
        harness.find(flight::FaultType::LowBattery, flight::FaultSource::Battery);
    ASSERT_NE(low, nullptr);
    EXPECT_TRUE(low->active);
    EXPECT_EQ(low->severity, flight::FaultSeverity::Critical);
    EXPECT_FALSE(
        harness.find(flight::FaultType::SensorStale, flight::FaultSource::Battery)->active);

    auto band = harness.fdir().observe_battery(flight::SampleUsability::Usable, in_band, time + 2s);
    EXPECT_EQ(band.power, flight::RegistryStatus::Unchanged);
    EXPECT_TRUE(low->active);

    auto still =
        harness.fdir().observe_battery(flight::SampleUsability::Usable, at_clear, time + 3s);
    EXPECT_EQ(still.power, flight::RegistryStatus::Unchanged);
    EXPECT_TRUE(low->active);

    auto cleared =
        harness.fdir().observe_battery(flight::SampleUsability::Usable, above, time + 4s);
    EXPECT_EQ(cleared.power, flight::RegistryStatus::Cleared);
    EXPECT_FALSE(low->active);
    EXPECT_EQ(low->first_detected, time + 1s);
    EXPECT_EQ(low->occurrence_count, 1U);

    auto not_yet =
        harness.fdir().observe_battery(flight::SampleUsability::Usable, at_activate, time + 5s);
    EXPECT_EQ(not_yet.power, flight::RegistryStatus::Unchanged);
    EXPECT_FALSE(low->active);
}

TEST(FaultDetection, FutureHoldContinuesTheStaleRecord) {
    Harness harness;
    const Time first = Time{} + 1s;
    const Time held = Time{} + 2s;
    const Time second = Time{} + 3s;
    ASSERT_EQ(harness.fdir().observe_sensor(flight::FaultSource::PrimaryGps,
                                            flight::SampleUsability::Stale, first),
              flight::RegistryStatus::Activated);
    EXPECT_EQ(harness.fdir().observe_sensor(flight::FaultSource::PrimaryGps,
                                            flight::SampleUsability::Future, held),
              flight::RegistryStatus::Unchanged);
    const flight::FaultRecord<Time>* during =
        harness.find(flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps);
    ASSERT_NE(during, nullptr);
    EXPECT_TRUE(during->active);
    EXPECT_EQ(during->consecutive_count, 1U);
    EXPECT_EQ(during->occurrence_count, 1U);

    EXPECT_EQ(harness.fdir().observe_sensor(flight::FaultSource::PrimaryGps,
                                            flight::SampleUsability::Stale, second),
              flight::RegistryStatus::Updated);
    EXPECT_TRUE(during->active);
    EXPECT_EQ(during->consecutive_count, 2U);
    EXPECT_EQ(during->occurrence_count, 2U);
    EXPECT_EQ(during->last_detected, second);
    EXPECT_EQ(harness.find(flight::FaultType::SensorInvalid, flight::FaultSource::PrimaryGps),
              nullptr);
}

TEST(FaultDetection, TimeErrorHoldContinuesTheStaleRecord) {
    Harness harness;
    const Time first = Time{} + 1s;
    const Time held = Time{} + 2s;
    const Time second = Time{} + 3s;
    ASSERT_EQ(harness.fdir().observe_sensor(flight::FaultSource::PrimaryGps,
                                            flight::SampleUsability::Stale, first),
              flight::RegistryStatus::Activated);
    EXPECT_EQ(harness.fdir().observe_sensor(flight::FaultSource::PrimaryGps,
                                            flight::SampleUsability::TimeError, held),
              flight::RegistryStatus::Unchanged);
    const flight::FaultRecord<Time>* during =
        harness.find(flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps);
    ASSERT_NE(during, nullptr);
    EXPECT_TRUE(during->active);
    EXPECT_EQ(during->consecutive_count, 1U);

    EXPECT_EQ(harness.fdir().observe_sensor(flight::FaultSource::PrimaryGps,
                                            flight::SampleUsability::Stale, second),
              flight::RegistryStatus::Updated);
    EXPECT_EQ(during->consecutive_count, 2U);
    EXPECT_EQ(during->occurrence_count, 2U);
    EXPECT_TRUE(during->active);
}

TEST(FaultDetection, InvalidBreaksTheStaleRecord) {
    Harness harness;
    const Time stale_at = Time{} + 1s;
    const Time invalid_at = Time{} + 2s;
    const Time stale_again = Time{} + 3s;
    ASSERT_EQ(harness.fdir().observe_sensor(flight::FaultSource::PrimaryGps,
                                            flight::SampleUsability::Stale, stale_at),
              flight::RegistryStatus::Activated);
    EXPECT_EQ(harness.fdir().observe_sensor(flight::FaultSource::PrimaryGps,
                                            flight::SampleUsability::Invalid, invalid_at),
              flight::RegistryStatus::Activated);
    const flight::FaultRecord<Time>* stale =
        harness.find(flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps);
    const flight::FaultRecord<Time>* invalid =
        harness.find(flight::FaultType::SensorInvalid, flight::FaultSource::PrimaryGps);
    ASSERT_NE(stale, nullptr);
    ASSERT_NE(invalid, nullptr);
    EXPECT_FALSE(stale->active);
    EXPECT_EQ(stale->consecutive_count, 0U);
    EXPECT_EQ(stale->occurrence_count, 1U);
    EXPECT_TRUE(invalid->active);
    EXPECT_EQ(invalid->consecutive_count, 1U);
    EXPECT_EQ(invalid->occurrence_count, 1U);

    EXPECT_EQ(harness.fdir().observe_sensor(flight::FaultSource::PrimaryGps,
                                            flight::SampleUsability::Stale, stale_again),
              flight::RegistryStatus::Activated);
    EXPECT_TRUE(stale->active);
    EXPECT_EQ(stale->consecutive_count, 1U);
    EXPECT_EQ(stale->occurrence_count, 2U);
    EXPECT_FALSE(invalid->active);
    EXPECT_EQ(invalid->consecutive_count, 0U);
    EXPECT_EQ(invalid->occurrence_count, 1U);
}
