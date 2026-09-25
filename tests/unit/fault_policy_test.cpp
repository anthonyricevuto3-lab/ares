#include "ares/core/clock.hpp"
#include "ares/flight/fault_policy.hpp"

#include <gtest/gtest.h>

namespace flight = ares::flight;
using namespace std::chrono_literals;
using Time = ares::core::ManualClock::time_point;

namespace {

void raise_warning(flight::FaultRegistry<Time, 4>& registry, std::uint32_t times) {
    for (std::uint32_t index = 0; index < times; ++index) {
        const Time time = Time{} + std::chrono::seconds{index + 1};
        (void)registry.raise(flight::FaultType::SensorStale, flight::FaultSource::Gps,
                             flight::FaultSeverity::Warning, time);
    }
}

} // namespace

TEST(FaultPolicy, AdvisoryStaysNominal) {
    flight::FaultRegistry<Time, 4> registry;
    ASSERT_EQ(registry.raise(flight::FaultType::DeadlineMiss, flight::FaultSource::NavigationTask,
                             flight::FaultSeverity::Advisory, Time{} + 1s),
              flight::RegistryStatus::Activated);
    ASSERT_EQ(registry.raise(flight::FaultType::DeadlineMiss, flight::FaultSource::NavigationTask,
                             flight::FaultSeverity::Advisory, Time{} + 2s),
              flight::RegistryStatus::Updated);
    ASSERT_EQ(registry.raise(flight::FaultType::DeadlineMiss, flight::FaultSource::NavigationTask,
                             flight::FaultSeverity::Advisory, Time{} + 3s),
              flight::RegistryStatus::Updated);

    const flight::PolicyDecision decision =
        flight::evaluate_fault_policy(registry, flight::SpacecraftMode::Nominal, {});
    EXPECT_EQ(decision.action, flight::RecoveryAction::None);
    EXPECT_FALSE(decision.requested_mode.has_value());
}

TEST(FaultPolicy, SingleWarningDoesNotLeaveNominal) {
    flight::FaultRegistry<Time, 4> registry;
    raise_warning(registry, 1);
    const flight::PolicyDecision decision =
        flight::evaluate_fault_policy(registry, flight::SpacecraftMode::Nominal, {});
    EXPECT_EQ(decision.action, flight::RecoveryAction::None);
    EXPECT_FALSE(decision.requested_mode.has_value());
    EXPECT_EQ(
        registry.find(flight::FaultType::SensorStale, flight::FaultSource::Gps)->consecutive_count,
        1U);
}

TEST(FaultPolicy, PersistentWarningRequestsDegraded) {
    flight::FaultRegistry<Time, 4> registry;
    raise_warning(registry, 2);
    const flight::PolicyDecision early =
        flight::evaluate_fault_policy(registry, flight::SpacecraftMode::Nominal, {});
    EXPECT_FALSE(early.requested_mode.has_value());

    raise_warning(registry, 1);
    const flight::PolicyDecision decision =
        flight::evaluate_fault_policy(registry, flight::SpacecraftMode::Nominal, {});
    EXPECT_EQ(decision.action, flight::RecoveryAction::ContinueDegraded);
    ASSERT_TRUE(decision.requested_mode.has_value());
    EXPECT_EQ(*decision.requested_mode, flight::SpacecraftMode::Degraded);

    const flight::PolicyDecision held =
        flight::evaluate_fault_policy(registry, flight::SpacecraftMode::Degraded, {});
    EXPECT_EQ(held.action, flight::RecoveryAction::ContinueDegraded);
    EXPECT_FALSE(held.requested_mode.has_value());
}

