#pragma once

#include "ares/core/task_supervisor.hpp"

#include <cstdint>
#include <optional>

namespace ares {

// Process status for ares::run. Worker faults stay contained; they become a
// code here and are not rethrown out of the worker or out of run.
enum class ExitCode : int {
    Success = 0,
    BootFailed = 1,
    UsageError = 2,
    WorkerException = 3,
    ScheduleFault = 4,
    HookFault = 5,
    UnknownWorkerFault = 6,
    StartupFailed = 7,
    TimeError = 8,
    // Deadline-miss history overwrote an older miss. Cycle-log overflow is not this.
    FaultHistoryOverflow = 9,
};

// Test injection for ares::run. Not a command-line flag. None is the flight path.
enum class InjectedFault : std::uint8_t { None, WorkerThrows, ScheduleOverflow };

[[nodiscard]] constexpr int to_int(ExitCode code) noexcept {
    return static_cast<int>(code);
}

// Exception outranks an unknown fault, then a schedule fault, then a hook fault.
template <core::Clock C>
[[nodiscard]] ExitCode exit_code_for(const core::TaskSupervisor<C>& supervisor) {
    bool exception = false;
    bool unknown = false;
    bool schedule = false;
    bool hook = false;
    for (std::size_t index = 0; index < supervisor.worker_count(); ++index) {
        const std::optional<core::WorkerFault> fault = supervisor.fault(index);
        if (!fault.has_value()) {
            continue;
        }
        switch (*fault) {
        case core::WorkerFault::Exception:
            exception = true;
            break;
        case core::WorkerFault::Unknown:
            unknown = true;
            break;
        case core::WorkerFault::Schedule:
            schedule = true;
            break;
        case core::WorkerFault::Hook:
            hook = true;
            break;
        case core::WorkerFault::None:
            break;
        }
    }
    if (exception) {
        return ExitCode::WorkerException;
    }
    if (unknown) {
        return ExitCode::UnknownWorkerFault;
    }
    if (schedule) {
        return ExitCode::ScheduleFault;
    }
    if (hook) {
        return ExitCode::HookFault;
    }
    return ExitCode::Success;
}

// Worker faults outrank a full deadline-miss log. Cycle-log overflow stays success.
[[nodiscard]] constexpr ExitCode combine_exit(ExitCode faults,
                                              bool miss_history_overflow) noexcept {
    if (faults != ExitCode::Success) {
        return faults;
    }
    if (miss_history_overflow) {
        return ExitCode::FaultHistoryOverflow;
    }
    return ExitCode::Success;
}

[[nodiscard]] int run(int argc, char** argv, InjectedFault fault = InjectedFault::None);

} // namespace ares
