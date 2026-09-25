#include "ares/recorder/replay.hpp"

#include "ares/flight/fault.hpp"
#include "ares/flight/fault_events.hpp"
#include "ares/flight/mode.hpp"
#include "ares/flight/mode_machine.hpp"
#include "ares/simulation/chaos_engine.hpp"

#include <array>
#include <limits>
#include <string_view>

namespace ares::recorder {
namespace {

[[nodiscard]] bool magic_matches(const std::uint8_t* bytes) noexcept {
    for (std::size_t index = 0; index < 8; ++index) {
        if (*(bytes + index) != static_cast<std::uint8_t>(kMagic.at(index))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string_view record_name(std::uint16_t type) noexcept {
    switch (static_cast<RecordType>(type)) {
    case RecordType::MissionStart:
    case RecordType::MissionEnd:
        return "MISSION";
    case RecordType::ModeChanged:
        return "MODE";
    case RecordType::FaultActivated:
    case RecordType::FaultUpdated:
    case RecordType::FaultCleared:
    case RecordType::DeadlineMiss:
        return "FAULT";
    case RecordType::RecoveryStarted:
    case RecordType::RecoverySucceeded:
    case RecordType::RecoveryFailed:
        return "RECOVERY";
    case RecordType::ChaosStarted:
    case RecordType::ChaosEnded:
        return "CHAOS";
    case RecordType::TaskGenerationChanged:
        return "TASK";
    case RecordType::GpsSelectionChanged:
        return "GPS";
    }
    return "RECORD";
}

[[nodiscard]] std::string format_time(std::int64_t time_ns) {
    const bool negative = time_ns < 0;
    const std::uint64_t magnitude = negative ? static_cast<std::uint64_t>(-(time_ns + 1)) + 1U
                                             : static_cast<std::uint64_t>(time_ns);
    const std::uint64_t seconds = magnitude / 1000000000ULL;
    const std::uint64_t micros = (magnitude % 1000000000ULL) / 1000ULL;
    std::string text = negative ? "T-" : "T+";
    if (seconds < 10U) {
        text += '0';
    }
    text += std::to_string(seconds);
    text += '.';
    const std::string fraction = std::to_string(micros);
    for (std::size_t pad = fraction.size(); pad < 6U; ++pad) {
        text += '0';
    }
    text += fraction;
    return text;
}

[[nodiscard]] std::string describe(const ReplayRecord& record) {
    const std::uint8_t* payload = record.payload.data();
    switch (static_cast<RecordType>(record.type)) {
    case RecordType::MissionStart:
        return "start";
    case RecordType::MissionEnd:
        return "end";
    case RecordType::ModeChanged: {
        const auto from = static_cast<flight::SpacecraftMode>(payload[0]);
        const auto to = static_cast<flight::SpacecraftMode>(payload[1]);
        return std::string(flight::to_string(from)) + " -> " + std::string(flight::to_string(to));
    }
    case RecordType::FaultActivated:
    case RecordType::FaultUpdated:
    case RecordType::FaultCleared:
    case RecordType::DeadlineMiss: {
        const auto type = static_cast<flight::FaultType>(payload[0]);
        const auto source = static_cast<flight::FaultSource>(payload[1]);
        const auto severity = static_cast<flight::FaultSeverity>(payload[2]);
        std::string line =
            std::string(flight::to_string(source)) + " " + std::string(flight::to_string(type));
        if (record.type == static_cast<std::uint16_t>(RecordType::FaultCleared)) {
            line += " cleared";
        } else if (record.type == static_cast<std::uint16_t>(RecordType::FaultUpdated) ||
                   record.type == static_cast<std::uint16_t>(RecordType::DeadlineMiss)) {
            line += " severity=";
            line += flight::to_string(severity);
        } else {
            line += " activated severity=";
            line += flight::to_string(severity);
        }
        return line;
    }
    case RecordType::RecoveryStarted:
    case RecordType::RecoverySucceeded:
    case RecordType::RecoveryFailed: {
        const auto action = static_cast<flight::SubsystemAction>(payload[0]);
        const auto target = static_cast<flight::RecoveryTarget>(payload[1]);
        const char* action_name =
            action == flight::SubsystemAction::SwitchSensor ? "SwitchSensor" : "RestartTask";
        const char* target_name = "navigation";
        if (target == flight::RecoveryTarget::PrimaryGps) {
            target_name = "primary-gps";
        } else if (target == flight::RecoveryTarget::BackupGps) {
            target_name = "backup-gps";
        }
        const char* edge = "started";
        if (record.type == static_cast<std::uint16_t>(RecordType::RecoverySucceeded)) {
            edge = "succeeded";
        } else if (record.type == static_cast<std::uint16_t>(RecordType::RecoveryFailed)) {
            edge = "failed";
        }
        return std::string(action_name) + " " + target_name + " " + edge +
               " attempt=" + std::to_string(payload[2]);
    }
    case RecordType::ChaosStarted:
    case RecordType::ChaosEnded: {
        const auto kind = static_cast<simulation::InjectionKind>(payload[0]);
        const auto target = static_cast<simulation::ChaosTarget>(payload[1]);
        std::string line = std::string(simulation::chaos_target_name(target)) + " " +
                           std::string(simulation::injection_kind_name(kind));
        line += record.type == static_cast<std::uint16_t>(RecordType::ChaosStarted) ? " started"
                                                                                    : " ended";
        return line;
    }
    case RecordType::TaskGenerationChanged:
        return "generation " + std::to_string(load_u32(payload)) + " -> " +
               std::to_string(load_u32(payload + 4));
    case RecordType::GpsSelectionChanged:
        return std::string(payload[0] == 0 ? "primary" : "backup") +
               (payload[1] == 0 ? " selected" : " selected isolated");
    }
    return "record";
}

// Sequence is the file order. A later record may carry an earlier logical time.
// The span between MissionStart and MissionEnd must still fit in a signed duration.
[[nodiscard]] bool mission_duration(std::int64_t start, std::int64_t end,
                                    std::int64_t& duration) noexcept {
    if (end < start) {
        return false;
    }
    const auto span = static_cast<std::uint64_t>(end) - static_cast<std::uint64_t>(start);
    if (span > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return false;
    }
    duration = static_cast<std::int64_t>(span);
    return true;
}

[[nodiscard]] bool validate(ReplayReport& report) {
    if ((report.records.empty()) ||
        report.records.front().type != static_cast<std::uint16_t>(RecordType::MissionStart)) {
        report.error = "missing MissionStart";
        return false;
    }
    int mission_starts = 0;
    int mission_ends = 0;
    bool saw_sequence = false;
    std::uint32_t previous_sequence = 0;
    std::int64_t start_time = 0;
    std::int64_t end_time = 0;
    std::array<std::array<bool, 8>, 8> active{};
    std::array<std::array<bool, 3>, 2> recovery_open{};
    for (std::size_t index = 0; index < report.records.size(); ++index) {
        const ReplayRecord& record = report.records.at(index);
        if (saw_sequence && record.sequence <= previous_sequence) {
            report.error = "sequence regression";
            return false;
        }
        saw_sequence = true;
        previous_sequence = record.sequence;
        const auto type = static_cast<RecordType>(record.type);
        if (type == RecordType::MissionStart) {
            ++mission_starts;
            start_time = record.time_ns;
        } else if (type == RecordType::MissionEnd) {
            ++mission_ends;
            if (index + 1 != report.records.size()) {
                report.error = "MissionEnd is not last";
                return false;
            }
            report.final_mode = record.payload.at(0);
            report.exit_code = load_i32(record.payload.data() + 4);
            end_time = record.time_ns;
        } else if (type == RecordType::ModeChanged) {
            if (record.payload.at(0) >
                    static_cast<std::uint8_t>(flight::SpacecraftMode::Emergency) ||
                record.payload.at(1) >
                    static_cast<std::uint8_t>(flight::SpacecraftMode::Emergency)) {
                report.error = "unknown mode";
                return false;
            }
            const auto from = static_cast<flight::SpacecraftMode>(record.payload.at(0));
            const auto to = static_cast<flight::SpacecraftMode>(record.payload.at(1));
            if (!flight::is_legal_transition(from, to)) {
                report.error = "illegal mode transition";
                return false;
            }
        } else if (type == RecordType::FaultActivated || type == RecordType::FaultUpdated ||
                   type == RecordType::FaultCleared || type == RecordType::DeadlineMiss) {
            const std::uint8_t fault_type = record.payload.at(0);
            const std::uint8_t source = record.payload.at(1);
            if (fault_type > static_cast<std::uint8_t>(flight::FaultType::LowBattery) ||
                source > 7U) {
                report.error = "unknown fault identity";
                return false;
            }
            if (type == RecordType::DeadlineMiss) {
                continue;
            }
            if (type == RecordType::FaultActivated) {
                if (active.at(fault_type).at(source)) {
                    report.error = "fault activated while active";
                    return false;
                }
                active.at(fault_type).at(source) = true;
                ++report.activations;
            } else if (type == RecordType::FaultUpdated) {
                if (!active.at(fault_type).at(source)) {
                    report.error = "fault updated while inactive";
                    return false;
                }
            } else if (!active.at(fault_type).at(source)) {
                report.error = "fault cleared while inactive";
                return false;
            } else {
                active.at(fault_type).at(source) = false;
                ++report.clears;
            }
        } else if (type == RecordType::RecoveryStarted || type == RecordType::RecoverySucceeded ||
                   type == RecordType::RecoveryFailed) {
            const std::uint8_t action = record.payload.at(0);
            const std::uint8_t target = record.payload.at(1);
            if (action > static_cast<std::uint8_t>(flight::SubsystemAction::SwitchSensor) ||
                target > static_cast<std::uint8_t>(flight::RecoveryTarget::BackupGps)) {
                report.error = "unknown recovery identity";
                return false;
            }
            if (type == RecordType::RecoveryStarted) {
                recovery_open.at(action).at(target) = true;
                ++report.recovery_starts;
                if (action == static_cast<std::uint8_t>(flight::SubsystemAction::RestartTask)) {
                    ++report.task_restarts;
                }
            } else if (!recovery_open.at(action).at(target)) {
                report.error = "recovery result without start";
                return false;
            } else {
                recovery_open.at(action).at(target) = false;
                if (type == RecordType::RecoverySucceeded) {
                    ++report.recovery_successes;
                } else {
                    ++report.recovery_failures;
                }
            }
        } else if (type == RecordType::TaskGenerationChanged) {
            const std::uint32_t previous = load_u32(record.payload.data());
            const std::uint32_t generation = load_u32(record.payload.data() + 4);
            if (generation <= previous) {
                report.error = "generation regression";
                return false;
            }
        } else if (type == RecordType::GpsSelectionChanged && record.payload.at(0) == 1U) {
            ++report.gps_failovers;
        }
    }
    if (mission_starts != 1) {
        report.error = "MissionStart must occur once";
        return false;
    }
    if (mission_ends != 1) {
        report.error = "missing MissionEnd";
        return false;
    }
    if (end_time < start_time) {
        report.error = "MissionEnd precedes MissionStart";
        return false;
    }
    std::int64_t duration = 0;
    if (!mission_duration(start_time, end_time, duration)) {
        report.error = "mission duration overflow";
        return false;
    }
    report.duration_ns = duration;
    return true;
}

} // namespace

ReplayReport replay_bytes(std::span<const std::uint8_t> bytes) {
    ReplayReport report;
    if (bytes.empty()) {
        report.error = "empty file";
        return report;
    }
    if (bytes.size() < kHeaderBytes) {
        report.error = "truncated header";
        return report;
    }
    const std::uint8_t* const data = bytes.data();
    if (!magic_matches(data)) {
        report.error = "bad magic";
        return report;
    }
    report.major = load_u16(data + 8);
    report.minor = load_u16(data + 10);
    if (report.major != kFormatMajor || report.minor != kFormatMinor) {
        report.error = "unsupported version";
        return report;
    }
    if (load_u32(data + 12) != kEndianMarker) {
        report.error = "unexpected endianness";
        return report;
    }
    if (load_u16(data + 22) != kHeaderBytes) {
        report.error = "truncated header";
        return report;
    }
    const auto header = std::span<const std::uint8_t>(data, 60);
    if (crc32(header) != load_u32(data + 60)) {
        report.error = "checksum mismatch";
        return report;
    }
    report.seed = static_cast<std::uint64_t>(load_i64(data + 24));
    report.scenario.clear();
    for (std::size_t index = 0; index < kScenarioNameBytes; ++index) {
        const char character = static_cast<char>(*(data + 32 + index));
        if (character == '\0') {
            break;
        }
        report.scenario.push_back(character);
    }
    const std::uint32_t flags = load_u32(data + 48);
    report.overflow = (flags & static_cast<std::uint32_t>(HeaderFlag::Overflow)) != 0U;
    const std::uint32_t declared = load_u32(data + 52);
    if (declared > 1000000U) {
        report.error = "absurd record count";
        return report;
    }
    std::size_t offset = kHeaderBytes;
    for (std::uint32_t index = 0; index < declared; ++index) {
        std::size_t prefix_end = 0;
        if (!add_size(offset, kRecordPrefix, prefix_end) || prefix_end > bytes.size()) {
            report.error = "truncated record";
            return report;
        }
        const std::uint8_t* prefix = data + offset;
        const std::uint16_t type = load_u16(prefix);
        const std::uint16_t schema = load_u16(prefix + 2);
        const std::uint16_t payload_size = load_u16(prefix + 4);
        if (schema != kRecordSchema) {
            report.error = "unsupported record schema";
            return report;
        }
        if (!known_record_type(type)) {
            report.error = "unknown record type";
            return report;
        }
        if (payload_size != payload_bytes(type) || payload_size > kPayloadCapacity) {
            report.error = "invalid payload length";
            return report;
        }
        std::size_t body = 0;
        std::size_t record_end = 0;
        if (!add_size(kRecordPrefix, payload_size, body) ||
            !add_size(body, kRecordCrcBytes, body) || !add_size(offset, body, record_end) ||
            record_end > bytes.size()) {
            report.error = "truncated record";
            return report;
        }
        const auto covered = std::span<const std::uint8_t>(prefix, kRecordPrefix + payload_size);
        if (crc32(covered) != load_u32(prefix + kRecordPrefix + payload_size)) {
            report.error = "checksum mismatch";
            return report;
        }
        ReplayRecord record;
        record.type = type;
        record.payload_size = payload_size;
        record.time_ns = load_i64(prefix + 8);
        record.sequence = load_u32(prefix + 16);
        for (std::uint16_t byte = 0; byte < payload_size; ++byte) {
            record.payload.at(byte) = *(prefix + kRecordPrefix + byte);
        }
        report.records.push_back(record);
        offset = record_end;
    }
    if (offset != bytes.size()) {
        report.error = "trailing bytes";
        return report;
    }
    const auto payload = std::span<const std::uint8_t>(data + kHeaderBytes, offset - kHeaderBytes);
    if (crc32(payload) != load_u32(data + 56)) {
        report.error = "checksum mismatch";
        return report;
    }
    if ((flags & static_cast<std::uint32_t>(HeaderFlag::Finalized)) == 0U) {
        report.error = "incomplete recording";
        return report;
    }
    if ((flags & static_cast<std::uint32_t>(HeaderFlag::IoError)) != 0U) {
        report.error = "recording io error";
        return report;
    }
    if (!validate(report)) {
        return report;
    }
    report.ok = true;
    return report;
}

std::string format_timeline(const ReplayReport& report) {
    std::string text = "ARES Mission Replay\nFormat: " + std::to_string(report.major) + "." +
                       std::to_string(report.minor) + "\nSeed: " + std::to_string(report.seed) +
                       "\nScenario: " + report.scenario + "\n\n";
    for (const ReplayRecord& record : report.records) {
        text += format_time(record.time_ns);
        text += "  ";
        text += record_name(record.type);
        text += "\n";
        text += describe(record);
        text += "\n\n";
    }
    return text;
}

std::string format_summary(const ReplayReport& report) {
    std::string text;
    text += "records: " + std::to_string(report.records.size()) + "\n";
    text += "duration_ns: " + std::to_string(report.duration_ns) + "\n";
    if (report.final_mode <= static_cast<std::uint8_t>(flight::SpacecraftMode::Emergency)) {
        text += "final_mode: ";
        text += flight::to_string(static_cast<flight::SpacecraftMode>(report.final_mode));
        text += "\n";
    } else {
        text += "final_mode: invalid\n";
    }
    text += "fault_activations: " + std::to_string(report.activations) + "\n";
    text += "fault_clears: " + std::to_string(report.clears) + "\n";
    text += "recovery_starts: " + std::to_string(report.recovery_starts) + "\n";
    text += "recovery_successes: " + std::to_string(report.recovery_successes) + "\n";
    text += "recovery_failures: " + std::to_string(report.recovery_failures) + "\n";
    text += "task_restarts: " + std::to_string(report.task_restarts) + "\n";
    text += "gps_failovers: " + std::to_string(report.gps_failovers) + "\n";
    text += "exit_code: " + std::to_string(report.exit_code) + "\n";
    text += std::string("integrity: ") + (report.ok ? "ok" : "failed") + "\n";
    text += std::string("overflow: ") + (report.overflow ? "yes" : "no") + "\n";
    if (!report.ok) {
        text += "error: " + report.error + "\n";
    }
    return text;
}

bool recompute_checksums(std::span<std::uint8_t> bytes) {
    if (bytes.size() < kHeaderBytes) {
        return false;
    }
    std::uint8_t* data = bytes.data();
    const std::uint32_t declared = load_u32(data + 52);
    std::size_t offset = kHeaderBytes;
    for (std::uint32_t index = 0; index < declared; ++index) {
        std::size_t prefix_end = 0;
        if (!add_size(offset, kRecordPrefix, prefix_end) || prefix_end > bytes.size()) {
            return false;
        }
        std::uint8_t* prefix = data + offset;
        const std::uint16_t payload_size = load_u16(prefix + 4);
        std::size_t body = 0;
        std::size_t record_end = 0;
        if (payload_size > kPayloadCapacity || !add_size(kRecordPrefix, payload_size, body) ||
            !add_size(body, kRecordCrcBytes, body) || !add_size(offset, body, record_end) ||
            record_end > bytes.size()) {
            return false;
        }
        const auto covered = std::span<const std::uint8_t>(prefix, kRecordPrefix + payload_size);
        store_u32(prefix + kRecordPrefix + payload_size, crc32(covered));
        offset = record_end;
    }
    if (offset > bytes.size()) {
        return false;
    }
    const auto payload = std::span<const std::uint8_t>(data + kHeaderBytes, offset - kHeaderBytes);
    store_u32(data + 56, crc32(payload));
    store_u32(data + 60, 0);
    const auto header = std::span<const std::uint8_t>(data, 60);
    store_u32(data + 60, crc32(header));
    return true;
}

} // namespace ares::recorder
