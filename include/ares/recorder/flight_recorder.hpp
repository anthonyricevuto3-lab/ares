#pragma once

#include "ares/flight/fault_events.hpp"
#include "ares/flight/system_event.hpp"
#include "ares/recorder/format.hpp"
#include "ares/simulation/chaos_engine.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ares::recorder {

// Optional mission observer. Append copies one fixed slot and does not allocate.
// Disk I/O happens only in commit(), after workers have stopped.
// Overflow keeps the ordered prefix, latches, and does not touch flight state.
template <std::size_t Capacity = 256, std::uint32_t SequenceLimit = 0xFFFFFFFFU>
class FlightRecorder {
    static_assert(Capacity > 1);
    static_assert(SequenceLimit > 0);

public:
    FlightRecorder() = default;
    ~FlightRecorder() {
        if (file_ != nullptr) {
            std::fclose(file_);
            file_ = nullptr;
        }
    }

    FlightRecorder(const FlightRecorder&) = delete;
    FlightRecorder& operator=(const FlightRecorder&) = delete;
    FlightRecorder(FlightRecorder&&) = delete;
    FlightRecorder& operator=(FlightRecorder&&) = delete;

    void set_mission(std::uint64_t seed, std::string_view scenario) {
        std::lock_guard lock(mutex_);
        seed_ = seed;
        scenario_ = {};
        const std::size_t count =
            scenario.size() < (kScenarioNameBytes - 1) ? scenario.size() : (kScenarioNameBytes - 1);
        for (std::size_t index = 0; index < count; ++index) {
            scenario_.at(index) = *(scenario.data() + index);
        }
    }

    // Enables recording without a file. Tests use this.
    void arm() noexcept {
        std::lock_guard lock(mutex_);
        enabled_ = true;
    }

    [[nodiscard]] bool open(const std::string& path) {
        std::lock_guard lock(mutex_);
        if (file_ != nullptr || path.empty()) {
            io_error_ = true;
            return false;
        }
        file_ = std::fopen(path.c_str(), "wb+");
        if (file_ == nullptr) {
            io_error_ = true;
            return false;
        }
        enabled_ = true;
        return true;
    }

    void fail_commits() noexcept {
        std::lock_guard lock(mutex_);
        force_fail_ = true;
    }

    [[nodiscard]] bool enabled() const noexcept {
        std::lock_guard lock(mutex_);
        return enabled_;
    }
    [[nodiscard]] bool overflowed() const noexcept {
        std::lock_guard lock(mutex_);
        return overflow_;
    }
    [[nodiscard]] bool io_error() const noexcept {
        std::lock_guard lock(mutex_);
        return io_error_;
    }
    [[nodiscard]] bool sealed() const noexcept {
        std::lock_guard lock(mutex_);
        return sealed_;
    }
    [[nodiscard]] std::size_t size() const noexcept {
        std::lock_guard lock(mutex_);
        return size_;
    }

    [[nodiscard]] bool record_mission_start(std::uint64_t seed, std::string_view scenario,
                                            std::int64_t time_ns) {
        std::array<std::uint8_t, kPayloadCapacity> payload{};
        store_i64(payload.data(), static_cast<std::int64_t>(seed));
        const std::size_t count =
            scenario.size() < (kScenarioNameBytes - 1) ? scenario.size() : (kScenarioNameBytes - 1);
        for (std::size_t index = 0; index < count; ++index) {
            payload.at(8 + index) = static_cast<std::uint8_t>(*(scenario.data() + index));
        }
        return append(RecordType::MissionStart, time_ns, payload, 24);
    }

    [[nodiscard]] bool record_generation(std::uint32_t previous, std::uint32_t generation,
                                         std::int64_t time_ns) {
        std::array<std::uint8_t, kPayloadCapacity> payload{};
        store_u32(payload.data(), previous);
        store_u32(payload.data() + 4, generation);
        return append(RecordType::TaskGenerationChanged, time_ns, payload, 8);
    }

