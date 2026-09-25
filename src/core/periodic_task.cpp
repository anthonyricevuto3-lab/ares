#include "ares/core/periodic_task.hpp"

#include "ares/core/deadline_monitor.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ares::core {
namespace {

class FlagGuard {
public:
    explicit FlagGuard(bool& flag) : flag_(flag) { flag_ = true; }
    ~FlagGuard() { flag_ = false; }

    FlagGuard(const FlagGuard&) = delete;
    FlagGuard& operator=(const FlagGuard&) = delete;

private:
    bool& flag_;
};

} // namespace

bool valid_task_timing(TaskTiming timing) noexcept {
    if (timing.period <= Duration::zero() || timing.deadline <= Duration::zero()) {
        return false;
    }
    return timing.deadline <= timing.period;
}

template <Clock C>
PeriodicTask<C>::PeriodicTask(TaskId id, TaskTiming timing, TaskWork<C> work, C& clock,
                              TaskHooks<C> hooks)
    : id_(id), period_(timing.period), deadline_(timing.deadline), work_(std::move(work)),
      clock_(clock), hooks_(std::move(hooks)) {
    if (!valid_task_timing(TaskTiming{period_, deadline_}) || !work_) {
        throw std::invalid_argument("periodic task configuration is invalid");
    }
}

template <Clock C> ArmStatus PeriodicTask<C>::arm(time_point first_release) {
    if (armed_) {
        return ArmStatus::AlreadyArmed;
    }
    next_release_ = first_release;
    armed_ = true;
    return ArmStatus::Armed;
}

template <Clock C> void PeriodicTask<C>::disarm() noexcept {
    armed_ = false;
}

template <Clock C> PollResult PeriodicTask<C>::poll(std::stop_token stop) {
    if (in_poll_) {
        return PollResult::Reentrant;
    }
    const FlagGuard guard(in_poll_);
    if (!armed_ || stop.stop_requested()) {
        return PollResult::Waiting;
    }
    const time_point scheduled = next_release_;
    const ClockSample<time_point> started = clock_.now();
    if (started.status != ClockStatus::Ok) {
        record_.recorded = true;
        record_.unusable = true;
        record_.scheduled = scheduled;
        disarm();
        return PollResult::ScheduleError;
    }
    if (started.time < scheduled) {
        return PollResult::Waiting;
    }

    try {
        work_(scheduled, stop);
    } catch (...) {
        // The release is consumed before the exception reaches the supervisor.
        const ClockSample<time_point> completed = clock_.now();
        if (completed.status == ClockStatus::Ok) {
            const SimulatedCompletion shifted = apply_simulated_execution(completed.time);
            if (shifted.unrepresentable) {
                record_.recorded = true;
                record_.missed = false;
                record_.failed = true;
                record_.unusable = true;
                record_.scheduled = scheduled;
                disarm();
            } else {
                (void)finish_cycle(scheduled, shifted.time, true);
            }
        } else {
            (void)apply_simulated_execution(scheduled);
            record_.recorded = true;
            record_.failed = true;
            record_.unusable = true;
            record_.scheduled = scheduled;
            disarm();
        }
        throw;
    }
    const ClockSample<time_point> completed = clock_.now();
    if (completed.status != ClockStatus::Ok) {
        (void)apply_simulated_execution(scheduled);
        record_.recorded = true;
        record_.unusable = true;
        record_.scheduled = scheduled;
        disarm();
        return PollResult::ScheduleError;
    }
    const SimulatedCompletion shifted = apply_simulated_execution(completed.time);
    if (shifted.unrepresentable) {
        record_.recorded = true;
        record_.missed = false;
        record_.failed = false;
        record_.unusable = true;
        record_.scheduled = scheduled;
        record_.completed = completed.time;
        disarm();
        return PollResult::ScheduleError;
    }
    return finish_cycle(scheduled, shifted.time, false);
}