TEST(FaultPolicy, CriticalRequestsSafeModeFromNominalAndDegraded) {
    flight::FaultRegistry<Time, 4> registry;
    ASSERT_EQ(registry.raise(flight::FaultType::LowBattery, flight::FaultSource::Battery,
                             flight::FaultSeverity::Critical, Time{} + 1s),
              flight::RegistryStatus::Activated);

    const flight::PolicyDecision from_nominal =
        flight::evaluate_fault_policy(registry, flight::SpacecraftMode::Nominal, {});
    EXPECT_EQ(from_nominal.action, flight::RecoveryAction::EnterSafeMode);
    ASSERT_TRUE(from_nominal.requested_mode.has_value());
    EXPECT_EQ(*from_nominal.requested_mode, flight::SpacecraftMode::SafeMode);

    const flight::PolicyDecision from_degraded =
        flight::evaluate_fault_policy(registry, flight::SpacecraftMode::Degraded, {});
    ASSERT_TRUE(from_degraded.requested_mode.has_value());
    EXPECT_EQ(*from_degraded.requested_mode, flight::SpacecraftMode::SafeMode);

    const flight::PolicyDecision already =
        flight::evaluate_fault_policy(registry, flight::SpacecraftMode::SafeMode, {});
    EXPECT_EQ(already.action, flight::RecoveryAction::EnterSafeMode);
    EXPECT_FALSE(already.requested_mode.has_value());
}

TEST(FaultPolicy, RecoveryRequestsNominalOnlyFromDegraded) {
    flight::FaultRegistry<Time, 4> registry;
    raise_warning(registry, 3);
    ASSERT_EQ(registry.clear(flight::FaultType::SensorStale, flight::FaultSource::Gps),
              flight::RegistryStatus::Cleared);
    ASSERT_EQ(registry.raise(flight::FaultType::DeadlineMiss, flight::FaultSource::HealthTask,
                             flight::FaultSeverity::Advisory, Time{} + 8s),
              flight::RegistryStatus::Activated);

    const flight::PolicyDecision recovered =
        flight::evaluate_fault_policy(registry, flight::SpacecraftMode::Degraded, {});
    EXPECT_EQ(recovered.action, flight::RecoveryAction::None);
    ASSERT_TRUE(recovered.requested_mode.has_value());
    EXPECT_EQ(*recovered.requested_mode, flight::SpacecraftMode::Nominal);

    const flight::PolicyDecision safe =
        flight::evaluate_fault_policy(registry, flight::SpacecraftMode::SafeMode, {});
    EXPECT_FALSE(safe.requested_mode.has_value());
}

TEST(FaultPolicy, SaturatedRegistryRequestsSafeMode) {
    flight::FaultRegistry<Time, 1> registry;
    ASSERT_EQ(registry.raise(flight::FaultType::DeadlineMiss,
                             flight::FaultSource::CommunicationsTask,
                             flight::FaultSeverity::Advisory, Time{} + 1s),
              flight::RegistryStatus::Activated);
    EXPECT_EQ(registry.raise(flight::FaultType::LowBattery, flight::FaultSource::Battery,
                             flight::FaultSeverity::Critical, Time{} + 2s),
              flight::RegistryStatus::RejectedFull);
    EXPECT_TRUE(registry.saturated());
    EXPECT_EQ(registry.find(flight::FaultType::LowBattery, flight::FaultSource::Battery), nullptr);

    const flight::PolicyDecision decision =
        flight::evaluate_fault_policy(registry, flight::SpacecraftMode::Nominal, {});
    EXPECT_EQ(decision.action, flight::RecoveryAction::EnterSafeMode);
    ASSERT_TRUE(decision.requested_mode.has_value());
    EXPECT_EQ(*decision.requested_mode, flight::SpacecraftMode::SafeMode);
    EXPECT_FALSE(decision.transition.has_value());
}