    [[nodiscard]] bool record_gps(std::uint8_t selection, bool isolated, std::int64_t time_ns) {
        std::array<std::uint8_t, kPayloadCapacity> payload{};
        payload.at(0) = selection;
        payload.at(1) = isolated ? 1U : 0U;
        return append(RecordType::GpsSelectionChanged, time_ns, payload, 4);
    }

    [[nodiscard]] bool record_chaos(const simulation::ChaosEdge& edge, std::int64_t time_ns) {
        std::array<std::uint8_t, kPayloadCapacity> payload{};
        payload.at(0) = static_cast<std::uint8_t>(edge.kind);
        payload.at(1) = static_cast<std::uint8_t>(edge.target);
        payload.at(2) = edge.started ? 1U : 0U;
        store_u32(payload.data() + 4, edge.sequence);
        store_i64(payload.data() + 8, edge.at.count());
        const RecordType type = edge.started ? RecordType::ChaosStarted : RecordType::ChaosEnded;
        return append(type, time_ns, payload, 16);
    }

    [[nodiscard]] bool append_record(RecordType type, std::int64_t time_ns,
                                     const std::array<std::uint8_t, kPayloadCapacity>& payload,
                                     std::uint16_t payload_size) {
        return append(type, time_ns, payload, payload_size);
    }

    [[nodiscard]] bool seal(std::int64_t time_ns, std::uint8_t mode, std::int32_t exit_code) {
        std::lock_guard lock(mutex_);
        if (!enabled_ || sealed_) {
            return false;
        }
        std::array<std::uint8_t, kPayloadCapacity> payload{};
        payload.at(0) = mode;
        payload.at(1) = overflow_ ? static_cast<std::uint8_t>(1) : static_cast<std::uint8_t>(0);
        payload.at(2) = io_error_ ? static_cast<std::uint8_t>(1) : static_cast<std::uint8_t>(0);
        payload.at(3) = 1;
        store_i32(payload.data() + 4, exit_code);
        store_u32(payload.data() + 8, activations_);
        store_u32(payload.data() + 12, clears_);
        store_u32(payload.data() + 16, recovery_started_);
        store_u32(payload.data() + 20, recovery_succeeded_);
        store_u32(payload.data() + 24, recovery_failed_);
        if (!append_locked(RecordType::MissionEnd, time_ns, payload, 32)) {
            return false;
        }
        sealed_ = true;
        return true;
    }

    [[nodiscard]] std::vector<std::uint8_t> encode() const {
        std::lock_guard lock(mutex_);
        std::vector<std::uint8_t> bytes(kHeaderBytes, 0);
        for (std::size_t index = 0; index < 8; ++index) {
            bytes.at(index) = static_cast<std::uint8_t>(kMagic.at(index));
        }
        store_u16(bytes.data() + 8, kFormatMajor);
        store_u16(bytes.data() + 10, kFormatMinor);
        store_u32(bytes.data() + 12, kEndianMarker);
        store_u16(bytes.data() + 16, kApplicationMajor);
        store_u16(bytes.data() + 18, kApplicationMinor);
        store_u16(bytes.data() + 20, kApplicationPatch);
        store_u16(bytes.data() + 22, static_cast<std::uint16_t>(kHeaderBytes));
        store_i64(bytes.data() + 24, static_cast<std::int64_t>(seed_));
        for (std::size_t index = 0; index < kScenarioNameBytes; ++index) {
            bytes.at(32 + index) = static_cast<std::uint8_t>(scenario_.at(index));
        }
        std::uint32_t flags = 0;
        if (sealed_) {
            flags |= static_cast<std::uint32_t>(HeaderFlag::Finalized);
        }
        if (overflow_) {
            flags |= static_cast<std::uint32_t>(HeaderFlag::Overflow);
        }
        if (io_error_) {
            flags |= static_cast<std::uint32_t>(HeaderFlag::IoError);
        }
        store_u32(bytes.data() + 48, flags);
        store_u32(bytes.data() + 52, static_cast<std::uint32_t>(size_));

        for (std::size_t index = 0; index < size_; ++index) {
            const Slot& slot = slots_.at(index);
            const std::size_t wire = kRecordPrefix + slot.payload_size + kRecordCrcBytes;
            const std::size_t at = bytes.size();
            bytes.resize(at + wire, 0);
            std::uint8_t* prefix = bytes.data() + at;
            store_u16(prefix, slot.type);
            store_u16(prefix + 2, kRecordSchema);
            store_u16(prefix + 4, slot.payload_size);
            store_u16(prefix + 6, 0);
            store_i64(prefix + 8, slot.time_ns);
            store_u32(prefix + 16, slot.sequence);
            for (std::uint16_t byte = 0; byte < slot.payload_size; ++byte) {
                *(prefix + kRecordPrefix + byte) = slot.payload.at(byte);
            }
            const auto covered =
                std::span<const std::uint8_t>(prefix, kRecordPrefix + slot.payload_size);
            store_u32(prefix + kRecordPrefix + slot.payload_size, crc32(covered));
        }

        const auto payload =
            std::span<const std::uint8_t>(bytes.data() + kHeaderBytes, bytes.size() - kHeaderBytes);
        store_u32(bytes.data() + 56, crc32(payload));
        store_u32(bytes.data() + 60, 0);
        const auto header = std::span<const std::uint8_t>(bytes.data(), 60);
        store_u32(bytes.data() + 60, crc32(header));
        return bytes;
    }