template <Clock C>
typename PeriodicTask<C>::SimulatedCompletion
PeriodicTask<C>::apply_simulated_execution(time_point completed) noexcept {
    SimulatedCompletion result{completed, false};
    if (note_ == nullptr) {
        return result;
    }
    const Duration extra = note_->extra;
    note_->extra = Duration::zero();
    if (extra <= Duration::zero()) {
        return result;
    }
    time_point shifted{};
    if (!checked_time_add(completed, extra, shifted)) {
        result.unrepresentable = true;
        return result;
    }
    result.time = shifted;
    return result;
}

template <Clock C>
PollResult PeriodicTask<C>::finish_cycle(time_point scheduled, time_point completed, bool failed) {
    Duration elapsed{};
    const bool elapsed_ok =
        completed >= scheduled && checked_time_between(completed, scheduled, elapsed);
    const DeadlineStatus timing = deadline_status(scheduled, completed, deadline_);
    const ReleaseUpdate update = advance_release(scheduled, completed);

    record_.recorded = true;
    record_.missed = timing == DeadlineStatus::Missed;
    record_.failed = failed;
    record_.unusable = timing == DeadlineStatus::Unusable || !elapsed_ok;
    record_.scheduled = scheduled;
    record_.completed = completed;
    record_.releases_skipped = update.representable ? update.releases_skipped : 0;

    if (!update.representable || record_.unusable) {
        disarm();
        return PollResult::ScheduleError;
    }
    next_release_ = update.next;
    if (failed) {
        return PollResult::Ran;
    }

    // Scheduler state is committed. Hook failures must not rerun the body.
    try {
        if (record_.missed && hooks_.on_deadline_miss) {
            hooks_.on_deadline_miss(DeadlineMissEvent<time_point>{
                id_, scheduled, completed, elapsed, deadline_, update.releases_skipped});
        }
        if (hooks_.on_cycle) {
            hooks_.on_cycle(TaskCycleEvent<time_point>{id_, scheduled, completed, elapsed,
                                                       deadline_, record_.missed,
                                                       update.releases_skipped});
        }
    } catch (...) {
        return PollResult::HookError;
    }
    return PollResult::Ran;
}

template <Clock C>
typename PeriodicTask<C>::time_point PeriodicTask<C>::next_release() const noexcept {
    return next_release_;
}

template <Clock C> TaskId PeriodicTask<C>::id() const noexcept {
    return id_;
}

template <Clock C> DeadlineRecord<C> PeriodicTask<C>::deadline_record() const noexcept {
    return record_;
}

template <Clock C>
typename PeriodicTask<C>::ReleaseUpdate
PeriodicTask<C>::advance_release(time_point scheduled, time_point completed) const {
    ReleaseUpdate update;
    if (completed < scheduled) {
        if (!checked_time_add(scheduled, period_, update.next)) {
            return update;
        }
        update.representable = true;
        return update;
    }

    Duration lag{};
    if (!checked_time_between(completed, scheduled, lag)) {
        return update;
    }
    const auto period_count = period_.count();
    if (period_count <= 0) {
        return update;
    }
    const auto lag_count = lag.count();
    if (lag_count < 0) {
        return update;
    }
    const auto steps = lag_count / period_count;
    const bool landed_on_boundary = lag_count > 0 && lag_count % period_count == 0;
    Duration::rep span = 0;
    if (landed_on_boundary) {
        span = steps;
        update.releases_skipped = static_cast<std::uint64_t>(steps - 1);
    } else {
        if (steps == std::numeric_limits<Duration::rep>::max()) {
            return update;
        }
        span = steps + 1;
        update.releases_skipped = static_cast<std::uint64_t>(steps);
    }
    if (span <= 0 || span > Duration::max().count() / period_count) {
        return update;
    }
    const Duration shift{span * period_count};
    if (!checked_time_add(scheduled, shift, update.next)) {
        return update;
    }
    update.representable = true;
    return update;
}

template class PeriodicTask<ManualClock>;
template class PeriodicTask<SteadyClock>;

} // namespace ares::core
