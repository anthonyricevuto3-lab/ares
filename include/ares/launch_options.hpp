#pragma once

#include "ares/core/time.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace ares {

struct LaunchOptions {
    core::Duration run_for{std::chrono::milliseconds{250}};
};

enum class ArgumentStatus { Ok, Help, Error };

struct ArgumentParse {
    ArgumentStatus status{ArgumentStatus::Ok};
    LaunchOptions options{};
    std::string message{};
};

[[nodiscard]] constexpr std::string_view launch_help() noexcept {
    return "ARES v0.1\n"
           "Usage: ares [--duration-ms N]\n"
           "\n"
           "Boot into Standby, accept StartMission, run three periodic tasks, and shut down.\n"
           "\n"
           "--duration-ms N   Keep tasks running for N milliseconds (default 250).\n"
           "--help            Show this help.\n";
}

[[nodiscard]] inline ArgumentParse parse_arguments(std::span<const std::string_view> args) {
    ArgumentParse parsed;
    auto cursor = args.begin();
    if (cursor != args.end() && !cursor->starts_with('-')) {
        ++cursor;
    }

    bool saw_duration = false;
    while (cursor != args.end()) {
        const std::string_view arg = *cursor;
        if (arg == "--help" || arg == "-h") {
            parsed.status = ArgumentStatus::Help;
            parsed.message = std::string(launch_help());
            return parsed;
        }
        if (arg == "--duration-ms") {
            if (saw_duration) {
                parsed.status = ArgumentStatus::Error;
                parsed.message = "duplicate --duration-ms";
                return parsed;
            }
            ++cursor;
            if (cursor == args.end()) {
                parsed.status = ArgumentStatus::Error;
                parsed.message = "missing value for --duration-ms";
                return parsed;
            }
            const std::string_view value = *cursor;
            std::int64_t milliseconds = 0;
            const char* const first = value.data();
            const char* const last = value.data() + value.size();
            const std::from_chars_result result = std::from_chars(first, last, milliseconds);
            if (result.ec != std::errc{} || result.ptr != last || milliseconds < 0) {
                parsed.status = ArgumentStatus::Error;
                parsed.message = "invalid --duration-ms value";
                return parsed;
            }
            const auto max_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(core::Duration::max())
                    .count();
            if (milliseconds > max_ms) {
                parsed.status = ArgumentStatus::Error;
                parsed.message = "invalid --duration-ms value";
                return parsed;
            }
            parsed.options.run_for = std::chrono::milliseconds{milliseconds};
            saw_duration = true;
            ++cursor;
            continue;
        }
        parsed.status = ArgumentStatus::Error;
        parsed.message = "unknown argument: " + std::string(arg);
        return parsed;
    }
    return parsed;
}

} // namespace ares
