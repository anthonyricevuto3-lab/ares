#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace ares::flight {

// Closed set for this milestone. Future and TimeError are not members: they are
// not sensor-status faults, and they are not rewritten as SensorStale.
enum class FaultType : std::uint8_t {
    SensorUnavailable,
    SensorInvalid,
    SensorStale,
    DeadlineMiss,
    LowBattery,
};

enum class FaultSeverity : std::uint8_t { Advisory, Warning, Critical };

// Primary identity. Comparisons are enumerators, not names.
enum class FaultSource : std::uint8_t {
    Imu,
    PrimaryGps,
    BackupGps,
    Battery,
    Temperature,
    NavigationTask,
    HealthTask,
    CommunicationsTask,
};

// One logical fault is (type, source). consecutive_count is detections since the
// last clear and is what persistence reads. occurrence_count is the lifetime
// total and is not reset when the fault goes inactive.
template <typename TimePoint> struct FaultRecord {
    FaultType type{};
    FaultSource source{};
    FaultSeverity severity{};
    TimePoint first_detected{};
    TimePoint last_detected{};
    std::uint32_t occurrence_count{0};
    std::uint32_t consecutive_count{0};
    bool active{false};

    bool operator==(const FaultRecord&) const = default;
};

inline constexpr std::array<FaultType, 3> kSensorHealthFaults{
    FaultType::SensorUnavailable,
    FaultType::SensorInvalid,
    FaultType::SensorStale,
};

[[nodiscard]] constexpr bool is_sensor_health(FaultType type) noexcept {
    switch (type) {
    case FaultType::SensorUnavailable:
    case FaultType::SensorInvalid:
    case FaultType::SensorStale:
        return true;
    case FaultType::DeadlineMiss:
    case FaultType::LowBattery:
        return false;
    }
    return false;
}

[[nodiscard]] constexpr bool is_sensor_source(FaultSource source) noexcept {
    switch (source) {
    case FaultSource::Imu:
    case FaultSource::PrimaryGps:
    case FaultSource::BackupGps:
    case FaultSource::Battery:
    case FaultSource::Temperature:
        return true;
    case FaultSource::NavigationTask:
    case FaultSource::HealthTask:
    case FaultSource::CommunicationsTask:
        return false;
    }
    return false;
}

[[nodiscard]] constexpr bool is_task_source(FaultSource source) noexcept {
    switch (source) {
    case FaultSource::NavigationTask:
    case FaultSource::HealthTask:
    case FaultSource::CommunicationsTask:
        return true;
    case FaultSource::Imu:
    case FaultSource::PrimaryGps:
    case FaultSource::BackupGps:
    case FaultSource::Battery:
    case FaultSource::Temperature:
        return false;
    }
    return false;
}

[[nodiscard]] constexpr FaultSeverity severity_of(FaultType type) noexcept {
    switch (type) {
    case FaultType::SensorUnavailable:
    case FaultType::SensorInvalid:
    case FaultType::SensorStale:
        return FaultSeverity::Warning;
    case FaultType::DeadlineMiss:
        // Base severity only. An active streak may raise the record above this.
        return FaultSeverity::Advisory;
    case FaultType::LowBattery:
        return FaultSeverity::Critical;
    }
    return FaultSeverity::Critical;
}

[[nodiscard]] constexpr std::string_view to_string(FaultType type) noexcept {
    switch (type) {
    case FaultType::SensorUnavailable:
        return "sensor-unavailable";
    case FaultType::SensorInvalid:
        return "sensor-invalid";
    case FaultType::SensorStale:
        return "sensor-stale";
    case FaultType::DeadlineMiss:
        return "deadline-miss";
    case FaultType::LowBattery:
        return "low-battery";
    }
    return "invalid";
}

[[nodiscard]] constexpr std::string_view to_string(FaultSeverity severity) noexcept {
    switch (severity) {
    case FaultSeverity::Advisory:
        return "advisory";
    case FaultSeverity::Warning:
        return "warning";
    case FaultSeverity::Critical:
        return "critical";
    }
    return "invalid";
}

[[nodiscard]] constexpr std::string_view to_string(FaultSource source) noexcept {
    switch (source) {
    case FaultSource::Imu:
        return "imu";
    case FaultSource::PrimaryGps:
        return "primary-gps";
    case FaultSource::BackupGps:
        return "backup-gps";
    case FaultSource::Battery:
        return "battery";
    case FaultSource::Temperature:
        return "temperature";
    case FaultSource::NavigationTask:
        return "navigation-task";
    case FaultSource::HealthTask:
        return "health-task";
    case FaultSource::CommunicationsTask:
        return "communications-task";
    }
    return "invalid";
}

} // namespace ares::flight
