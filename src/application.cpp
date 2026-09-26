#include "ares/application.hpp"

#include "ares/core/bounded_log.hpp"
#include "ares/core/event_log.hpp"
#include "ares/core/logger.hpp"
#include "ares/core/task_supervisor.hpp"
#include "ares/flight/example_tasks.hpp"
#include "ares/flight/executive.hpp"
#include "ares/flight/fdir.hpp"
#include "ares/flight/freshness.hpp"
#include "ares/flight/power_manager.hpp"
#include "ares/flight/sample_limits.hpp"
#include "ares/flight/thermal_monitor.hpp"
#include "ares/launch_options.hpp"
#include "ares/recorder/flight_recorder.hpp"
#include "ares/simulation/chaos_engine.hpp"
#include "ares/simulation/scenarios.hpp"
#include "ares/simulation/sensors.hpp"
#include "ares/version.hpp"

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
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
// destroyed first and joins every worker before the tasks, sensors, logger,
// event logs, or clock are destroyed. Sensors outlive the tasks that reference them.
struct MissionRuntime {
    MissionRuntime(std::ostream& out, const LaunchOptions& options,
                   const simulation::NamedScenario& scenario)
        : logger_(out, clock_), spacecraft_(clock_),
          imu_(spacecraft_, simulation::SensorNoise{options.seed}, &chaos_),
          gps_(spacecraft_, simulation::SensorNoise{options.seed}, &chaos_),
          backup_gps_(spacecraft_, simulation::SensorNoise{options.seed}, &chaos_,
                      simulation::SensorStream::BackupGps, simulation::ChaosTarget::BackupGps),
          battery_(spacecraft_, simulation::SensorNoise{options.seed}, &chaos_),
          temperature_(spacecraft_, simulation::SensorNoise{options.seed}),
          executive_(clock_, logger_, events_), health_(logger_), selector_(gps_, backup_gps_),
          navigation_(logger_, imu_, selector_, clock_,
                      flight::NavigationAgeLimits{flight::limits::kNavigationImuMaxAge,
                                                  flight::limits::kNavigationGpsMaxAge},
                      &navigation_restart_.generation),
          comms_(logger_), power_(battery_, clock_, flight::limits::kPowerMaxAge),
          thermal_(temperature_, clock_, flight::limits::kThermalMaxAge), fdir_(events_),
          supervisor_(clock_, nullptr, &navigation_restart_) {
        spacecraft_.set_epoch_now();
        if (scenario.battery_baseline_mv > 0) {
            simulation::SimulationTruth<core::SteadyClock> truth = spacecraft_.truth();
            truth.voltage = hardware::Millivolts{scenario.battery_baseline_mv};
            spacecraft_.set_truth(truth);
        }
        scenario_name_ = scenario.name;
        seed_ = options.seed;
    }

    core::SteadyClock clock_;
    core::Logger<core::SteadyClock> logger_;
    core::EventLog<flight::SystemEvent<core::SteadyClock::time_point>> events_;
    core::BoundedLog<core::TaskCycleEvent<core::SteadyClock::time_point>, 64> cycles_;
    core::BoundedLog<core::DeadlineMissEvent<core::SteadyClock::time_point>, 32> misses_;
    simulation::ChaosEngine<core::SteadyClock> chaos_{};
    std::string scenario_name_{"nominal"};
    std::uint64_t seed_{0};
    core::SimulatedExecution navigation_execution_{};
    core::SimulatedExecution communications_execution_{};
    simulation::SpacecraftModel<core::SteadyClock> spacecraft_;
    simulation::SimulatedImu<core::SteadyClock> imu_;
    simulation::SimulatedGps<core::SteadyClock> gps_;
    simulation::SimulatedGps<core::SteadyClock> backup_gps_;
    simulation::SimulatedBatteryMonitor<core::SteadyClock> battery_;
    simulation::SimulatedTemperatureSensor<core::SteadyClock> temperature_;
    flight::FlightExecutive<core::SteadyClock> executive_;
    flight::HealthPulse<core::SteadyClock> health_;
    core::NavigationRestart navigation_restart_{};
    flight::GpsSelector<core::SteadyClock> selector_;
    flight::NavigationCadence<core::SteadyClock> navigation_;
    flight::CommBeacon<core::SteadyClock> comms_;
    flight::PowerManager<core::SteadyClock> power_;
    flight::ThermalMonitor<core::SteadyClock> thermal_;
    // The supervisor is destroyed first and joins workers before the mailbox
    // and the controller are destroyed. A pending observation is not consumed
    // after health has observed stop; it is discarded with the mailbox.
    // Workers publish observations. Only the health task mutates the registry.
    flight::FaultMailbox<core::SteadyClock> fault_mailbox_;
    flight::FdirController<core::SteadyClock> fdir_;
    recorder::FlightRecorder<> recorder_{};
    std::uint32_t recorded_generation_{0};
    core::TaskSupervisor<core::SteadyClock> supervisor_;
};

