#include "ares/recorder/flight_recorder.hpp"
#include "ares/recorder/replay.hpp"

#include <array>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::vector<std::uint8_t> valid_recording() {
    ares::recorder::FlightRecorder<> recorder;
    recorder.arm();
    recorder.set_mission(9, "nominal");
    EXPECT_TRUE(recorder.record_mission_start(9, "nominal", 0));
    std::array<std::uint8_t, ares::recorder::kPayloadCapacity> payload{};
    payload.at(0) = 2;
    payload.at(1) = 3;
    EXPECT_TRUE(recorder.append_record(ares::recorder::RecordType::ModeChanged, 1, payload, 4));
    EXPECT_TRUE(recorder.seal(2, 3, 0));
    return recorder.encode();
}

} // namespace

TEST(Replay, RejectsEmptyAndTruncatedHeader) {
    EXPECT_EQ(ares::recorder::replay_bytes({}).error, "empty file");
    const std::vector<std::uint8_t> short_header(10, 0);
    EXPECT_EQ(ares::recorder::replay_bytes(short_header).error, "truncated header");
}

TEST(Replay, RejectsBadMagicAndUnsupportedMajor) {
    std::vector<std::uint8_t> bytes = valid_recording();
    bytes.at(0) = 'X';
    ASSERT_TRUE(ares::recorder::recompute_checksums(bytes));
    EXPECT_EQ(ares::recorder::replay_bytes(bytes).error, "bad magic");

    bytes = valid_recording();
    ares::recorder::store_u16(bytes.data() + 8, 99);
    ASSERT_TRUE(ares::recorder::recompute_checksums(bytes));
    EXPECT_EQ(ares::recorder::replay_bytes(bytes).error, "unsupported version");
}

TEST(Replay, RejectsChecksumTruncationLengthSequenceAndTrailingBytes) {
    std::vector<std::uint8_t> bytes = valid_recording();
    bytes.at(ares::recorder::kHeaderBytes + ares::recorder::kRecordPrefix) ^= 0x01U;
    EXPECT_EQ(ares::recorder::replay_bytes(bytes).error, "checksum mismatch");

    bytes = valid_recording();
    bytes.resize(bytes.size() - 3);
    EXPECT_EQ(ares::recorder::replay_bytes(bytes).error, "truncated record");

    bytes = valid_recording();
    bytes.resize(20);
    EXPECT_EQ(ares::recorder::replay_bytes(bytes).error, "truncated header");

    bytes = valid_recording();
    ares::recorder::store_u16(bytes.data() + ares::recorder::kHeaderBytes + 4, 99);
    EXPECT_EQ(ares::recorder::replay_bytes(bytes).error, "invalid payload length");

    bytes = valid_recording();
    const std::uint32_t second = ares::recorder::load_u32(bytes.data() + 52);
    ASSERT_GE(second, 2U);
    std::size_t offset = ares::recorder::kHeaderBytes;
    const std::uint16_t first_payload = ares::recorder::load_u16(bytes.data() + offset + 4);
    offset += ares::recorder::kRecordPrefix + first_payload + ares::recorder::kRecordCrcBytes;
    ares::recorder::store_u32(bytes.data() + offset + 16, 0);
    ASSERT_TRUE(ares::recorder::recompute_checksums(bytes));
    EXPECT_EQ(ares::recorder::replay_bytes(bytes).error, "sequence regression");

    bytes = valid_recording();
    bytes.push_back(0xAB);
    EXPECT_EQ(ares::recorder::replay_bytes(bytes).error, "trailing bytes");
}

TEST(Replay, RejectsAMissingMissionEnd) {
    ares::recorder::FlightRecorder<> recorder;
    recorder.arm();
    recorder.set_mission(1, "nominal");
    ASSERT_TRUE(recorder.record_mission_start(1, "nominal", 0));
    std::vector<std::uint8_t> bytes = recorder.encode();
    ares::recorder::store_u32(bytes.data() + 48,
                              static_cast<std::uint32_t>(ares::recorder::HeaderFlag::Finalized));
    ASSERT_TRUE(ares::recorder::recompute_checksums(bytes));
    EXPECT_EQ(ares::recorder::replay_bytes(bytes).error, "missing MissionEnd");
}

TEST(Replay, RejectsAMissionEndThatPrecedesTheStart) {
    ares::recorder::FlightRecorder<> recorder;
    recorder.arm();
    ASSERT_TRUE(recorder.record_mission_start(1, "nominal", 10));
    ASSERT_TRUE(recorder.seal(4, 3, 0));
    const ares::recorder::ReplayReport report = ares::recorder::replay_bytes(recorder.encode());
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.error, "MissionEnd precedes MissionStart");
}

TEST(Replay, RejectsAClearWithNoActivation) {
    ares::recorder::FlightRecorder<> recorder;
    recorder.arm();
    ASSERT_TRUE(recorder.record_mission_start(1, "nominal", 0));
    std::array<std::uint8_t, ares::recorder::kPayloadCapacity> payload{};
    payload.at(0) = 3;
    payload.at(1) = 5;
    ASSERT_TRUE(recorder.append_record(ares::recorder::RecordType::FaultCleared, 1, payload, 4));
    ASSERT_TRUE(recorder.seal(2, 3, 0));
    const ares::recorder::ReplayReport report = ares::recorder::replay_bytes(recorder.encode());
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.error, "fault cleared while inactive");
}

TEST(Replay, TextIsStable) {
    const std::vector<std::uint8_t> bytes = valid_recording();
    const ares::recorder::ReplayReport report = ares::recorder::replay_bytes(bytes);
    ASSERT_TRUE(report.ok) << report.error;
    const std::string once =
        ares::recorder::format_timeline(report) + ares::recorder::format_summary(report);
    const std::string twice =
        ares::recorder::format_timeline(report) + ares::recorder::format_summary(report);
    EXPECT_EQ(once, twice);
    EXPECT_NE(once.find("T+00.000000"), std::string::npos);
    EXPECT_NE(once.find("Standby -> Nominal"), std::string::npos);
}
