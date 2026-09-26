#pragma once

#include "ares/core/time.hpp"
#include "ares/simulation/scenarios.hpp"
#include "ares/version.hpp"

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
    std::string scenario{"nominal"};
    std::uint64_t seed{0};
    std::string record_path{};
};

enum class ArgumentStatus { Ok, Help, ListScenarios, Error };

struct ArgumentParse {
    ArgumentStatus status{ArgumentStatus::Ok};
    LaunchOptions options{};
    std::string message{};
};

[[nodiscard]] inline std::string launch_help() {
    return std::string("ARES v") + kVersionString +
           "\n"
           "Usage: ares [--duration-ms N] [--scenario NAME] [--seed N] [--record FILE]\n"
           "       ares --list-scenarios\n"
           "\n"
           "Boot into Standby, accept StartMission, run three periodic tasks, and shut down.\n"
           "The default scenario is nominal: no injected fault conditions.\n"
           "\n"
           "--duration-ms N     Run tasks for N milliseconds (default 250).\n"
           "--scenario NAME     Named chaos schedule (default nominal).\n"
           "--list-scenarios    Print scenario names and one-line descriptions.\n"
           "--seed N            Mission noise seed (default 0). Named scenarios use zero noise.\n"
           "--record FILE       Write a binary mission recording. Omit to record nothing.\n"
           "--help              Show this help.\n";
}

[[nodiscard]] inline ArgumentParse parse_arguments(std::span<const std::string_view> args) {
    ArgumentParse parsed;
    auto cursor = args.begin();
    if (cursor != args.end() && !cursor->starts_with('-')) {
        ++cursor;
    }

    bool saw_duration = false;
    bool saw_scenario = false;
    bool saw_seed = false;
    bool saw_record = false;
    while (cursor != args.end()) {
        const std::string_view arg = *cursor;
        if (arg == "--help" || arg == "-h") {
            parsed.status = ArgumentStatus::Help;
            parsed.message = launch_help();
            return parsed;
        }
        if (arg == "--list-scenarios") {
            parsed.status = ArgumentStatus::ListScenarios;
            parsed.message = simulation::scenario_catalog_text();
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
        if (arg == "--scenario") {
            if (saw_scenario) {
                parsed.status = ArgumentStatus::Error;
                parsed.message = "duplicate --scenario";
                return parsed;
            }
            ++cursor;
            if (cursor == args.end() || cursor->empty()) {
                parsed.status = ArgumentStatus::Error;
                parsed.message = "missing value for --scenario";
                return parsed;
            }
            parsed.options.scenario = std::string(*cursor);
            saw_scenario = true;
            ++cursor;
            continue;
        }
        if (arg == "--seed") {
            if (saw_seed) {
                parsed.status = ArgumentStatus::Error;
                parsed.message = "duplicate --seed";
                return parsed;
            }
            ++cursor;
            if (cursor == args.end()) {
                parsed.status = ArgumentStatus::Error;
                parsed.message = "missing value for --seed";
                return parsed;
            }
            const std::string_view value = *cursor;
            std::uint64_t seed = 0;
            const char* const first = value.data();
            const char* const last = value.data() + value.size();
            const std::from_chars_result result = std::from_chars(first, last, seed);
            if (result.ec != std::errc{} || result.ptr != last) {
                parsed.status = ArgumentStatus::Error;
                parsed.message = "invalid --seed value";
                return parsed;
            }
            parsed.options.seed = seed;
            saw_seed = true;
            ++cursor;
            continue;
        }
        if (arg == "--record") {
            if (saw_record) {
                parsed.status = ArgumentStatus::Error;
                parsed.message = "duplicate --record";
                return parsed;
            }
            ++cursor;
            if (cursor == args.end() || cursor->empty()) {
                parsed.status = ArgumentStatus::Error;
                parsed.message = "missing value for --record";
                return parsed;
            }
            parsed.options.record_path = std::string(*cursor);
            saw_record = true;
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