[[nodiscard]] std::optional<flight::FaultSource> source_for_task(std::string_view name) {
    using Steady = core::SteadyClock;
    if (name == flight::NavigationCadence<Steady>::name) {
        return flight::FaultSource::NavigationTask;
    }
    if (name == flight::HealthPulse<Steady>::name) {
        return flight::FaultSource::HealthTask;
    }
    if (name == flight::CommBeacon<Steady>::name) {
        return flight::FaultSource::CommunicationsTask;
    }
    return std::nullopt;
}

void on_recorded_event(const flight::SystemEvent<core::SteadyClock::time_point>* event,
                       void* context) {
    if (event == nullptr || context == nullptr) {
        return;
    }
    recorder::absorb(*static_cast<recorder::FlightRecorder<>*>(context), *event);
}

void on_chaos_edge(const simulation::ChaosEdge& edge, core::SteadyClock::time_point when,
                   void* context) {
    if (context == nullptr) {
        return;
    }
    (void)static_cast<recorder::FlightRecorder<>*>(context)->record_chaos(
        edge, when.time_since_epoch().count());
}

void write_summary(const MissionRuntime& runtime, ExitCode code) {
    using SteadyTime = core::SteadyClock::time_point;
    std::uint32_t activations = 0;
    std::uint32_t clears = 0;
    std::uint32_t successes = 0;
    std::uint32_t failures = 0;
    const std::vector<flight::SystemEvent<SteadyTime>> events = runtime.events_.snapshot();
    for (const flight::SystemEvent<SteadyTime>& event : events) {
        if (std::holds_alternative<flight::FaultActivatedEvent<SteadyTime>>(event)) {
            ++activations;
        } else if (std::holds_alternative<flight::FaultClearedEvent<SteadyTime>>(event)) {
            ++clears;
        } else if (const auto* recovery = std::get_if<flight::RecoveryEvent<SteadyTime>>(&event)) {
            if (recovery->notice == flight::RecoveryNotice::Succeeded) {
                ++successes;
            } else if (recovery->notice == flight::RecoveryNotice::Failed) {
                ++failures;
            }
        }
    }
    const char* recording = "disabled";
    if (runtime.recorder_.enabled()) {
        if (runtime.recorder_.io_error()) {
            recording = "failed";
        } else if (runtime.recorder_.overflowed()) {
            recording = "overflow";
        } else {
            recording = "written";
        }
    }
    const char* gps =
        runtime.selector_.selection() == flight::GpsSelection::Backup ? "backup" : "primary";
    std::cout << "ARES " << kVersionString << '\n'
              << "scenario: " << runtime.scenario_name_ << '\n'
              << "seed: " << runtime.seed_ << '\n'
              << "final_mode: " << flight::to_string(runtime.executive_.mode()) << '\n'
              << "navigation_generation: "
              << runtime.navigation_restart_.generation.load(std::memory_order_acquire) << '\n'
              << "active_gps: " << gps << '\n'
              << "fault_activations: " << activations << '\n'
              << "fault_clears: " << clears << '\n'
              << "recovery_successes: " << successes << '\n'
              << "recovery_failures: " << failures << '\n'
              << "recording: " << recording << '\n'
              << "exit: " << exit_code_name(code) << '\n';
}

