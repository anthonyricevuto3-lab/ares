#pragma once

#include <string_view>

namespace ares::flight {

enum class Command { StartMission };

enum class CommandStatus { Accepted, Rejected };

[[nodiscard]] constexpr std::string_view to_string(Command command) noexcept {
    switch (command) {
    case Command::StartMission:
        return "StartMission";
    }
    return "invalid";
}

} // namespace ares::flight
