#pragma once

#include "ares/core/bounded_log.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ares::core {

// Fixed-capacity flight event record. publish copies into the slot and does not
// allocate. When full, the oldest event is overwritten and overflow is counted.
// The newest event is retained. snapshot allocates; flight publish must not call it.
template <typename Event, std::size_t Capacity = 64> class EventLog {
public:
    using Status = typename BoundedLog<Event, Capacity>::Push;

    EventLog() = default;
    ~EventLog() = default;

    EventLog(const EventLog&) = delete;
    EventLog& operator=(const EventLog&) = delete;
    EventLog(EventLog&&) = delete;
    EventLog& operator=(EventLog&&) = delete;

    [[nodiscard]] Status publish(const Event& event) {
        const Status status = records_.push(event);
        if (observer_ != nullptr) {
            observer_(&event, observer_context_);
        }
        return status;
    }

    // Set before publishers run. The observer must not call publish on this log.
    // It runs after the log mutex is released.
    using Observer = void (*)(const Event* event, void* context);
    void set_observer(Observer observer, void* context) noexcept {
        observer_ = observer;
        observer_context_ = context;
    }

    [[nodiscard]] std::vector<Event> snapshot() const {
        std::vector<Event> copy(size());
        const std::size_t copied = records_.copy_into(copy);
        copy.resize(copied);
        return copy;
    }

    [[nodiscard]] std::size_t size() const { return records_.size(); }

    [[nodiscard]] bool overflowed() const { return records_.overflowed(); }

    [[nodiscard]] std::uint64_t overwrite_count() const { return records_.overwrite_count(); }

    static constexpr std::size_t capacity = Capacity;

private:
    BoundedLog<Event, Capacity> records_{};
    Observer observer_{nullptr};
    void* observer_context_{nullptr};
};

} // namespace ares::core