[[nodiscard]] int finish_mission(MissionRuntime& runtime, ExitCode code) {
    ExitCode reported = code;
    if (runtime.recorder_.enabled()) {
        const core::ClockSample<core::SteadyClock::time_point> now = runtime.clock_.now();
        const std::int64_t stamp =
            now.status == core::ClockStatus::Ok ? now.time.time_since_epoch().count() : 0;
        (void)runtime.recorder_.seal(stamp, static_cast<std::uint8_t>(runtime.executive_.mode()),
                                     to_int(code));
        const bool wrote = runtime.recorder_.commit();
        if (code == ExitCode::Success &&
            (!wrote || runtime.recorder_.io_error() || runtime.recorder_.overflowed())) {
            reported = ExitCode::RecorderFailed;
        }
    }
    write_summary(runtime, reported);
    return to_int(reported);
}

void publish_navigation_observation(MissionRuntime& runtime) {
    const flight::NavigationSolution<core::SteadyClock::time_point>& solution =
        runtime.navigation_.solution();
    runtime.fault_mailbox_.publish_sensor(flight::FaultSource::Imu, solution.imu_usability);
    const core::ClockSample<core::SteadyClock::time_point> now = runtime.clock_.now();
    if (now.status != core::ClockStatus::Ok) {
        return;
    }
    runtime.fault_mailbox_.publish_sensor(
        flight::FaultSource::PrimaryGps,
        flight::evaluate_freshness(runtime.navigation_.primary_gps().status,
                                   runtime.navigation_.primary_gps().time, now.time,
                                   flight::limits::kNavigationGpsMaxAge));
    runtime.fault_mailbox_.publish_sensor(
        flight::FaultSource::BackupGps,
        flight::evaluate_freshness(runtime.navigation_.backup_gps().status,
                                   runtime.navigation_.backup_gps().time, now.time,
                                   flight::limits::kNavigationGpsMaxAge));
}

