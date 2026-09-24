#pragma once

#include "ares/core/task_id.hpp"
#include "ares/core/time.hpp"

#include <cstdint>

namespace ares::core {

template <typename TimePoint> struct TaskCycleEvent {
    TaskId id{};
    TimePoint scheduled{};
    TimePoint completed{};
    Duration elapsed{};
    Duration deadline{};
    bool deadline_missed{false};
    // Releases strictly before completion that were not executed.
    // The release that completion lands on is the next release, not a skip.
    std::uint64_t releases_skipped{0};

    bool operator==(const TaskCycleEvent&) const = default;
};

template <typename TimePoint> struct DeadlineMissEvent {
    TaskId id{};
    TimePoint scheduled{};
    TimePoint completed{};
    Duration elapsed{};
    Duration deadline{};
    std::uint64_t releases_skipped{0};

    bool operator==(const DeadlineMissEvent&) const = default;
};

} // namespace ares::core
