#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>

namespace ares::core {

// Mutex-protected fixed log. push overwrites the oldest record when full.
// T must be copyable without heap allocation. This log does not allocate.
template <typename T, std::size_t Capacity> class BoundedLog {
public:
    static_assert(Capacity > 0);

    enum class Push : std::uint8_t { Stored, Overwrote };

    BoundedLog() = default;
    BoundedLog(const BoundedLog&) = delete;
    BoundedLog& operator=(const BoundedLog&) = delete;
    BoundedLog(BoundedLog&&) = delete;
    BoundedLog& operator=(BoundedLog&&) = delete;

    [[nodiscard]] Push push(const T& item) {
        std::lock_guard lock(mutex_);
        if (size_ == Capacity) {
            slots_[next_] = item;
            next_ = (next_ + 1) % Capacity;
            overflow_ = true;
            ++overwrites_;
            return Push::Overwrote;
        }
        slots_[next_] = item;
        next_ = (next_ + 1) % Capacity;
        ++size_;
        return Push::Stored;
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard lock(mutex_);
        return size_;
    }

    [[nodiscard]] bool overflowed() const {
        std::lock_guard lock(mutex_);
        return overflow_;
    }

    [[nodiscard]] std::uint64_t overwrite_count() const {
        std::lock_guard lock(mutex_);
        return overwrites_;
    }

    // Copies the stored records in order, oldest first. Returns how many fit in out.
    [[nodiscard]] std::size_t copy_into(std::span<T> out) const {
        std::lock_guard lock(mutex_);
        const std::size_t count = size_ < out.size() ? size_ : out.size();
        const std::size_t first = size_ == Capacity ? next_ : 0;
        for (std::size_t index = 0; index < count; ++index) {
            out[index] = slots_[(first + index) % Capacity];
        }
        return count;
    }

private:
    std::array<T, Capacity> slots_{};
    std::size_t next_{0};
    std::size_t size_{0};
    bool overflow_{false};
    std::uint64_t overwrites_{0};
    mutable std::mutex mutex_;
};

} // namespace ares::core
