#pragma once

#include "ares/simulation/chaos_engine.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
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

inline constexpr NamedScenario kScenarios[] = {
    {"nominal", nullptr, 0, 0},
    {"gps_stale", kGpsStaleEvents, 1, 0},
    {"gps_unavailable", kGpsUnavailableEvents, 1, 0},
    {"imu_invalid", kImuInvalidEvents, 1, 0},
    {"low_battery", kLowBatteryEvents, 1, 12400},
    {"deadline_storm", kDeadlineStormEvents, 1, 0},
    {"mixed_faults", kMixedFaultEvents, 2, 12400},
};

[[nodiscard]] inline const NamedScenario* find_scenario(std::string_view name) noexcept {
    for (const NamedScenario& scenario : kScenarios) {
        if (scenario.name == name) {
            return &scenario;
        }
    }
    return nullptr;
}

} // namespace ares::simulation
