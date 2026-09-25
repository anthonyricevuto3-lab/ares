#pragma once

#include "ares/simulation/chaos_engine.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ares::simulation {

// Named schedules. Parsing the name happens before workers start.
// The flight path never compares these strings.
struct NamedScenario {
    std::string_view name{};
    const ChaosEvent* events{nullptr};
    std::size_t count{0};
    // Zero leaves the model voltage alone. A positive value is the restored battery.
    std::int64_t battery_baseline_mv{0};
};

inline constexpr ChaosEvent kGpsStaleEvents[] = {
    {InjectionKind::SensorFreeze, ChaosTarget::Gps, std::chrono::seconds{5},
     std::chrono::seconds{4}, 0, 0},
};

inline constexpr ChaosEvent kGpsUnavailableEvents[] = {
    {InjectionKind::SensorUnavailable, ChaosTarget::Gps, std::chrono::seconds{5},
     std::chrono::seconds{3}, 0, 0},
};

inline constexpr ChaosEvent kImuInvalidEvents[] = {
    {InjectionKind::SensorInvalid, ChaosTarget::Imu, std::chrono::seconds{5},
     std::chrono::seconds{3}, 0, 0},
};

inline constexpr ChaosEvent kLowBatteryEvents[] = {
    {InjectionKind::BatteryVoltageOverride, ChaosTarget::Battery, std::chrono::seconds{2},
     std::chrono::seconds{2}, 10800, 0},
};

inline constexpr ChaosEvent kDeadlineStormEvents[] = {
    {InjectionKind::TaskExecutionDelay, ChaosTarget::NavigationTask, std::chrono::seconds{1},
     std::chrono::seconds{4}, 150000000, 0},
};

inline constexpr ChaosEvent kMixedFaultEvents[] = {
    {InjectionKind::SensorFreeze, ChaosTarget::Gps, std::chrono::seconds{1},
     std::chrono::seconds{9}, 0, 0},
    {InjectionKind::BatteryVoltageOverride, ChaosTarget::Battery, std::chrono::seconds{4},
     std::chrono::seconds{2}, 10800, 0},
};

// Same injection as the campaign success case: 150 ms of extra navigation time for 1 s.
inline constexpr ChaosEvent kNavigationRestartEvents[] = {
    {InjectionKind::TaskExecutionDelay, ChaosTarget::NavigationTask, std::chrono::seconds{1},
     std::chrono::seconds{1}, 150000000, 0},
};

// Same injection as the campaign failure case: the delay outlasts both restart attempts.
inline constexpr ChaosEvent kRestartFailEvents[] = {
    {InjectionKind::TaskExecutionDelay, ChaosTarget::NavigationTask, std::chrono::seconds{1},
     std::chrono::seconds{30}, 150000000, 0},
};

inline constexpr NamedScenario kScenarios[] = {
    {"nominal", nullptr, 0, 0},
    {"gps_stale", kGpsStaleEvents, 1, 0},
    {"gps_unavailable", kGpsUnavailableEvents, 1, 0},
    {"imu_invalid", kImuInvalidEvents, 1, 0},
    {"low_battery", kLowBatteryEvents, 1, 12400},
    {"deadline_storm", kDeadlineStormEvents, 1, 0},
    {"mixed_faults", kMixedFaultEvents, 2, 12400},
    {"nav_restart", kNavigationRestartEvents, 1, 0},
    {"restart_fail", kRestartFailEvents, 1, 0},
};

struct ScenarioInfo {
    std::string_view name{};
    std::string_view summary{};
};

inline constexpr ScenarioInfo kScenarioInfo[] = {
    {"nominal", "No injected fault conditions."},
    {"gps_stale", "Primary GPS freeze, then backup failover and verification."},
    {"gps_unavailable", "Primary GPS reports unavailable for a bounded window."},
    {"imu_invalid", "IMU reports invalid for a bounded window."},
    {"low_battery", "Battery voltage drops below the safe-mode threshold, then restores."},
    {"deadline_storm", "Navigation delay long enough to escalate, then the fault clears."},
    {"mixed_faults", "Overlapping GPS freeze and low battery."},
    {"nav_restart", "Demo of the existing delay injection. One restart verifies, then Standby."},
    {"restart_fail", "Demo of the existing delay injection. Both restarts fail. No third attempt."},
};

[[nodiscard]] constexpr bool scenario_catalog_matches() noexcept {
    constexpr std::size_t count = sizeof(kScenarios) / sizeof(NamedScenario);
    constexpr std::size_t described = sizeof(kScenarioInfo) / sizeof(ScenarioInfo);
    if (count != described) {
        return false;
    }
    const NamedScenario* scenario = kScenarios;
    const ScenarioInfo* info = kScenarioInfo;
    const NamedScenario* const end = kScenarios + count;
    for (; scenario != end; ++scenario, ++info) {
        if (scenario->name != info->name) {
            return false;
        }
    }
    return true;
}

static_assert(scenario_catalog_matches());

[[nodiscard]] inline std::string scenario_catalog_text() {
    std::string text;
    for (const ScenarioInfo& info : kScenarioInfo) {
        text.append(info.name);
        text.append("  ");
        text.append(info.summary);
        text.push_back('\n');
    }
    return text;
}

[[nodiscard]] inline const NamedScenario* find_scenario(std::string_view name) noexcept {
    for (const NamedScenario& scenario : kScenarios) {
        if (scenario.name == name) {
            return &scenario;
        }
    }
    return nullptr;
}

} // namespace ares::simulation