void evaluate_fdir(MissionRuntime& runtime, std::stop_token stop) {
    // Stop is observed before publish, consume, and policy. A deadline the
    // cycle hook still posts after this return is not a mode decision.
    if (stop.stop_requested()) {
        return;
    }
    runtime.fault_mailbox_.publish_sensor(flight::FaultSource::Temperature,
                                          runtime.thermal_.usability());
    runtime.fault_mailbox_.publish_battery(runtime.power_.usability(),
                                           runtime.power_.latest_observation().voltage);
    const bool primary_selected = runtime.selector_.selection() == flight::GpsSelection::Primary;
    (void)flight::run_fdir_cycle(runtime.fdir_, runtime.fault_mailbox_, runtime.executive_,
                                 runtime.clock_.now(), stop,
                                 runtime.supervisor_.navigation_generation(), primary_selected);
    if (stop.stop_requested()) {
        return;
    }
    const flight::RecoveryCommand command = runtime.fdir_.last_recovery_command();
    if (command.restart_navigation) {
        (void)runtime.supervisor_.request_navigation_restart();
    }
    if (command.failover_to_backup) {
        runtime.selector_.failover_to_backup();
        if (runtime.recorder_.enabled()) {
            const core::ClockSample<core::SteadyClock::time_point> stamped = runtime.clock_.now();
            const std::int64_t stamp = stamped.status == core::ClockStatus::Ok
                                           ? stamped.time.time_since_epoch().count()
                                           : 0;
            (void)runtime.recorder_.record_gps(1, true, stamp);
        }
    }
}

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
    if (parsed.status == ArgumentStatus::Help || parsed.status == ArgumentStatus::ListScenarios) {
        std::cout << parsed.message;
        return 0;
    }
    if (parsed.status == ArgumentStatus::Error) {
        std::cerr << parsed.message << '\n';
        return to_int(ExitCode::UsageError);
    }
    const simulation::NamedScenario* scenario = simulation::find_scenario(parsed.options.scenario);
    if (scenario == nullptr) {
        std::cerr << "unknown scenario: " << parsed.options.scenario << "\nUse --list-scenarios.\n";
        return to_int(ExitCode::UsageError);
    }

    MissionRuntime runtime(std::cout, parsed.options, *scenario);
    if (!parsed.options.record_path.empty()) {
        runtime.recorder_.set_mission(parsed.options.seed, scenario->name);
        if (!runtime.recorder_.open(parsed.options.record_path)) {
            std::cerr << "record open failed\n";
            return to_int(ExitCode::RecorderFailed);
        }
        runtime.events_.set_observer(&on_recorded_event, &runtime.recorder_);
        const core::ClockSample<core::SteadyClock::time_point> started = runtime.clock_.now();
        const std::int64_t stamp =
            started.status == core::ClockStatus::Ok ? started.time.time_since_epoch().count() : 0;
        (void)runtime.recorder_.record_mission_start(parsed.options.seed, scenario->name, stamp);
    }
    const flight::BootResult boot = runtime.executive_.boot_to_standby();
    if (boot.status != flight::TransitionStatus::Accepted ||
        boot.mode != flight::SpacecraftMode::Standby) {
        return finish_mission(runtime, ExitCode::BootFailed);
    }
    if (runtime.executive_.accept(flight::Command::StartMission) !=
        flight::CommandStatus::Accepted) {
        return finish_mission(runtime, ExitCode::BootFailed);
    }

    // The cycle hook runs after the task body. A health-task deadline is
    // published after evaluate_fdir, so that observation has one health-cycle
    // of latency. The hook does not call policy. A stop already observed by
    // the health body cannot be turned into a mode request here.
    core::TaskHooks<core::SteadyClock> hooks{
        [&runtime](const core::TaskCycleEvent<core::SteadyClock::time_point>& event) {
            (void)runtime.cycles_.push(event);
            if (const std::optional<flight::FaultSource> source =
                    source_for_task(event.id.text())) {
                const std::uint32_t generation =
                    *source == flight::FaultSource::NavigationTask
                        ? runtime.navigation_restart_.generation.load(std::memory_order_acquire)
                        : 0U;
                runtime.fault_mailbox_.publish_deadline(*source, event.deadline_missed, generation);
                if (*source == flight::FaultSource::NavigationTask && runtime.recorder_.enabled() &&
                    generation > runtime.recorded_generation_) {
                    (void)runtime.recorder_.record_generation(
                        runtime.recorded_generation_, generation,
                        event.scheduled.time_since_epoch().count());
                    runtime.recorded_generation_ = generation;
                }
            }
        },
        [&runtime](const core::DeadlineMissEvent<core::SteadyClock::time_point>& event) {
            (void)runtime.misses_.push(event);
            char message[64];
            constexpr std::string_view prefix{"deadline missed elapsed_ns="};
            char* cursor = message;
            char* const end = message + sizeof(message);
            for (const char character : prefix) {
                if (cursor == end) {
                    break;
                }
                *cursor = character;
                ++cursor;
            }
            const auto written = std::to_chars(cursor, end, event.elapsed.count());
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
                if (!stop.stop_requested()) {
                    publish_navigation_observation(runtime);
                }
                runtime.navigation_execution_.extra = runtime.chaos_.execution_delay(
                    simulation::ChaosTarget::NavigationTask, scheduled);
            },
            hooks, &runtime.navigation_execution_) != core::AddStatus::Ok) {
        return finish_mission(runtime, ExitCode::StartupFailed);
    }
    if (runtime.supervisor_.add(
            flight::HealthPulse<Steady>::name, health_timing,
            [&runtime](Steady::time_point scheduled, std::stop_token stop) {
                const core::ClockSample<Steady::time_point> now = runtime.clock_.now();
                if (now.status == core::ClockStatus::Ok) {
                    if (runtime.recorder_.enabled()) {
                        runtime.chaos_.note(runtime.logger_, now.time, &on_chaos_edge,
                                            &runtime.recorder_);
                    } else {
                        runtime.chaos_.note(runtime.logger_, now.time);
                    }
                }
                flight::run_health_cycle(runtime.power_, runtime.thermal_, runtime.health_,
                                         scheduled, stop);
                evaluate_fdir(runtime, stop);
            },
            hooks) != core::AddStatus::Ok) {
        return finish_mission(runtime, ExitCode::StartupFailed);
    }
    if (runtime.supervisor_.add(
            flight::CommBeacon<Steady>::name, comms_timing,
            [&runtime](Steady::time_point scheduled, std::stop_token stop) {
                runtime.comms_(scheduled, stop);
                runtime.communications_execution_.extra = runtime.chaos_.execution_delay(
                    simulation::ChaosTarget::CommunicationsTask, scheduled);
            },
            hooks, &runtime.communications_execution_) != core::AddStatus::Ok) {
        return finish_mission(runtime, ExitCode::StartupFailed);
    }
    const core::ClockSample<core::SteadyClock::time_point> scenario_epoch = runtime.clock_.now();
    if (scenario_epoch.status != core::ClockStatus::Ok) {
        return finish_mission(runtime, ExitCode::TimeError);
    }
    const std::span<const simulation::ChaosEvent> schedule{
        scenario->events, scenario->events == nullptr ? 0 : scenario->count};
    if (!runtime.chaos_.load(schedule, scenario_epoch.time)) {
        return finish_mission(runtime, ExitCode::StartupFailed);
    }
    if (runtime.supervisor_.start() != core::StartStatus::Ok) {
        return finish_mission(runtime, ExitCode::StartupFailed);
    }

    runtime.logger_.info("executive", "tasks started");
    const core::ClockSample<core::SteadyClock::time_point> end = runtime.clock_.now();
    if (end.status != core::ClockStatus::Ok) {
        (void)runtime.supervisor_.shutdown();
        return finish_mission(runtime, ExitCode::TimeError);
    }
    core::SteadyClock::time_point stop_at{};
    if (!core::checked_time_add(end.time, parsed.options.run_for, stop_at)) {
        (void)runtime.supervisor_.shutdown();
        return finish_mission(runtime, ExitCode::TimeError);
    }
    if (runtime.clock_.wait_until(stop_at, runtime.supervisor_.shutdown_token()) !=
        core::ClockStatus::Ok) {
        (void)runtime.supervisor_.shutdown();
        return finish_mission(runtime, ExitCode::TimeError);
    }

    const core::ShutdownReport report = runtime.supervisor_.shutdown();
    for (std::size_t slot = 0; slot < report.considered; ++slot) {
        const core::WorkerShutdown& worker = report.workers.at(slot);
        if (!worker.missed_grace) {
            continue;
        }
        char message[96];
        constexpr std::string_view prefix{"worker missed stop grace index="};
        char* cursor = message;
        char* const message_end = message + sizeof(message);
        for (const char character : prefix) {
            if (cursor == message_end) {
                break;
            }
            *cursor = character;
            ++cursor;
        }
        const auto index_text = std::to_chars(cursor, message_end, worker.index);
        if (index_text.ec != std::errc{}) {
            continue;
        }
        cursor = index_text.ptr;
        const std::string_view suffix = worker.exited ? " exited-after-grace" : " still-blocked";
        if (static_cast<std::size_t>(message_end - cursor) > suffix.size()) {
            for (const char character : suffix) {
                *cursor = character;
                ++cursor;
            }
            runtime.logger_.error("supervisor", std::string_view{message, static_cast<std::size_t>(
                                                                              cursor - message)});
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
    using SteadyTime = core::SteadyClock::time_point;
    std::uint32_t activations = 0;
    std::uint32_t clears = 0;
    std::uint32_t transitions = 0;
    for (const flight::SystemEvent<SteadyTime>& event : runtime.events_.snapshot()) {
        if (std::holds_alternative<flight::FaultActivatedEvent<SteadyTime>>(event)) {
            ++activations;
        } else if (std::holds_alternative<flight::FaultClearedEvent<SteadyTime>>(event)) {
            ++clears;
        } else if (std::holds_alternative<flight::ModeChangedEvent<SteadyTime>>(event)) {
            ++transitions;
        }
    }
    const ExitCode outcome =
        combine_exit(exit_code_for(runtime.supervisor_), runtime.misses_.overflowed());
    // completed follows the process exit. A missed stop grace is logged above and
    // does not by itself change that exit, so it does not by itself clear completed.
    const bool completed = outcome == ExitCode::Success;
    runtime.logger_.info("scenario", "name=" + runtime.scenario_name_ + " mode=" +
                                         std::string(flight::to_string(runtime.executive_.mode())) +
                                         " activations=" + std::to_string(activations) +
                                         " clears=" + std::to_string(clears) +
                                         " transitions=" + std::to_string(transitions) +
                                         " completed=" + (completed ? "1" : "0"));
    return finish_mission(runtime, outcome);
}

} // namespace ares
