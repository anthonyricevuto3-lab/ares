#pragma once

#include "ares/core/clock.hpp"
#include "ares/core/task_events.hpp"
#include "ares/core/task_id.hpp"

#include <cstdint>
#include <functional>
#include <stop_token>

namespace ares::core {

struct TaskTiming {
    Duration period{Duration::zero()};
    Duration deadline{Duration::zero()};
};

[[nodiscard]] bool valid_task_timing(TaskTiming timing) noexcept;

enum class PollResult : std::uint8_t { Waiting, Ran, Reentrant, ScheduleError, HookError };
enum class ArmStatus : std::uint8_t { Armed, AlreadyArmed };

template <Clock C> struct TaskHooks {
    using time_point = typename C::time_point;
    std::function<void(const TaskCycleEvent<time_point>&)> on_cycle{};
    std::function<void(const DeadlineMissEvent<time_point>&)> on_deadline_miss{};
};

template <Clock C>
using TaskWork = std::function<void(typename C::time_point scheduled, std::stop_token stop)>;

// Same-thread note. The worker sets extra before returning, and poll adds it to
// the completion timestamp. A null note, or a zero extra, leaves completion equal
// to the clock sample. This is how a scenario consumes simulated execution time
// without sleeping and without changing an uninjected cycle.
struct SimulatedExecution {
    Duration extra{Duration::zero()};
};

template <Clock C> struct DeadlineRecord {
    using time_point = typename C::time_point;
    bool recorded{false};
    bool missed{false};
    bool failed{false};
    bool unusable{false};
    std::uint64_t releases_skipped{0};
    time_point scheduled{};
    time_point completed{};
};

// One periodic release schedule. Not thread-safe: only its worker calls poll().
// A nested poll() returns Reentrant. Deadline state is stored before hooks run.
template <Clock C> class PeriodicTask {
public:
    using time_point = typename C::time_point;

    PeriodicTask(TaskId id, TaskTiming timing, TaskWork<C> work, C& clock, TaskHooks<C> hooks);

    PeriodicTask(const PeriodicTask&) = delete;
    PeriodicTask& operator=(const PeriodicTask&) = delete;
    PeriodicTask(PeriodicTask&&) = delete;
    PeriodicTask& operator=(PeriodicTask&&) = delete;

    [[nodiscard]] ArmStatus arm(time_point first_release);
    void disarm() noexcept;
    // Disarm and drop the cycle record and any pending simulated extra.
    // The worker calls this only after poll has returned.
    void prepare_restart() noexcept;
    [[nodiscard]] PollResult poll(std::stop_token stop = {});
    void bind_simulated_execution(SimulatedExecution* note) noexcept { note_ = note; }
    [[nodiscard]] time_point next_release() const noexcept;
    [[nodiscard]] TaskId id() const noexcept;
    [[nodiscard]] DeadlineRecord<C> deadline_record() const noexcept;

private:
    struct ReleaseUpdate {
        bool representable{false};
        time_point next{};
        std::uint64_t releases_skipped{0};
    };

    [[nodiscard]] ReleaseUpdate advance_release(time_point scheduled, time_point completed) const;
    [[nodiscard]] PollResult finish_cycle(time_point scheduled, time_point completed, bool failed);
    struct SimulatedCompletion {
        time_point time{};
        bool unrepresentable{false};
    };

    [[nodiscard]] SimulatedCompletion apply_simulated_execution(time_point completed) noexcept;

    TaskId id_{};
    Duration period_{};
    Duration deadline_{};
    TaskWork<C> work_{};
    C& clock_;
    TaskHooks<C> hooks_{};
    time_point next_release_{time_point::max()};
    bool armed_{false};
    bool in_poll_{false};
    DeadlineRecord<C> record_{};
    SimulatedExecution* note_{nullptr};
};

extern template class PeriodicTask<ManualClock>;
extern template class PeriodicTask<SteadyClock>;

} // namespace ares::core
