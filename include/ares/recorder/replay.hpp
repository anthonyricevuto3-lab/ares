#pragma once

#include "ares/recorder/format.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ares::recorder {

struct ReplayRecord {
    std::uint16_t type{0};
    std::uint16_t payload_size{0};
    std::int64_t time_ns{0};
    std::uint32_t sequence{0};
    std::array<std::uint8_t, kPayloadCapacity> payload{};
};

struct ReplayReport {
    bool ok{false};
    bool overflow{false};
    std::string error{};
    std::string scenario{};
    std::uint64_t seed{0};
    std::uint16_t major{0};
    std::uint16_t minor{0};
    std::int64_t duration_ns{0};
    std::uint8_t final_mode{0};
    std::int32_t exit_code{0};
    std::uint32_t activations{0};
    std::uint32_t clears{0};
    std::uint32_t recovery_starts{0};
    std::uint32_t recovery_successes{0};
    std::uint32_t recovery_failures{0};
    std::uint32_t task_restarts{0};
    std::uint32_t gps_failovers{0};
    std::vector<ReplayRecord> records{};
};

[[nodiscard]] ReplayReport replay_bytes(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::string format_timeline(const ReplayReport& report);
[[nodiscard]] std::string format_summary(const ReplayReport& report);
[[nodiscard]] bool recompute_checksums(std::span<std::uint8_t> bytes);

} // namespace ares::recorder
