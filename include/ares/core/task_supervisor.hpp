#pragma once

#include "ares/core/periodic_task.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string_view>
#include <thread>

namespace ares::core {

enum class AddStatus : std::uint8_t { Ok, Invalid, Started, Full };
enum class StartStatus : std::uint8_t { Ok, AlreadyStarted, Failed };
enum class WorkerPhase : std::uint8_t { Idle, Waiting, InWork, Exited };
enum class WorkerHealth : std::uint8_t { Running, Stopped, Hung, Faulted, Invalid };
enum class WorkerFault : std::uint8_t { None, Exception, Unknown, Schedule, Hook };

inline constexpr std::size_t kMaxSupervisorWorkers = 8;

// One worker after shutdown's grace period and the following join.
// missed_grace: the worker was not Exited when the grace period ended.
// exited: the thread reached Exited by the time join returned.
// A clean shutdown has missed_grace false and exited true.
// A worker that misses the grace and then returns from join has both true.
// Join is still unbounded: a worker that never returns does not produce a report.
struct WorkerShutdown {
    std::size_t index{0};
    WorkerPhase phase_after_grace{WorkerPhase::Idle};
    bool missed_grace{false};
    bool exited{false};
};

struct ShutdownReport {
    std::size_t considered{0};
    std::size_t missed_grace{0};
    std::array<WorkerShutdown, kMaxSupervisorWorkers> workers{};
};

// Fixed worker table. The creating thread calls add/start/shutdown.
// Workers are joined before this object, and therefore before the clock it
// references, is destroyed. Threads are never detached.
// outlives: Clock must outlive this supervisor.
template <Clock C> class TaskSupervisor {
public:
    static constexpr std::size_t kMaxWorkers = kMaxSupervisorWorkers;
    // Called with the worker index about to be launched. A throw aborts startup
    // after any already-launched workers have been stopped and joined.
    using LaunchProbe = void (*)(std::size_t index);

    explicit TaskSupervisor(C& clock, LaunchProbe launch_probe = nullptr);
    ~TaskSupervisor();

    TaskSupervisor(const TaskSupervisor&) = delete;
    TaskSupervisor& operator=(const TaskSupervisor&) = delete;
    TaskSupervisor(TaskSupervisor&&) = delete;
    TaskSupervisor& operator=(TaskSupervisor&&) = delete;

    [[nodiscard]] AddStatus add(std::string_view name, TaskTiming timing, TaskWork<C> work,
                                TaskHooks<C> hooks, SimulatedExecution* execution = nullptr);
    [[nodiscard]] StartStatus start();
    void request_stop() noexcept;
    void join();
    // Host-clock grace, then a typed report, then join. Not flight time.
    // A worker has acknowledged shutdown only when its phase is Exited.
    // kStopGrace is how long that acknowledgement may take.
    static constexpr Duration kStopGrace{std::chrono::milliseconds{50}};
    ShutdownReport shutdown();

    [[nodiscard]] bool started() const noexcept;
    [[nodiscard]] std::size_t worker_count() const noexcept;
    [[nodiscard]] std::size_t stopped_workers() const noexcept;
    [[nodiscard]] std::uint64_t completed_cycles() const noexcept;
    [[nodiscard]] WorkerHealth health(std::size_t index) const noexcept;
    // nullopt when index is not a worker. None is a real worker with no fault.
    [[nodiscard]] std::optional<WorkerFault> fault(std::size_t index) const noexcept;
    // Authoritative deadline sample. nullopt when index is not a worker.
    // Read it after the worker has exited; poll writes it from the worker thread.
    [[nodiscard]] std::optional<DeadlineRecord<C>> deadline_record(std::size_t index) const;
    [[nodiscard]] std::stop_token shutdown_token() const noexcept;

private:
    struct Worker {
        PeriodicTask<C> task;
        std::jthread thread;
        std::atomic<WorkerPhase> phase{WorkerPhase::Idle};
        std::atomic<std::uint8_t> fault{static_cast<std::uint8_t>(WorkerFault::None)};

        Worker(TaskId id, TaskTiming timing, TaskWork<C> work, C& clock, TaskHooks<C> hooks)
            : task(id, timing, std::move(work), clock, std::move(hooks)) {}
    };

    enum class Gate : std::uint8_t { Closed, Run, Cancel };

    void rollback_launch(std::size_t launched);
    void thread_main(Worker& worker, std::stop_token stop);
    void note_fault(Worker& worker, WorkerFault fault) noexcept;
    [[nodiscard]] bool launched(std::size_t index) const noexcept;
    [[nodiscard]] bool pending_exit(std::size_t index) const noexcept;

    C& clock_;
    LaunchProbe launch_probe_{nullptr};
    std::stop_source shutdown_{};
    std::mutex gate_mutex_{};
    std::condition_variable_any gate_cv_{};
    Gate gate_{Gate::Closed};
    std::array<std::optional<Worker>, kMaxWorkers> workers_{};
    std::size_t count_{0};
    bool started_{false};
    std::atomic<std::size_t> stopped_workers_{0};
    std::atomic<std::uint64_t> completed_cycles_{0};
};

extern template class TaskSupervisor<ManualClock>;
extern template class TaskSupervisor<SteadyClock>;

} // namespace ares::core
