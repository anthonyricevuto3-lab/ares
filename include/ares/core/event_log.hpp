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

    [[nodiscard]] Status publish(const Event& event) { return records_.push(event); }

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
};

} // namespace ares::core
