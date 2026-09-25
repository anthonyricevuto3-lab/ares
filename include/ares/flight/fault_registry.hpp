#pragma once

#include "ares/flight/fault.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace ares::flight {

enum class RegistryStatus : std::uint8_t {
    Activated,
    Updated,
    Cleared,
    Unchanged,
    RejectedFull,
    RejectedSource,
};

// Fixed table of logical faults. Not thread-safe: one owner mutates it.
// Identity is (type, source). A repeat updates that slot.
// An inactive slot may be reused when the table is full. An active fault is
// never evicted. If every slot is active, raise returns RejectedFull, leaves
// the table unchanged, and latches saturated(). The latch stays set.
template <typename TimePoint, std::size_t Capacity> class FaultRegistry {
public:
    static_assert(Capacity > 0);
    static constexpr std::size_t capacity = Capacity;

    FaultRegistry() = default;

    [[nodiscard]] RegistryStatus raise(FaultType type, FaultSource source, FaultSeverity severity,
                                       TimePoint time) {
        if (Slot* existing = find_slot(type, source)) {
            FaultRecord<TimePoint>& record = existing->record;
            record.severity = severity;
            record.last_detected = time;
            saturate_increment(record.occurrence_count);
            if (record.active) {
                saturate_increment(record.consecutive_count);
                return RegistryStatus::Updated;
            }
            record.active = true;
            record.consecutive_count = 1;
            return RegistryStatus::Activated;
        }

        Slot* destination = vacant_slot();
        if (destination == nullptr) {
            destination = inactive_slot();
        }
        if (destination == nullptr) {
            saturated_ = true;
            return RegistryStatus::RejectedFull;
        }
        destination->occupied = true;
        destination->record = FaultRecord<TimePoint>{
            .type = type,
            .source = source,
            .severity = severity,
            .first_detected = time,
            .last_detected = time,
            .occurrence_count = 1,
            .consecutive_count = 1,
            .active = true,
        };
        return RegistryStatus::Activated;
    }

    // History stays in the slot. consecutive_count returns to zero.
    [[nodiscard]] RegistryStatus clear(FaultType type, FaultSource source) {
        Slot* existing = find_slot(type, source);
        if (existing == nullptr || !existing->record.active) {
            return RegistryStatus::Unchanged;
        }
        existing->record.active = false;
        existing->record.consecutive_count = 0;
        return RegistryStatus::Cleared;
    }

    // Inactive records are still found. A missing identity is nullptr.
    [[nodiscard]] const FaultRecord<TimePoint>* find(FaultType type, FaultSource source) const {
        const Slot* existing = find_slot(type, source);
        return existing == nullptr ? nullptr : &existing->record;
    }

    [[nodiscard]] bool saturated() const noexcept { return saturated_; }

    [[nodiscard]] std::size_t active_count() const noexcept {
        std::size_t count = 0;
        for (const Slot& slot : slots_) {
            if (slot.occupied && slot.record.active) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] std::size_t occupied_count() const noexcept {
        std::size_t count = 0;
        for (const Slot& slot : slots_) {
            if (slot.occupied) {
                ++count;
            }
        }
        return count;
    }

    // Ascending slot order. Returns how many records were copied.
    [[nodiscard]] std::size_t copy_active(std::span<FaultRecord<TimePoint>> out) const {
        std::size_t written = 0;
        for (const Slot& slot : slots_) {
            if (!slot.occupied || !slot.record.active) {
                continue;
            }
            if (written == out.size()) {
                break;
            }
            out[written] = slot.record;
            ++written;
        }
        return written;
    }

    template <typename Fn> void for_each_active(const Fn& visitor) const {
        for (const Slot& slot : slots_) {
            if (slot.occupied && slot.record.active) {
                visitor(slot.record);
            }
        }
    }

private:
    struct Slot {
        FaultRecord<TimePoint> record{};
        bool occupied{false};
    };

    static void saturate_increment(std::uint32_t& value) noexcept {
        if (value < std::numeric_limits<std::uint32_t>::max()) {
            ++value;
        }
    }

    [[nodiscard]] Slot* find_slot(FaultType type, FaultSource source) noexcept {
        for (Slot& slot : slots_) {
            if (slot.occupied && slot.record.type == type && slot.record.source == source) {
                return &slot;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const Slot* find_slot(FaultType type, FaultSource source) const noexcept {
        for (const Slot& slot : slots_) {
            if (slot.occupied && slot.record.type == type && slot.record.source == source) {
                return &slot;
            }
        }
        return nullptr;
    }

    [[nodiscard]] Slot* vacant_slot() noexcept {
        for (Slot& slot : slots_) {
            if (!slot.occupied) {
                return &slot;
            }
        }
        return nullptr;
    }

    // Lowest index. Reclaiming discards that inactive identity.
    [[nodiscard]] Slot* inactive_slot() noexcept {
        for (Slot& slot : slots_) {
            if (slot.occupied && !slot.record.active) {
                return &slot;
            }
        }
        return nullptr;
    }

    std::array<Slot, Capacity> slots_{};
    bool saturated_{false};
};

} // namespace ares::flight
