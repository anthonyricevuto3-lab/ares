#pragma once

#include "ares/version.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ares::recorder {

inline constexpr std::uint16_t kFormatMajor = 1;
inline constexpr std::uint16_t kFormatMinor = 0;
inline constexpr std::uint16_t kApplicationMajor = static_cast<std::uint16_t>(ares::kVersionMajor);
inline constexpr std::uint16_t kApplicationMinor = static_cast<std::uint16_t>(ares::kVersionMinor);
inline constexpr std::uint16_t kApplicationPatch = static_cast<std::uint16_t>(ares::kVersionPatch);
inline constexpr std::uint32_t kEndianMarker = 0x01020304U;
inline constexpr std::size_t kHeaderBytes = 64;
inline constexpr std::size_t kRecordPrefix = 20;
inline constexpr std::size_t kRecordCrcBytes = 4;
inline constexpr std::size_t kPayloadCapacity = 32;
inline constexpr std::uint16_t kRecordSchema = 1;
inline constexpr std::size_t kScenarioNameBytes = 16;

inline constexpr std::array<char, 8> kMagic = {'A', 'R', 'E', 'S', 'R', 'E', 'C', '1'};

enum class RecordType : std::uint16_t {
    MissionStart = 1,
    MissionEnd = 2,
    ModeChanged = 3,
    FaultActivated = 4,
    FaultUpdated = 5,
    FaultCleared = 6,
    RecoveryStarted = 7,
    RecoverySucceeded = 8,
    RecoveryFailed = 9,
    ChaosStarted = 10,
    ChaosEnded = 11,
    DeadlineMiss = 12,
    TaskGenerationChanged = 13,
    GpsSelectionChanged = 14,
};

enum class HeaderFlag : std::uint32_t {
    Finalized = 1U,
    Overflow = 2U,
    IoError = 4U,
};

[[nodiscard]] inline bool known_record_type(std::uint16_t type) noexcept {
    switch (static_cast<RecordType>(type)) {
    case RecordType::MissionStart:
    case RecordType::MissionEnd:
    case RecordType::ModeChanged:
    case RecordType::FaultActivated:
    case RecordType::FaultUpdated:
    case RecordType::FaultCleared:
    case RecordType::RecoveryStarted:
    case RecordType::RecoverySucceeded:
    case RecordType::RecoveryFailed:
    case RecordType::ChaosStarted:
    case RecordType::ChaosEnded:
    case RecordType::DeadlineMiss:
    case RecordType::TaskGenerationChanged:
    case RecordType::GpsSelectionChanged:
        return true;
    }
    return false;
}

[[nodiscard]] inline std::uint16_t payload_bytes(std::uint16_t type) noexcept {
    switch (static_cast<RecordType>(type)) {
    case RecordType::MissionStart:
        return 24;
    case RecordType::MissionEnd:
        return 32;
    case RecordType::ModeChanged:
    case RecordType::FaultActivated:
    case RecordType::FaultCleared:
    case RecordType::GpsSelectionChanged:
        return 4;
    case RecordType::FaultUpdated:
    case RecordType::RecoveryStarted:
    case RecordType::RecoverySucceeded:
    case RecordType::RecoveryFailed:
    case RecordType::DeadlineMiss:
    case RecordType::TaskGenerationChanged:
        return 8;
    case RecordType::ChaosStarted:
    case RecordType::ChaosEnded:
        return 16;
    }
    return 0;
}

inline void store_u16(std::uint8_t* destination, std::uint16_t value) noexcept {
    *destination = static_cast<std::uint8_t>(value & 0x00FFU);
    *(destination + 1) =
        static_cast<std::uint8_t>(static_cast<std::uint16_t>(value >> 8U) & 0x00FFU);
}

inline void store_u32(std::uint8_t* destination, std::uint32_t value) noexcept {
    *destination = static_cast<std::uint8_t>(value & 0x000000FFU);
    *(destination + 1) = static_cast<std::uint8_t>((value >> 8U) & 0x000000FFU);
    *(destination + 2) = static_cast<std::uint8_t>((value >> 16U) & 0x000000FFU);
    *(destination + 3) = static_cast<std::uint8_t>((value >> 24U) & 0x000000FFU);
}

inline void store_i32(std::uint8_t* destination, std::int32_t value) noexcept {
    store_u32(destination, static_cast<std::uint32_t>(value));
}

inline void store_i64(std::uint8_t* destination, std::int64_t value) noexcept {
    const auto bits = static_cast<std::uint64_t>(value);
    store_u32(destination, static_cast<std::uint32_t>(bits & 0xFFFFFFFFU));
    store_u32(destination + 4, static_cast<std::uint32_t>(bits >> 32U));
}

[[nodiscard]] inline std::uint16_t load_u16(const std::uint8_t* source) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(*source) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(*(source + 1)) << 8U));
}

[[nodiscard]] inline std::uint32_t load_u32(const std::uint8_t* source) noexcept {
    return static_cast<std::uint32_t>(*source) | (static_cast<std::uint32_t>(*(source + 1)) << 8U) |
           (static_cast<std::uint32_t>(*(source + 2)) << 16U) |
           (static_cast<std::uint32_t>(*(source + 3)) << 24U);
}

[[nodiscard]] inline std::int32_t load_i32(const std::uint8_t* source) noexcept {
    return static_cast<std::int32_t>(load_u32(source));
}

[[nodiscard]] inline std::int64_t load_i64(const std::uint8_t* source) noexcept {
    const auto low = static_cast<std::uint64_t>(load_u32(source));
    const auto high = static_cast<std::uint64_t>(load_u32(source + 4));
    return static_cast<std::int64_t>(low | (high << 32U));
}

// ISO-HDLC CRC-32. The digest of "123456789" is 0xCBF43926.
[[nodiscard]] inline std::uint32_t crc32(std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    const std::uint8_t* cursor = bytes.data();
    const std::uint8_t* const end = cursor + bytes.size();
    for (; cursor != end; ++cursor) {
        crc ^= *cursor;
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

[[nodiscard]] inline bool add_size(std::size_t left, std::size_t right, std::size_t& out) noexcept {
    if (left > (static_cast<std::size_t>(-1) - right)) {
        return false;
    }
    out = left + right;
    return true;
}

} // namespace ares::recorder
