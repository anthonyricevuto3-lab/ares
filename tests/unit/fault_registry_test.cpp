#include "ares/core/clock.hpp"
#include "ares/flight/fault_registry.hpp"
#include "ares/flight/fdir_limits.hpp"

#include <array>

#include <gtest/gtest.h>

namespace flight = ares::flight;
using namespace std::chrono_literals;
using Time = ares::core::ManualClock::time_point;

namespace {

const flight::FaultRecord<Time>* find(const flight::FaultRegistry<Time, 4>& registry,
                                      flight::FaultType type, flight::FaultSource source) {
    return registry.find(type, source);
}

} // namespace

TEST(FaultRegistry, RepeatedDetectionUpdatesOneRecord) {
    flight::FaultRegistry<Time, 4> registry;
    const Time first = Time{} + 1s;
    const Time second = Time{} + 2s;
    const Time third = Time{} + 3s;

    EXPECT_EQ(registry.raise(flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps,
                             flight::FaultSeverity::Warning, first),
              flight::RegistryStatus::Activated);
    EXPECT_EQ(registry.raise(flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps,
                             flight::FaultSeverity::Warning, second),
              flight::RegistryStatus::Updated);
    EXPECT_EQ(registry.raise(flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps,
                             flight::FaultSeverity::Warning, third),
              flight::RegistryStatus::Updated);

    EXPECT_EQ(registry.occupied_count(), 1U);
    EXPECT_EQ(registry.active_count(), 1U);
    const flight::FaultRecord<Time>* record =
        find(registry, flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps);
    ASSERT_NE(record, nullptr);
    EXPECT_TRUE(record->active);
    EXPECT_EQ(record->first_detected, first);
    EXPECT_EQ(record->last_detected, third);
    EXPECT_EQ(record->occurrence_count, 3U);
    EXPECT_EQ(record->consecutive_count, 3U);
}

TEST(FaultRegistry, DifferentSourceIsASeparateFault) {
    flight::FaultRegistry<Time, 4> registry;
    const Time time = Time{} + 1s;
    ASSERT_EQ(registry.raise(flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps,
                             flight::FaultSeverity::Warning, time),
              flight::RegistryStatus::Activated);
    ASSERT_EQ(registry.raise(flight::FaultType::SensorStale, flight::FaultSource::Imu,
                             flight::FaultSeverity::Warning, time),
              flight::RegistryStatus::Activated);
    EXPECT_EQ(registry.active_count(), 2U);
    EXPECT_NE(registry.find(flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps),
              registry.find(flight::FaultType::SensorStale, flight::FaultSource::Imu));
}

TEST(FaultRegistry, ClearKeepsHistoryAndReactivationContinuesTheCount) {
    flight::FaultRegistry<Time, 4> registry;
    const Time first = Time{} + 1s;
    const Time second = Time{} + 2s;
    const Time third = Time{} + 4s;
    ASSERT_EQ(registry.raise(flight::FaultType::SensorInvalid, flight::FaultSource::Imu,
                             flight::FaultSeverity::Warning, first),
              flight::RegistryStatus::Activated);
    ASSERT_EQ(registry.raise(flight::FaultType::SensorInvalid, flight::FaultSource::Imu,
                             flight::FaultSeverity::Warning, second),
              flight::RegistryStatus::Updated);
    EXPECT_EQ(registry.clear(flight::FaultType::SensorInvalid, flight::FaultSource::Imu),
              flight::RegistryStatus::Cleared);
    EXPECT_EQ(registry.clear(flight::FaultType::SensorInvalid, flight::FaultSource::Imu),
              flight::RegistryStatus::Unchanged);

    const flight::FaultRecord<Time>* cleared =
        registry.find(flight::FaultType::SensorInvalid, flight::FaultSource::Imu);
    ASSERT_NE(cleared, nullptr);
    EXPECT_FALSE(cleared->active);
    EXPECT_EQ(cleared->first_detected, first);
    EXPECT_EQ(cleared->last_detected, second);
    EXPECT_EQ(cleared->occurrence_count, 2U);
    EXPECT_EQ(cleared->consecutive_count, 0U);
    EXPECT_EQ(registry.active_count(), 0U);

    EXPECT_EQ(registry.raise(flight::FaultType::SensorInvalid, flight::FaultSource::Imu,
                             flight::FaultSeverity::Warning, third),
              flight::RegistryStatus::Activated);
    const flight::FaultRecord<Time>* again =
        registry.find(flight::FaultType::SensorInvalid, flight::FaultSource::Imu);
    ASSERT_NE(again, nullptr);
    EXPECT_TRUE(again->active);
    EXPECT_EQ(again->first_detected, first);
    EXPECT_EQ(again->last_detected, third);
    EXPECT_EQ(again->occurrence_count, 3U);
    EXPECT_EQ(again->consecutive_count, 1U);
}

