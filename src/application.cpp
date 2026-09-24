#include "ares/application.hpp"

#include "ares/core/bounded_log.hpp"
#include "ares/core/event_log.hpp"
#include "ares/core/logger.hpp"
#include "ares/core/task_supervisor.hpp"
#include "ares/flight/example_tasks.hpp"
#include "ares/flight/executive.hpp"
#include "ares/launch_options.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <iostream>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace ares {
namespace {

constexpr core::TaskTiming navigation_timing{std::chrono::milliseconds{100},
                                             std::chrono::milliseconds{100}};
constexpr core::TaskTiming health_timing{std::chrono::milliseconds{200},
                                         std::chrono::milliseconds{200}};
constexpr core::TaskTiming comms_timing{std::chrono::milliseconds{400},
                                        std::chrono::milliseconds{400}};

// Member order is the outlives contract. The supervisor is last, so it is
// destroyed first and joins every worker before the tasks, logger, event logs,
// or clock are destroyed.
struct MissionRuntime {
    explicit MissionRuntime(std::ostream& out)
        : logger_(out, clock_), executive_(clock_, logger_, events_), health_(logger_),
          navigation_(logger_), comms_(logger_), supervisor_(clock_) {}

    core::SteadyClock clock_;
    core::Logger<core::SteadyClock> logger_;
    core::EventLog<flight::SystemEvent<core::SteadyClock::time_point>> events_;
    core::BoundedLog<core::TaskCycleEvent<core::SteadyClock::time_point>, 64> cycles_;
    core::BoundedLog<core::DeadlineMissEvent<core::SteadyClock::time_point>, 32> misses_;
    flight::FlightExecutive<core::SteadyClock> executive_;
    flight::HealthPulse<core::SteadyClock> health_;
    flight::NavigationCadence<core::SteadyClock> navigation_;
    flight::CommBeacon<core::SteadyClock> comms_;
    core::TaskSupervisor<core::SteadyClock> supervisor_;
};

} // namespace

