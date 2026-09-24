#pragma once

#include <string_view>

namespace ares::flight {

enum class SpacecraftMode {
    Boot,
    Initialization,
    Standby,
    Nominal,
    Degraded,
    SafeMode,
    Emergency,
};

[[nodiscard]] constexpr std::string_view to_string(SpacecraftMode mode) noexcept {
    switch (mode) {
    case SpacecraftMode::Boot:
        return "Boot";
    case SpacecraftMode::Initialization:
        return "Initialization";
    case SpacecraftMode::Standby:
        return "Standby";
    case SpacecraftMode::Nominal:
        return "Nominal";
    case SpacecraftMode::Degraded:
        return "Degraded";
    case SpacecraftMode::SafeMode:
        return "SafeMode";
    case SpacecraftMode::Emergency:
        return "Emergency";
    }
    return "invalid";
}

// False for a value that is not one of the enumerators, including a corrupt cast.
[[nodiscard]] constexpr bool is_known_mode(SpacecraftMode mode) noexcept {
    switch (mode) {
    case SpacecraftMode::Boot:
    case SpacecraftMode::Initialization:
    case SpacecraftMode::Standby:
    case SpacecraftMode::Nominal:
    case SpacecraftMode::Degraded:
    case SpacecraftMode::SafeMode:
    case SpacecraftMode::Emergency:
        return true;
    }
    return false;
}

} // namespace ares::flight