TEST(FaultRegistry, ActiveFaultsAreCopiedInSlotOrder) {
    flight::FaultRegistry<Time, 4> registry;
    const Time time = Time{} + 1s;
    ASSERT_EQ(registry.raise(flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps,
                             flight::FaultSeverity::Warning, time),
              flight::RegistryStatus::Activated);
    ASSERT_EQ(registry.raise(flight::FaultType::DeadlineMiss, flight::FaultSource::HealthTask,
                             flight::FaultSeverity::Advisory, time),
              flight::RegistryStatus::Activated);
    ASSERT_EQ(registry.clear(flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps),
              flight::RegistryStatus::Cleared);

    std::array<flight::FaultRecord<Time>, 4> active{};
    const std::size_t copied = registry.copy_active(active);
    EXPECT_EQ(copied, 1U);
    EXPECT_EQ(active[0].type, flight::FaultType::DeadlineMiss);
    EXPECT_EQ(active[0].source, flight::FaultSource::HealthTask);
}

TEST(FaultRegistry, FullActiveTableRejectsAndLatchesSaturation) {
    flight::FaultRegistry<Time, 2> registry;
    const Time time = Time{} + 1s;
    ASSERT_EQ(registry.raise(flight::FaultType::SensorStale, flight::FaultSource::Imu,
                             flight::FaultSeverity::Warning, time),
              flight::RegistryStatus::Activated);
    ASSERT_EQ(registry.raise(flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps,
                             flight::FaultSeverity::Warning, time),
              flight::RegistryStatus::Activated);
    EXPECT_FALSE(registry.saturated());

    EXPECT_EQ(registry.raise(flight::FaultType::LowBattery, flight::FaultSource::Battery,
                             flight::FaultSeverity::Critical, time),
              flight::RegistryStatus::RejectedFull);
    EXPECT_TRUE(registry.saturated());
    EXPECT_EQ(registry.find(flight::FaultType::LowBattery, flight::FaultSource::Battery), nullptr);
    EXPECT_NE(registry.find(flight::FaultType::SensorStale, flight::FaultSource::Imu), nullptr);
    EXPECT_NE(registry.find(flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps),
              nullptr);
    EXPECT_EQ(registry.active_count(), 2U);

    EXPECT_EQ(registry.raise(flight::FaultType::SensorInvalid, flight::FaultSource::Temperature,
                             flight::FaultSeverity::Warning, time + 1s),
              flight::RegistryStatus::RejectedFull);
    EXPECT_TRUE(registry.saturated());
    EXPECT_EQ(registry.active_count(), 2U);
}

TEST(FaultRegistry, InactiveSlotIsReclaimedDeterministically) {
    flight::FaultRegistry<Time, 1> registry;
    const Time time = Time{} + 1s;
    ASSERT_EQ(registry.raise(flight::FaultType::SensorStale, flight::FaultSource::Imu,
                             flight::FaultSeverity::Warning, time),
              flight::RegistryStatus::Activated);
    ASSERT_EQ(registry.clear(flight::FaultType::SensorStale, flight::FaultSource::Imu),
              flight::RegistryStatus::Cleared);
    EXPECT_EQ(registry.raise(flight::FaultType::SensorInvalid, flight::FaultSource::PrimaryGps,
                             flight::FaultSeverity::Warning, time + 1s),
              flight::RegistryStatus::Activated);
    EXPECT_FALSE(registry.saturated());
    EXPECT_EQ(registry.find(flight::FaultType::SensorStale, flight::FaultSource::Imu), nullptr);
    const flight::FaultRecord<Time>* replacement =
        registry.find(flight::FaultType::SensorInvalid, flight::FaultSource::PrimaryGps);
    ASSERT_NE(replacement, nullptr);
    EXPECT_TRUE(replacement->active);
    EXPECT_EQ(replacement->occurrence_count, 1U);
}

TEST(FaultRegistry, RepeatOfAnExistingFaultDoesNotNeedAFreeSlot) {
    flight::FaultRegistry<Time, 1> registry;
    const Time time = Time{} + 5s;
    ASSERT_EQ(registry.raise(flight::FaultType::DeadlineMiss, flight::FaultSource::NavigationTask,
                             flight::FaultSeverity::Advisory, time),
              flight::RegistryStatus::Activated);
    EXPECT_EQ(registry.raise(flight::FaultType::DeadlineMiss, flight::FaultSource::NavigationTask,
                             flight::FaultSeverity::Advisory, time + 1s),
              flight::RegistryStatus::Updated);
    EXPECT_FALSE(registry.saturated());
    const flight::FaultRecord<Time>* record =
        registry.find(flight::FaultType::DeadlineMiss, flight::FaultSource::NavigationTask);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->occurrence_count, 2U);
    EXPECT_EQ(record->last_detected, time + 1s);
}