TEST(FaultPolicy, TwoNonPersistentWarningsStayNominal) {
    flight::FaultRegistry<Time, 4> registry;
    ASSERT_EQ(registry.raise(flight::FaultType::SensorStale, flight::FaultSource::Gps,
                             flight::FaultSeverity::Warning, Time{} + 1s),
              flight::RegistryStatus::Activated);
    ASSERT_EQ(registry.raise(flight::FaultType::SensorStale, flight::FaultSource::Gps,
                             flight::FaultSeverity::Warning, Time{} + 2s),
              flight::RegistryStatus::Updated);
    ASSERT_EQ(registry.raise(flight::FaultType::SensorInvalid, flight::FaultSource::Imu,
                             flight::FaultSeverity::Warning, Time{} + 1s),
              flight::RegistryStatus::Activated);

    const flight::PolicyDecision decision =
        flight::evaluate_fault_policy(registry, flight::SpacecraftMode::Nominal, {});
    EXPECT_EQ(decision.action, flight::RecoveryAction::None);
    EXPECT_FALSE(decision.requested_mode.has_value());
}

TEST(FaultPolicy, PersistentWarningPlusAdvisoryRequestsDegraded) {
    flight::FaultRegistry<Time, 4> registry;
    raise_warning(registry, 3);
    ASSERT_EQ(registry.raise(flight::FaultType::DeadlineMiss, flight::FaultSource::NavigationTask,
                             flight::FaultSeverity::Advisory, Time{} + 4s),
              flight::RegistryStatus::Activated);

    const flight::PolicyDecision decision =
        flight::evaluate_fault_policy(registry, flight::SpacecraftMode::Nominal, {});
    EXPECT_EQ(decision.action, flight::RecoveryAction::ContinueDegraded);
    ASSERT_TRUE(decision.requested_mode.has_value());
    EXPECT_EQ(*decision.requested_mode, flight::SpacecraftMode::Degraded);
}

TEST(FaultPolicy, PersistentWarningPlusCriticalRequestsSafeMode) {
    flight::FaultRegistry<Time, 4> registry;
    raise_warning(registry, 3);
    ASSERT_EQ(registry.raise(flight::FaultType::LowBattery, flight::FaultSource::Battery,
                             flight::FaultSeverity::Critical, Time{} + 4s),
              flight::RegistryStatus::Activated);

    const flight::PolicyDecision decision =
        flight::evaluate_fault_policy(registry, flight::SpacecraftMode::Nominal, {});
    EXPECT_EQ(decision.action, flight::RecoveryAction::EnterSafeMode);
    ASSERT_TRUE(decision.requested_mode.has_value());
    EXPECT_EQ(*decision.requested_mode, flight::SpacecraftMode::SafeMode);
}

TEST(FaultPolicy, ClearingCriticalDoesNotReturnToNominal) {
    flight::FaultRegistry<Time, 4> registry;
    raise_warning(registry, 3);
    ASSERT_EQ(registry.raise(flight::FaultType::LowBattery, flight::FaultSource::Battery,
                             flight::FaultSeverity::Critical, Time{} + 4s),
              flight::RegistryStatus::Activated);
    ASSERT_EQ(registry.clear(flight::FaultType::LowBattery, flight::FaultSource::Battery),
              flight::RegistryStatus::Cleared);
    ASSERT_NE(registry.find(flight::FaultType::SensorStale, flight::FaultSource::Gps), nullptr);
    EXPECT_EQ(
        registry.find(flight::FaultType::SensorStale, flight::FaultSource::Gps)->consecutive_count,
        3U);

    const flight::PolicyDecision from_safe =
        flight::evaluate_fault_policy(registry, flight::SpacecraftMode::SafeMode, {});
    EXPECT_EQ(from_safe.action, flight::RecoveryAction::ContinueDegraded);
    EXPECT_FALSE(from_safe.requested_mode.has_value());

    const flight::PolicyDecision from_degraded =
        flight::evaluate_fault_policy(registry, flight::SpacecraftMode::Degraded, {});
    EXPECT_EQ(from_degraded.action, flight::RecoveryAction::ContinueDegraded);
    EXPECT_FALSE(from_degraded.requested_mode.has_value());
}