int run(int argc, char** argv, InjectedFault fault) {
    std::vector<std::string_view> arguments;
    if (argc > 0 && argv != nullptr) {
        arguments.reserve(static_cast<std::size_t>(argc));
        for (int index = 0; index < argc; ++index) {
            arguments.emplace_back(argv[index] != nullptr ? argv[index] : "");
        }
    }

    const ArgumentParse parsed =
        parse_arguments(std::span<const std::string_view>(arguments.data(), arguments.size()));
    if (parsed.status == ArgumentStatus::Help) {
        std::cout << parsed.message;
        return 0;
    }
    if (parsed.status == ArgumentStatus::Error) {
        std::cerr << parsed.message << '\n';
        return to_int(ExitCode::UsageError);
    }

    MissionRuntime runtime(std::cout);
    const flight::BootResult boot = runtime.executive_.boot_to_standby();
    if (boot.status != flight::TransitionStatus::Accepted ||
        boot.mode != flight::SpacecraftMode::Standby) {
        return to_int(ExitCode::BootFailed);
    }
    if (runtime.executive_.accept(flight::Command::StartMission) !=
        flight::CommandStatus::Accepted) {
        return to_int(ExitCode::BootFailed);
    }

    core::TaskHooks<core::SteadyClock> hooks{
        [&runtime](const core::TaskCycleEvent<core::SteadyClock::time_point>& event) {
            (void)runtime.cycles_.push(event);
        },
        [&runtime](const core::DeadlineMissEvent<core::SteadyClock::time_point>& event) {
            (void)runtime.misses_.push(event);
            char message[64];
            constexpr std::string_view prefix{"deadline missed elapsed_ns="};
            std::size_t used = 0;
            for (const char character : prefix) {
                message[used] = character;
                ++used;
            }
            const auto written =
                std::to_chars(message + used, message + sizeof(message), event.elapsed.count());
            if (written.ec == std::errc{}) {
                runtime.logger_.warn(
                    event.id.text(),
                    std::string_view{message, static_cast<std::size_t>(written.ptr - message)});
            } else {
                runtime.logger_.warn(event.id.text(), "deadline missed");
            }
        },
    };

    using Steady = core::SteadyClock;
    const core::TaskTiming navigation =
        fault == InjectedFault::ScheduleOverflow
            ? core::TaskTiming{core::Duration::max(), core::Duration::max()}
            : navigation_timing;
    if (runtime.supervisor_.add(
            flight::NavigationCadence<Steady>::name, navigation,
            [&runtime, fault](Steady::time_point scheduled, std::stop_token stop) {
                if (fault == InjectedFault::WorkerThrows) {
                    throw std::runtime_error("injected worker fault");
                }
                runtime.navigation_(scheduled, stop);
            },
            hooks) != core::AddStatus::Ok) {
        return to_int(ExitCode::StartupFailed);
    }
    if (runtime.supervisor_.add(
            flight::HealthPulse<Steady>::name, health_timing,
            [&runtime](Steady::time_point scheduled, std::stop_token stop) {
                runtime.health_(scheduled, stop);
            },
            hooks) != core::AddStatus::Ok) {
        return to_int(ExitCode::StartupFailed);
    }
    if (runtime.supervisor_.add(
            flight::CommBeacon<Steady>::name, comms_timing,
            [&runtime](Steady::time_point scheduled, std::stop_token stop) {
                runtime.comms_(scheduled, stop);
            },
            hooks) != core::AddStatus::Ok) {
        return to_int(ExitCode::StartupFailed);
    }
    if (runtime.supervisor_.start() != core::StartStatus::Ok) {
        return to_int(ExitCode::StartupFailed);
    }

    runtime.logger_.info("executive", "tasks started");
    const core::ClockSample<core::SteadyClock::time_point> end = runtime.clock_.now();
    if (end.status != core::ClockStatus::Ok) {
        (void)runtime.supervisor_.shutdown();
        return to_int(ExitCode::TimeError);
    }
    core::SteadyClock::time_point stop_at{};
    if (!core::checked_time_add(end.time, parsed.options.run_for, stop_at)) {
        (void)runtime.supervisor_.shutdown();
        return to_int(ExitCode::TimeError);
    }
    if (runtime.clock_.wait_until(stop_at, runtime.supervisor_.shutdown_token()) !=
        core::ClockStatus::Ok) {
        (void)runtime.supervisor_.shutdown();
        return to_int(ExitCode::TimeError);
    }

    const core::ShutdownReport report = runtime.supervisor_.shutdown();
    for (std::size_t slot = 0; slot < report.considered; ++slot) {
        const core::WorkerShutdown& worker = report.workers[slot];
        if (!worker.missed_grace) {
            continue;
        }
        char message[96];
        constexpr std::string_view prefix{"worker missed stop grace index="};
        std::size_t used = 0;
        for (const char character : prefix) {
            message[used] = character;
            ++used;
        }
        const auto index_text =
            std::to_chars(message + used, message + sizeof(message), worker.index);
        if (index_text.ec != std::errc{}) {
            continue;
        }
        used = static_cast<std::size_t>(index_text.ptr - message);
        const std::string_view suffix = worker.exited ? " exited-after-grace" : " still-blocked";
        if (used + suffix.size() < sizeof(message)) {
            for (const char character : suffix) {
                message[used] = character;
                ++used;
            }
            runtime.logger_.error("supervisor", std::string_view{message, used});
        }
    }

    std::array<core::DeadlineMissEvent<core::SteadyClock::time_point>, 32> retained{};
    const std::size_t miss_count = runtime.misses_.copy_into(retained);
    for (std::size_t index = 0; index < runtime.supervisor_.worker_count(); ++index) {
        const std::optional<core::DeadlineRecord<core::SteadyClock>> record =
            runtime.supervisor_.deadline_record(index);
        if (!record.has_value() || !record->recorded || !record->missed) {
            continue;
        }
        runtime.logger_.warn("task", "retained deadline miss");
    }
    if (runtime.cycles_.overflowed()) {
        runtime.logger_.warn("executive", "cycle log overwrote older records");
    }
    runtime.logger_.info(
        "executive",
        "shutdown complete navigation=" + std::to_string(runtime.navigation_.cycles()) +
            " health=" + std::to_string(runtime.health_.cycles()) + " comms=" +
            std::to_string(runtime.comms_.cycles()) + " misses=" + std::to_string(miss_count) +
            " miss_overwrites=" + std::to_string(runtime.misses_.overwrite_count()) +
            " cycle_overwrites=" + std::to_string(runtime.cycles_.overwrite_count()) +
            " missed_grace=" + std::to_string(report.missed_grace));
    return to_int(combine_exit(exit_code_for(runtime.supervisor_), runtime.misses_.overflowed()));
}

} // namespace ares
