#include "ares/core/task_supervisor.hpp"

#include <chrono>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>

namespace ares::core {

template <Clock C>
TaskSupervisor<C>::TaskSupervisor(C& clock, LaunchProbe launch_probe)
    : clock_(clock), launch_probe_(launch_probe) {}

template <Clock C> TaskSupervisor<C>::~TaskSupervisor() {
    try {
        (void)shutdown();
    } catch (...) {
        std::terminate();
    }
}

template <Clock C>
AddStatus TaskSupervisor<C>::add(std::string_view name, TaskTiming timing, TaskWork<C> work,
                                 TaskHooks<C> hooks) {
    if (started_) {
        return AddStatus::Started;
    }
    if (count_ == kMaxWorkers) {
        return AddStatus::Full;
    }
    const std::optional<TaskId> id = TaskId::make(name);
    if (!id.has_value() || !work || !valid_task_timing(timing)) {
        return AddStatus::Invalid;
    }
    workers_.at(count_).emplace(*id, timing, std::move(work), clock_, std::move(hooks));
    ++count_;
    return AddStatus::Ok;
}

template <Clock C> void TaskSupervisor<C>::rollback_launch(std::size_t launched) {
    {
        std::lock_guard lock(gate_mutex_);
        gate_ = Gate::Cancel;
    }
    gate_cv_.notify_all();
    request_stop();
    for (std::size_t index = 0; index < launched; ++index) {
        auto& slot = workers_.at(index);
        if (!slot.has_value()) {
            continue;
        }
        Worker& worker = slot.value();
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
    }
    for (std::size_t index = 0; index < count_; ++index) {
        auto& slot = workers_.at(index);
        if (!slot.has_value()) {
            continue;
        }
        slot.value().task.disarm();
    }
    shutdown_ = std::stop_source{};
    started_ = false;
}

template <Clock C> StartStatus TaskSupervisor<C>::start() {
    if (started_) {
        return StartStatus::AlreadyStarted;
    }
    shutdown_ = std::stop_source{};
    stopped_workers_.store(0, std::memory_order_relaxed);
    completed_cycles_.store(0, std::memory_order_relaxed);
    {
        std::lock_guard lock(gate_mutex_);
        gate_ = Gate::Closed;
    }
    std::size_t launched = 0;
    try {
        const ClockSample<typename C::time_point> start_sample = clock_.now();
        if (start_sample.status != ClockStatus::Ok) {
            return StartStatus::Failed;
        }
        const typename C::time_point start_time = start_sample.time;
        for (std::size_t index = 0; index < count_; ++index) {
            if (launch_probe_ != nullptr) {
                launch_probe_(index);
            }
            auto& slot = workers_.at(index);
            if (!slot.has_value()) {
                throw std::logic_error("worker slot is empty");
            }
            Worker& engaged = slot.value();
            if (engaged.task.arm(start_time) != ArmStatus::Armed) {
                throw std::logic_error("periodic task was already armed");
            }
            engaged.phase.store(WorkerPhase::Idle, std::memory_order_release);
            engaged.fault.store(static_cast<std::uint8_t>(WorkerFault::None),
                                std::memory_order_relaxed);
            Worker* const worker = &engaged;
            worker->thread =
                std::jthread([this, worker](std::stop_token stop) { thread_main(*worker, stop); });
            ++launched;
        }
    } catch (...) {
        rollback_launch(launched);
        return StartStatus::Failed;
    }
    started_ = true;
    {
        std::lock_guard lock(gate_mutex_);
        gate_ = Gate::Run;
    }
    gate_cv_.notify_all();
    return StartStatus::Ok;
}

template <Clock C> void TaskSupervisor<C>::request_stop() noexcept {
    {
        std::lock_guard lock(gate_mutex_);
        shutdown_.request_stop();
    }
    for (std::size_t index = 0; index < count_; ++index) {
        auto& slot = *(workers_.data() + index);
        if (!slot.has_value()) {
            continue;
        }
        Worker& worker = *slot;
        if (worker.thread.joinable()) {
            worker.thread.request_stop();
        }
    }
    gate_cv_.notify_all();
}

template <Clock C> void TaskSupervisor<C>::join() {
    for (std::size_t index = 0; index < count_; ++index) {
        auto& slot = workers_.at(index);
        if (!slot.has_value()) {
            continue;
        }
        Worker& worker = slot.value();
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
    }
}

template <Clock C> bool TaskSupervisor<C>::launched(std::size_t index) const noexcept {
    if (index >= count_) {
        return false;
    }
    const auto& slot = *(workers_.data() + index);
    if (!slot.has_value()) {
        return false;
    }
    const Worker& worker = *slot;
    if (worker.thread.joinable()) {
        return true;
    }
    return worker.phase.load(std::memory_order_acquire) != WorkerPhase::Idle;
}

template <Clock C> bool TaskSupervisor<C>::pending_exit(std::size_t index) const noexcept {
    if (index >= count_) {
        return false;
    }
    const auto& slot = *(workers_.data() + index);
    if (!slot.has_value()) {
        return false;
    }
    return (*slot).phase.load(std::memory_order_acquire) != WorkerPhase::Exited;
}

template <Clock C> ShutdownReport TaskSupervisor<C>::shutdown() {
    request_stop();
    const auto deadline = std::chrono::steady_clock::now() + kStopGrace;
    while (std::chrono::steady_clock::now() < deadline) {
        bool pending = false;
        for (std::size_t index = 0; index < count_; ++index) {
            if (pending_exit(index)) {
                pending = true;
                break;
            }
        }
        if (!pending) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    ShutdownReport report;
    for (std::size_t index = 0; index < count_; ++index) {
        if (!launched(index)) {
            continue;
        }
        WorkerShutdown& recorded = report.workers.at(report.considered);
        recorded.index = index;
        const auto& slot = workers_.at(index);
        if (!slot.has_value()) {
            continue;
        }
        recorded.phase_after_grace = slot.value().phase.load(std::memory_order_acquire);
        recorded.missed_grace = recorded.phase_after_grace != WorkerPhase::Exited;
        if (recorded.missed_grace) {
            ++report.missed_grace;
        }
        ++report.considered;
    }
    join();
    for (std::size_t slot = 0; slot < report.considered; ++slot) {
        WorkerShutdown& recorded = report.workers.at(slot);
        const auto& worker_slot = workers_.at(recorded.index);
        if (!worker_slot.has_value()) {
            recorded.exited = false;
            continue;
        }
        recorded.exited =
            worker_slot.value().phase.load(std::memory_order_acquire) == WorkerPhase::Exited;
    }
    return report;
}

template <Clock C> bool TaskSupervisor<C>::started() const noexcept {
    return started_;
}

template <Clock C> void TaskSupervisor<C>::note_fault(Worker& worker, WorkerFault fault) noexcept {
    worker.fault.store(static_cast<std::uint8_t>(fault), std::memory_order_release);
    request_stop();
}

template <Clock C> void TaskSupervisor<C>::thread_main(Worker& worker, std::stop_token stop) {
    struct MarkStopped {
        std::atomic<std::size_t>& counter;
        std::atomic<WorkerPhase>& phase;
        ~MarkStopped() {
            phase.store(WorkerPhase::Exited, std::memory_order_release);
            counter.fetch_add(1, std::memory_order_acq_rel);
        }
    } mark{stopped_workers_, worker.phase};

    {
        std::unique_lock lock(gate_mutex_);
        gate_cv_.wait(lock, stop,
                      [&] { return gate_ != Gate::Closed || shutdown_.stop_requested(); });
        if (gate_ != Gate::Run || stop.stop_requested() || shutdown_.stop_requested()) {
            return;
        }
    }

    try {
        while (!stop.stop_requested() && !shutdown_.stop_requested()) {
            worker.phase.store(WorkerPhase::Waiting, std::memory_order_release);
            if (clock_.wait_until(worker.task.next_release(), stop) != ClockStatus::Ok) {
                note_fault(worker, WorkerFault::Schedule);
                break;
            }
            if (stop.stop_requested() || shutdown_.stop_requested()) {
                break;
            }
            const ClockSample<typename C::time_point> sample = clock_.now();
            if (sample.status != ClockStatus::Ok) {
                note_fault(worker, WorkerFault::Schedule);
                break;
            }
            if (sample.time < worker.task.next_release()) {
                continue;
            }
            worker.phase.store(WorkerPhase::InWork, std::memory_order_release);
            const PollResult result = worker.task.poll(stop);
            if (result == PollResult::ScheduleError) {
                note_fault(worker, WorkerFault::Schedule);
                break;
            }
            if (result == PollResult::HookError) {
                note_fault(worker, WorkerFault::Hook);
                break;
            }
            if (result == PollResult::Ran) {
                completed_cycles_.fetch_add(1, std::memory_order_acq_rel);
            }
        }
    } catch (const std::exception&) {
        note_fault(worker, WorkerFault::Exception);
    } catch (...) {
        note_fault(worker, WorkerFault::Unknown);
    }
}

template <Clock C> std::size_t TaskSupervisor<C>::worker_count() const noexcept {
    return count_;
}

template <Clock C> std::size_t TaskSupervisor<C>::stopped_workers() const noexcept {
    return stopped_workers_.load(std::memory_order_acquire);
}

template <Clock C> std::uint64_t TaskSupervisor<C>::completed_cycles() const noexcept {
    return completed_cycles_.load(std::memory_order_acquire);
}

template <Clock C> WorkerHealth TaskSupervisor<C>::health(std::size_t index) const noexcept {
    if (index >= count_) {
        return WorkerHealth::Invalid;
    }
    const auto& slot = *(workers_.data() + index);
    if (!slot.has_value()) {
        return WorkerHealth::Invalid;
    }
    const Worker& worker = *slot;
    const WorkerPhase phase = worker.phase.load(std::memory_order_acquire);
    if (phase == WorkerPhase::Exited) {
        const auto fault = static_cast<WorkerFault>(worker.fault.load(std::memory_order_acquire));
        return fault == WorkerFault::None ? WorkerHealth::Stopped : WorkerHealth::Faulted;
    }
    if (shutdown_.stop_requested() && phase == WorkerPhase::InWork) {
        return WorkerHealth::Hung;
    }
    return WorkerHealth::Running;
}

template <Clock C>
std::optional<WorkerFault> TaskSupervisor<C>::fault(std::size_t index) const noexcept {
    if (index >= count_) {
        return std::nullopt;
    }
    const auto& slot = *(workers_.data() + index);
    if (!slot.has_value()) {
        return std::nullopt;
    }
    return static_cast<WorkerFault>((*slot).fault.load(std::memory_order_acquire));
}

template <Clock C>
std::optional<DeadlineRecord<C>> TaskSupervisor<C>::deadline_record(std::size_t index) const {
    if (index >= count_) {
        return std::nullopt;
    }
    const auto& slot = workers_.at(index);
    if (!slot.has_value()) {
        return std::nullopt;
    }
    return slot.value().task.deadline_record();
}

template <Clock C> std::stop_token TaskSupervisor<C>::shutdown_token() const noexcept {
    return shutdown_.get_token();
}

template class TaskSupervisor<ManualClock>;
template class TaskSupervisor<SteadyClock>;

} // namespace ares::core
