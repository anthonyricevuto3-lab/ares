#pragma once

#include "ares/flight/command.hpp"
#include "ares/flight/mode.hpp"

#include <string_view>

namespace ares::flight {

enum class TransitionRejectReason { Illegal, InvalidMode, Reentrant, BootRejected };

template <typename TimePoint> struct ModeChangedEvent {
    SpacecraftMode from{};
    SpacecraftMode to{};
    TimePoint time{};

    bool operator==(const ModeChangedEvent&) const = default;
};

template <typename TimePoint> struct ModeTransitionRejected {
    SpacecraftMode from{};
    SpacecraftMode attempted{};
    TransitionRejectReason reason{};
    TimePoint time{};

    bool operator==(const ModeTransitionRejected&) const = default;
};

template <typename TimePoint> struct CommandEvent {
    Command command{};
    bool accepted{false};
    TimePoint time{};

    bool operator==(const CommandEvent&) const = default;
};

[[nodiscard]] constexpr std::string_view to_string(TransitionRejectReason reason) noexcept {
    switch (reason) {
    case TransitionRejectReason::Illegal:
        return "illegal";
    case TransitionRejectReason::InvalidMode:
        return "invalid-mode";
    case TransitionRejectReason::Reentrant:
        return "reentrant";
    case TransitionRejectReason::BootRejected:
        return "boot-rejected";
    }
    return "invalid";
}

} // namespace ares::flight