TEST(FaultPolicy, ClearingOneWarningLeavesTheOtherDegraded) {
    flight::FaultRegistry<Time, 4> registry;
    raise_warning(registry, 3);
    ASSERT_EQ(registry.raise(flight::FaultType::SensorStale, flight::FaultSource::Imu,
                             flight::FaultSeverity::Warning, Time{} + 4s),
              flight::RegistryStatus::Activated);
    ASSERT_EQ(registry.clear(flight::FaultType::SensorStale, flight::FaultSource::Imu),
              flight::RegistryStatus::Cleared);

    const flight::PolicyDecision decision =
        flight::evaluate_fault_policy(registry, flight::SpacecraftMode::Degraded, {});
    EXPECT_EQ(decision.action, flight::RecoveryAction::ContinueDegraded);
    EXPECT_FALSE(decision.requested_mode.has_value());
    EXPECT_TRUE(registry.find(flight::FaultType::SensorStale, flight::FaultSource::Gps)->active);
}

TEST(FaultPolicy, AdvisoryAloneAllowsReturnToNominal) {
    flight::FaultRegistry<Time, 4> registry;
    raise_warning(registry, 3);
    ASSERT_EQ(registry.raise(flight::FaultType::SensorInvalid, flight::FaultSource::Imu,
                             flight::FaultSeverity::Warning, Time{} + 4s),
              flight::RegistryStatus::Activated);
    ASSERT_EQ(registry.raise(flight::FaultType::SensorInvalid, flight::FaultSource::Imu,
                             flight::FaultSeverity::Warning, Time{} + 5s),
              flight::RegistryStatus::Updated);
    ASSERT_EQ(registry.raise(flight::FaultType::DeadlineMiss, flight::FaultSource::HealthTask,
                             flight::FaultSeverity::Advisory, Time{} + 6s),
              flight::RegistryStatus::Activated);
    ASSERT_EQ(registry.clear(flight::FaultType::SensorStale, flight::FaultSource::Gps),
              flight::RegistryStatus::Cleared);
    ASSERT_EQ(registry.clear(flight::FaultType::SensorInvalid, flight::FaultSource::Imu),
              flight::RegistryStatus::Cleared);

    const flight::PolicyDecision decision =
        flight::evaluate_fault_policy(registry, flight::SpacecraftMode::Degraded, {});
    EXPECT_EQ(decision.action, flight::RecoveryAction::None);
    ASSERT_TRUE(decision.requested_mode.has_value());
    EXPECT_EQ(*decision.requested_mode, flight::SpacecraftMode::Nominal);
    EXPECT_TRUE(
        registry.find(flight::FaultType::DeadlineMiss, flight::FaultSource::HealthTask)->active);
}

TEST(FaultPolicy, SaturationDominatesOtherFaults) {
    flight::FaultRegistry<Time, 2> registry;
    for (std::uint32_t index = 0; index < 3; ++index) {
        const Time time = Time{} + std::chrono::seconds{index + 1};
        const flight::RegistryStatus status =
            registry.raise(flight::FaultType::SensorStale, flight::FaultSource::Gps,
                           flight::FaultSeverity::Warning, time);
        ASSERT_TRUE(status == flight::RegistryStatus::Activated ||
                    status == flight::RegistryStatus::Updated);
    }
    ASSERT_EQ(registry.raise(flight::FaultType::DeadlineMiss, flight::FaultSource::NavigationTask,
                             flight::FaultSeverity::Advisory, Time{} + 4s),
              flight::RegistryStatus::Activated);
    EXPECT_EQ(registry.raise(flight::FaultType::LowBattery, flight::FaultSource::Battery,
                             flight::FaultSeverity::Critical, Time{} + 5s),
              flight::RegistryStatus::RejectedFull);
    EXPECT_TRUE(registry.saturated());

    const flight::PolicyDecision decision =
        flight::evaluate_fault_policy(registry, flight::SpacecraftMode::Nominal, {});
    EXPECT_EQ(decision.action, flight::RecoveryAction::EnterSafeMode);
    ASSERT_TRUE(decision.requested_mode.has_value());
    EXPECT_EQ(*decision.requested_mode, flight::SpacecraftMode::SafeMode);
}