TEST(FaultRegistry, ProductionCapacityAcceptsEveryLogicalIdentity) {
    constexpr std::size_t capacity = flight::limits::kFaultRegistryCapacity;
    static_assert(capacity == 19U);
    flight::FaultRegistry<Time, capacity> registry;

    struct Identity {
        flight::FaultType type;
        flight::FaultSource source;
    };
    const Identity identities[] = {
        {flight::FaultType::SensorUnavailable, flight::FaultSource::Imu},
        {flight::FaultType::SensorInvalid, flight::FaultSource::Imu},
        {flight::FaultType::SensorStale, flight::FaultSource::Imu},
        {flight::FaultType::SensorUnavailable, flight::FaultSource::PrimaryGps},
        {flight::FaultType::SensorInvalid, flight::FaultSource::PrimaryGps},
        {flight::FaultType::SensorStale, flight::FaultSource::PrimaryGps},
        {flight::FaultType::SensorUnavailable, flight::FaultSource::BackupGps},
        {flight::FaultType::SensorInvalid, flight::FaultSource::BackupGps},
        {flight::FaultType::SensorStale, flight::FaultSource::BackupGps},
        {flight::FaultType::SensorUnavailable, flight::FaultSource::Battery},
        {flight::FaultType::SensorInvalid, flight::FaultSource::Battery},
        {flight::FaultType::SensorStale, flight::FaultSource::Battery},
        {flight::FaultType::SensorUnavailable, flight::FaultSource::Temperature},
        {flight::FaultType::SensorInvalid, flight::FaultSource::Temperature},
        {flight::FaultType::SensorStale, flight::FaultSource::Temperature},
        {flight::FaultType::DeadlineMiss, flight::FaultSource::NavigationTask},
        {flight::FaultType::DeadlineMiss, flight::FaultSource::HealthTask},
        {flight::FaultType::DeadlineMiss, flight::FaultSource::CommunicationsTask},
        {flight::FaultType::LowBattery, flight::FaultSource::Battery},
    };
    static_assert(sizeof(identities) / sizeof(identities[0]) == capacity);

    for (const Identity& identity : identities) {
        EXPECT_EQ(registry.raise(identity.type, identity.source, flight::severity_of(identity.type),
                                 Time{} + 1s),
                  flight::RegistryStatus::Activated);
    }
    EXPECT_FALSE(registry.saturated());
    EXPECT_EQ(registry.occupied_count(), capacity);
    EXPECT_EQ(registry.active_count(), capacity);

    for (const Identity& identity : identities) {
        EXPECT_EQ(registry.raise(identity.type, identity.source, flight::severity_of(identity.type),
                                 Time{} + 2s),
                  flight::RegistryStatus::Updated);
        const flight::FaultRecord<Time>* record = registry.find(identity.type, identity.source);
        ASSERT_NE(record, nullptr);
        EXPECT_TRUE(record->active);
        EXPECT_EQ(record->consecutive_count, 2U);
        EXPECT_EQ(record->occurrence_count, 2U);
    }
    EXPECT_FALSE(registry.saturated());
    EXPECT_EQ(registry.occupied_count(), capacity);
    EXPECT_EQ(registry.raise(flight::FaultType::LowBattery, flight::FaultSource::Battery,
                             flight::FaultSeverity::Critical, Time{} + 3s),
              flight::RegistryStatus::Updated);

    ASSERT_EQ(registry.clear(identities[0].type, identities[0].source),
              flight::RegistryStatus::Cleared);
    EXPECT_EQ(registry.active_count(), capacity - 1U);
    EXPECT_EQ(registry.occupied_count(), capacity);
    EXPECT_EQ(registry.raise(identities[0].type, identities[0].source,
                             flight::severity_of(identities[0].type), Time{} + 4s),
              flight::RegistryStatus::Activated);
    const flight::FaultRecord<Time>* reactivated =
        registry.find(identities[0].type, identities[0].source);
    ASSERT_NE(reactivated, nullptr);
    EXPECT_TRUE(reactivated->active);
    EXPECT_EQ(reactivated->consecutive_count, 1U);
    EXPECT_EQ(reactivated->occurrence_count, 3U);
    EXPECT_EQ(registry.active_count(), capacity);
    EXPECT_EQ(registry.occupied_count(), capacity);
    EXPECT_FALSE(registry.saturated());
}