    [[nodiscard]] bool commit() {
        const std::vector<std::uint8_t> bytes = encode();
        std::lock_guard lock(mutex_);
        if (force_fail_) {
            io_error_ = true;
            return false;
        }
        if (file_ == nullptr) {
            committed_ = true;
            return !io_error_;
        }
        if (std::fseek(file_, 0, SEEK_SET) != 0 ||
            std::fwrite(bytes.data(), 1, bytes.size(), file_) != bytes.size() ||
            std::fflush(file_) != 0) {
            io_error_ = true;
            return false;
        }
        std::fclose(file_);
        file_ = nullptr;
        committed_ = true;
        return true;
    }

private:
    struct Slot {
        std::uint16_t type{0};
        std::uint16_t payload_size{0};
        std::int64_t time_ns{0};
        std::uint32_t sequence{0};
        std::array<std::uint8_t, kPayloadCapacity> payload{};
    };

    [[nodiscard]] bool append(RecordType type, std::int64_t time_ns,
                              const std::array<std::uint8_t, kPayloadCapacity>& payload,
                              std::uint16_t payload_size) noexcept {
        try {
            std::lock_guard lock(mutex_);
            return append_locked(type, time_ns, payload, payload_size);
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] bool append_locked(RecordType type, std::int64_t time_ns,
                                     const std::array<std::uint8_t, kPayloadCapacity>& payload,
                                     std::uint16_t payload_size) {
        if (!enabled_ || sealed_) {
            return false;
        }
        const bool mission_end = type == RecordType::MissionEnd;
        const bool full = size_ >= Capacity || (!mission_end && (size_ + 1U) >= Capacity);
        if (full || next_sequence_ >= SequenceLimit) {
            overflow_ = true;
            return false;
        }
        Slot& slot = slots_.at(size_);
        slot = {};
        slot.type = static_cast<std::uint16_t>(type);
        slot.payload_size = payload_size;
        slot.time_ns = time_ns;
        slot.sequence = next_sequence_;
        for (std::uint16_t index = 0; index < payload_size; ++index) {
            slot.payload.at(index) = payload.at(index);
        }
        ++size_;
        ++next_sequence_;
        if (type == RecordType::FaultActivated) {
            ++activations_;
        } else if (type == RecordType::FaultCleared) {
            ++clears_;
        } else if (type == RecordType::RecoveryStarted) {
            ++recovery_started_;
        } else if (type == RecordType::RecoverySucceeded) {
            ++recovery_succeeded_;
        } else if (type == RecordType::RecoveryFailed) {
            ++recovery_failed_;
        }
        return true;
    }

    mutable std::mutex mutex_{};
    std::array<Slot, Capacity> slots_{};
    std::size_t size_{0};
    std::uint32_t next_sequence_{0};
    bool enabled_{false};
    bool overflow_{false};
    bool io_error_{false};
    bool sealed_{false};
    bool committed_{false};
    bool force_fail_{false};
    std::uint64_t seed_{0};
    std::array<char, kScenarioNameBytes> scenario_{};
    std::uint32_t activations_{0};
    std::uint32_t clears_{0};
    std::uint32_t recovery_started_{0};
    std::uint32_t recovery_succeeded_{0};
    std::uint32_t recovery_failed_{0};
    std::FILE* file_{nullptr};
};

template <typename TimePoint>
[[nodiscard]] inline std::int64_t event_time_ns(TimePoint time) noexcept {
    return time.time_since_epoch().count();
}

template <std::size_t Capacity, std::uint32_t SequenceLimit, typename TimePoint>
void absorb(FlightRecorder<Capacity, SequenceLimit>& recorder,
            const flight::SystemEvent<TimePoint>& event) noexcept {
    if (const auto* mode = std::get_if<flight::ModeChangedEvent<TimePoint>>(&event)) {
        std::array<std::uint8_t, kPayloadCapacity> payload{};
        payload.at(0) = static_cast<std::uint8_t>(mode->from);
        payload.at(1) = static_cast<std::uint8_t>(mode->to);
        (void)recorder.append_record(RecordType::ModeChanged, event_time_ns(mode->time), payload,
                                     4);
        return;
    }
    if (const auto* activated = std::get_if<flight::FaultActivatedEvent<TimePoint>>(&event)) {
        std::array<std::uint8_t, kPayloadCapacity> payload{};
        payload.at(0) = static_cast<std::uint8_t>(activated->type);
        payload.at(1) = static_cast<std::uint8_t>(activated->source);
        payload.at(2) = static_cast<std::uint8_t>(activated->severity);
        (void)recorder.append_record(RecordType::FaultActivated, event_time_ns(activated->time),
                                     payload, 4);
        if (activated->type == flight::FaultType::DeadlineMiss) {
            store_u32(payload.data() + 4, 1U);
            (void)recorder.append_record(RecordType::DeadlineMiss, event_time_ns(activated->time),
                                         payload, 8);
        }
        return;
    }
    if (const auto* updated = std::get_if<flight::FaultUpdatedEvent<TimePoint>>(&event)) {
        std::array<std::uint8_t, kPayloadCapacity> payload{};
        payload.at(0) = static_cast<std::uint8_t>(updated->type);
        payload.at(1) = static_cast<std::uint8_t>(updated->source);
        payload.at(2) = static_cast<std::uint8_t>(updated->severity);
        store_u32(payload.data() + 4, updated->consecutive_count);
        (void)recorder.append_record(RecordType::FaultUpdated, event_time_ns(updated->time),
                                     payload, 8);
        if (updated->type == flight::FaultType::DeadlineMiss) {
            (void)recorder.append_record(RecordType::DeadlineMiss, event_time_ns(updated->time),
                                         payload, 8);
        }
        return;
    }
    if (const auto* cleared = std::get_if<flight::FaultClearedEvent<TimePoint>>(&event)) {
        std::array<std::uint8_t, kPayloadCapacity> payload{};
        payload.at(0) = static_cast<std::uint8_t>(cleared->type);
        payload.at(1) = static_cast<std::uint8_t>(cleared->source);
        (void)recorder.append_record(RecordType::FaultCleared, event_time_ns(cleared->time),
                                     payload, 4);
        return;
    }
    if (const auto* recovery = std::get_if<flight::RecoveryEvent<TimePoint>>(&event)) {
        std::array<std::uint8_t, kPayloadCapacity> payload{};
        payload.at(0) = static_cast<std::uint8_t>(recovery->action);
        payload.at(1) = static_cast<std::uint8_t>(recovery->target);
        payload.at(2) = recovery->attempt;
        store_u32(payload.data() + 4, recovery->generation);
        RecordType type = RecordType::RecoveryStarted;
        if (recovery->notice == flight::RecoveryNotice::Succeeded) {
            type = RecordType::RecoverySucceeded;
        } else if (recovery->notice == flight::RecoveryNotice::Failed) {
            type = RecordType::RecoveryFailed;
        }
        (void)recorder.append_record(type, event_time_ns(recovery->time), payload, 8);
    }
}

} // namespace ares::recorder
