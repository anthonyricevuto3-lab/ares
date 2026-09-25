#include "ares/flight/fault.hpp"
#include "ares/flight/mode.hpp"
#include "ares/recorder/flight_recorder.hpp"
#include "ares/recorder/replay.hpp"
#include "ares/simulation/chaos_engine.hpp"

#include <array>
#include <chrono>
#include <latch>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

TEST(FlightRecorder, Crc32MatchesTheKnownVector) {
    const std::string text = "123456789";
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    EXPECT_EQ(ares::recorder::crc32(bytes), 0xCBF43926U);
}

TEST(FlightRecorder, RoundTripPreservesFields) {
    ares::recorder::FlightRecorder<> recorder;
    recorder.arm();
    recorder.set_mission(42, "nominal");
    ASSERT_TRUE(recorder.record_mission_start(42, "nominal", 0));
    std::array<std::uint8_t, ares::recorder::kPayloadCapacity> payload{};
    payload.at(0) = 3;
    payload.at(1) = 4;
    ASSERT_TRUE(recorder.append_record(ares::recorder::RecordType::ModeChanged, 10, payload, 4));
    payload = {};
    payload.at(0) = 2;
    payload.at(1) = 1;
    payload.at(2) = 1;
    ASSERT_TRUE(recorder.append_record(ares::recorder::RecordType::FaultActivated, 20, payload, 4));
    ares::recorder::store_u32(payload.data() + 4, 3);
    ASSERT_TRUE(recorder.append_record(ares::recorder::RecordType::FaultUpdated, 30, payload, 8));
    payload = {};
    payload.at(0) = 2;
    payload.at(1) = 1;
    ASSERT_TRUE(recorder.append_record(ares::recorder::RecordType::FaultCleared, 40, payload, 4));
    payload = {};
    payload.at(0) = 0;
    payload.at(1) = 0;
    payload.at(2) = 1;
    ares::recorder::store_u32(payload.data() + 4, 1);
    ASSERT_TRUE(
        recorder.append_record(ares::recorder::RecordType::RecoveryStarted, 50, payload, 8));
    ASSERT_TRUE(
        recorder.append_record(ares::recorder::RecordType::RecoverySucceeded, 60, payload, 8));
    ares::simulation::ChaosEdge edge;
    edge.kind = ares::simulation::InjectionKind::SensorFreeze;
    edge.target = ares::simulation::ChaosTarget::Gps;
    edge.at = std::chrono::seconds{5};
    edge.started = true;
    edge.sequence = 7;
    ASSERT_TRUE(recorder.record_chaos(edge, 70));
    ASSERT_TRUE(recorder.record_generation(0, 1, 80));
    ASSERT_TRUE(recorder.record_gps(1, true, 90));
    payload = {};
    payload.at(0) = 3;
    payload.at(1) = 6;
    payload.at(2) = 2;
    ares::recorder::store_u32(payload.data() + 4, 5);
    ASSERT_TRUE(recorder.append_record(ares::recorder::RecordType::DeadlineMiss, 100, payload, 8));
    ASSERT_TRUE(recorder.seal(110, 3, 0));
    const std::vector<std::uint8_t> bytes = recorder.encode();
    const ares::recorder::ReplayReport report = ares::recorder::replay_bytes(bytes);
    ASSERT_TRUE(report.ok) << report.error;
    EXPECT_EQ(report.seed, 42U);
    EXPECT_EQ(report.scenario, "nominal");
    ASSERT_GE(report.records.size(), 11U);
    EXPECT_EQ(report.records.front().type,
              static_cast<std::uint16_t>(ares::recorder::RecordType::MissionStart));
    EXPECT_EQ(report.records.front().sequence, 0U);
    EXPECT_EQ(ares::recorder::load_i64(report.records.front().payload.data()), 42);
    const ares::recorder::ReplayRecord& mode = report.records.at(1);
    EXPECT_EQ(mode.payload[0], 3);
    EXPECT_EQ(mode.payload[1], 4);
    EXPECT_EQ(mode.time_ns, 10);
    EXPECT_EQ(report.records.back().type,
              static_cast<std::uint16_t>(ares::recorder::RecordType::MissionEnd));
    EXPECT_EQ(report.final_mode, 3);
    EXPECT_EQ(report.exit_code, 0);
    EXPECT_EQ(report.activations, 1U);
    EXPECT_EQ(report.clears, 1U);
    EXPECT_EQ(report.recovery_starts, 1U);
    EXPECT_EQ(report.recovery_successes, 1U);
    EXPECT_EQ(report.gps_failovers, 1U);
    EXPECT_EQ(report.task_restarts, 1U);
}

TEST(FlightRecorder, IdenticalSequencesAreByteIdentical) {
    const auto fill = [] {
        ares::recorder::FlightRecorder<> recorder;
        recorder.arm();
        recorder.set_mission(7, "gps_stale");
        EXPECT_TRUE(recorder.record_mission_start(7, "gps_stale", 5));
        EXPECT_TRUE(recorder.record_generation(0, 2, 15));
        EXPECT_TRUE(recorder.seal(25, 4, 0));
        return recorder.encode();
    };
    EXPECT_EQ(fill(), fill());
}

TEST(FlightRecorder, OverflowKeepsThePrefixAndDoesNotWrap) {
    ares::recorder::FlightRecorder<4, 100> recorder;
    recorder.arm();
    std::array<std::uint8_t, ares::recorder::kPayloadCapacity> payload{};
    payload.at(0) = 3;
    payload.at(1) = 4;
    ASSERT_TRUE(recorder.record_mission_start(1, "nominal", 0));
    ASSERT_TRUE(recorder.append_record(ares::recorder::RecordType::ModeChanged, 1, payload, 4));
    ASSERT_TRUE(recorder.append_record(ares::recorder::RecordType::ModeChanged, 2, payload, 4));
    EXPECT_FALSE(recorder.append_record(ares::recorder::RecordType::ModeChanged, 3, payload, 4));
    EXPECT_TRUE(recorder.overflowed());
    ASSERT_TRUE(recorder.seal(4, 3, 0));
    const ares::recorder::ReplayReport report = ares::recorder::replay_bytes(recorder.encode());
    ASSERT_TRUE(report.ok) << report.error;
    EXPECT_TRUE(report.overflow);
    EXPECT_EQ(report.records.front().sequence, 0U);
    EXPECT_EQ(report.records.at(1).time_ns, 1);
    EXPECT_EQ(report.records.back().type,
              static_cast<std::uint16_t>(ares::recorder::RecordType::MissionEnd));
}

TEST(FlightRecorder, SequenceLimitDoesNotWrap) {
    ares::recorder::FlightRecorder<16, 2> recorder;
    recorder.arm();
    ASSERT_TRUE(recorder.record_mission_start(1, "nominal", 0));
    ASSERT_TRUE(recorder.record_gps(0, false, 1));
    EXPECT_FALSE(recorder.record_gps(1, true, 2));
    EXPECT_TRUE(recorder.overflowed());
    EXPECT_FALSE(
        recorder.seal(3, static_cast<std::uint8_t>(ares::flight::SpacecraftMode::Standby), 0));
    EXPECT_EQ(recorder.size(), 2U);
    const std::vector<std::uint8_t> bytes = recorder.encode();
    ASSERT_GE(bytes.size(), ares::recorder::kHeaderBytes + ares::recorder::kRecordPrefix);
    EXPECT_EQ(ares::recorder::load_u32(bytes.data() + ares::recorder::kHeaderBytes + 16), 0U);
    const std::size_t second = ares::recorder::kHeaderBytes + ares::recorder::kRecordPrefix + 24 +
                               ares::recorder::kRecordCrcBytes;
    ASSERT_GT(bytes.size(), second + 16);
    EXPECT_EQ(ares::recorder::load_u32(bytes.data() + second + 16), 1U);
}

TEST(FlightRecorder, AppendDoesNotShiftThePrefix) {
    ares::recorder::FlightRecorder<> recorder;
    recorder.arm();
    ASSERT_TRUE(recorder.record_mission_start(1, "nominal", 0));
    for (int index = 0; index < 49; ++index) {
        ASSERT_TRUE(recorder.record_gps(0, false, index));
    }
    ASSERT_TRUE(recorder.seal(50, 3, 0));
    const ares::recorder::ReplayReport report = ares::recorder::replay_bytes(recorder.encode());
    ASSERT_TRUE(report.ok) << report.error;
    EXPECT_EQ(report.records.front().sequence, 0U);
    EXPECT_EQ(report.records.front().time_ns, 0);
    EXPECT_EQ(report.records.at(49).sequence, 49U);
}

TEST(FlightRecorder, WriterFailureIsContained) {
    ares::recorder::FlightRecorder<> recorder;
    recorder.arm();
    ASSERT_TRUE(recorder.record_mission_start(1, "nominal", 0));
    ASSERT_TRUE(recorder.seal(1, 3, 0));
    const std::vector<std::uint8_t> encoded = recorder.encode();
    const ares::recorder::ReplayReport before = ares::recorder::replay_bytes(encoded);
    ASSERT_TRUE(before.ok) << before.error;
    EXPECT_EQ(before.exit_code, 0);
    EXPECT_FALSE(before.overflow);
    recorder.fail_commits();
    int mode = 3;
    EXPECT_FALSE(recorder.commit());
    EXPECT_TRUE(recorder.io_error());
    EXPECT_EQ(mode, 3);
    const ares::recorder::ReplayReport still = ares::recorder::replay_bytes(encoded);
    ASSERT_TRUE(still.ok) << still.error;
    EXPECT_EQ(still.exit_code, 0);
}

TEST(FlightRecorder, ConcurrentAppendKeepsSequenceWhenTimeMovesBackward) {
    ares::recorder::FlightRecorder<> recorder;
    recorder.arm();
    recorder.set_mission(1, "nominal");
    ASSERT_TRUE(recorder.record_mission_start(1, "nominal", 0));
    std::array<std::uint8_t, ares::recorder::kPayloadCapacity> mode{};
    mode.at(0) = static_cast<std::uint8_t>(ares::flight::SpacecraftMode::Nominal);
    mode.at(1) = static_cast<std::uint8_t>(ares::flight::SpacecraftMode::Degraded);
    std::latch health_appended{1};
    bool health_ok = false;
    bool navigation_ok = false;
    std::thread health([&] {
        health_ok = recorder.append_record(ares::recorder::RecordType::ModeChanged, 200, mode, 4);
        health_appended.count_down();
    });
    std::thread navigation([&] {
        health_appended.wait();
        navigation_ok = recorder.record_generation(0, 1, 100);
    });
    health.join();
    navigation.join();
    ASSERT_TRUE(health_ok);
    ASSERT_TRUE(navigation_ok);
    ASSERT_TRUE(
        recorder.seal(150, static_cast<std::uint8_t>(ares::flight::SpacecraftMode::Degraded), 0));
    const ares::recorder::ReplayReport report = ares::recorder::replay_bytes(recorder.encode());
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_EQ(report.records.size(), 4U);
    EXPECT_EQ(report.records.at(1).type,
              static_cast<std::uint16_t>(ares::recorder::RecordType::ModeChanged));
    EXPECT_EQ(report.records.at(1).time_ns, 200);
    EXPECT_EQ(report.records.at(1).sequence, 1U);
    EXPECT_EQ(report.records.at(2).type,
              static_cast<std::uint16_t>(ares::recorder::RecordType::TaskGenerationChanged));
    EXPECT_EQ(report.records.at(2).time_ns, 100);
    EXPECT_EQ(report.records.at(2).sequence, 2U);
    EXPECT_EQ(report.duration_ns, 150);
    const std::string timeline = ares::recorder::format_timeline(report);
    const auto mode_at = timeline.find("Nominal -> Degraded");
    const auto generation_at = timeline.find("generation 0 -> 1");
    ASSERT_NE(mode_at, std::string::npos);
    ASSERT_NE(generation_at, std::string::npos);
    EXPECT_LT(mode_at, generation_at);
}

TEST(FlightRecorder, HealthSampleThenEarlierGenerationReplayIsValid) {
    ares::recorder::FlightRecorder<> recorder;
    recorder.arm();
    recorder.set_mission(1, "restart-ok");
    constexpr std::int64_t kScheduled = 5200000000;
    constexpr std::int64_t kHealth = kScheduled + 50000000;
    ASSERT_TRUE(recorder.record_mission_start(1, "restart-ok", 0));
    std::array<std::uint8_t, ares::recorder::kPayloadCapacity> fault{};
    fault.at(0) = static_cast<std::uint8_t>(ares::flight::FaultType::DeadlineMiss);
    fault.at(1) = static_cast<std::uint8_t>(ares::flight::FaultSource::NavigationTask);
    fault.at(2) = static_cast<std::uint8_t>(ares::flight::FaultSeverity::Critical);
    ASSERT_TRUE(
        recorder.append_record(ares::recorder::RecordType::FaultActivated, kScheduled, fault, 4));
    std::latch health_appended{1};
    bool health_ok = false;
    bool navigation_ok = false;
    std::thread health([&] {
        ares::recorder::store_u32(fault.data() + 4, 5U);
        health_ok =
            recorder.append_record(ares::recorder::RecordType::FaultUpdated, kHealth, fault, 8);
        health_appended.count_down();
    });
    std::thread navigation([&] {
        health_appended.wait();
        navigation_ok = recorder.record_generation(0, 1, kScheduled);
    });
    health.join();
    navigation.join();
    ASSERT_TRUE(health_ok);
    ASSERT_TRUE(navigation_ok);
    ASSERT_TRUE(recorder.seal(
        kHealth, static_cast<std::uint8_t>(ares::flight::SpacecraftMode::SafeMode), 0));
    const ares::recorder::ReplayReport report = ares::recorder::replay_bytes(recorder.encode());
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_GE(report.records.size(), 4U);
    EXPECT_EQ(report.records.at(2).type,
              static_cast<std::uint16_t>(ares::recorder::RecordType::FaultUpdated));
    EXPECT_EQ(report.records.at(2).time_ns, kHealth);
    EXPECT_EQ(report.records.at(3).type,
              static_cast<std::uint16_t>(ares::recorder::RecordType::TaskGenerationChanged));
    EXPECT_EQ(report.records.at(3).time_ns, kScheduled);
    EXPECT_LT(report.records.at(3).time_ns, report.records.at(2).time_ns);
    EXPECT_EQ(report.duration_ns, kHealth);
}

TEST(FlightRecorder, OverflowPrefixMayOmitALaterClearAndRecoveryResult) {
    ares::recorder::FlightRecorder<4, 100> recorder;
    recorder.arm();
    ASSERT_TRUE(recorder.record_mission_start(1, "nominal", 0));
    std::array<std::uint8_t, ares::recorder::kPayloadCapacity> fault{};
    fault.at(0) = static_cast<std::uint8_t>(ares::flight::FaultType::DeadlineMiss);
    fault.at(1) = static_cast<std::uint8_t>(ares::flight::FaultSource::NavigationTask);
    fault.at(2) = static_cast<std::uint8_t>(ares::flight::FaultSeverity::Critical);
    ASSERT_TRUE(recorder.append_record(ares::recorder::RecordType::FaultActivated, 10, fault, 4));
    fault = {};
    fault.at(0) = static_cast<std::uint8_t>(ares::flight::SubsystemAction::RestartTask);
    fault.at(1) = static_cast<std::uint8_t>(ares::flight::RecoveryTarget::NavigationTask);
    fault.at(2) = 1;
    ares::recorder::store_u32(fault.data() + 4, 1U);
    ASSERT_TRUE(recorder.append_record(ares::recorder::RecordType::RecoveryStarted, 20, fault, 8));
    EXPECT_FALSE(recorder.append_record(ares::recorder::RecordType::FaultCleared, 30, fault, 4));
    EXPECT_TRUE(recorder.overflowed());
    ASSERT_TRUE(
        recorder.seal(40, static_cast<std::uint8_t>(ares::flight::SpacecraftMode::SafeMode), 0));
    const ares::recorder::ReplayReport report = ares::recorder::replay_bytes(recorder.encode());
    ASSERT_TRUE(report.ok) << report.error;
    EXPECT_TRUE(report.overflow);
    EXPECT_EQ(report.activations, 1U);
    EXPECT_EQ(report.clears, 0U);
    EXPECT_EQ(report.recovery_starts, 1U);
    EXPECT_EQ(report.recovery_successes, 0U);
    EXPECT_EQ(report.recovery_failures, 0U);
}

TEST(FlightRecorder, RecoveryNoticesStayDistinct) {
    ares::recorder::FlightRecorder<> recorder;
    recorder.arm();
    ASSERT_TRUE(recorder.record_mission_start(1, "nominal", 0));
    std::array<std::uint8_t, ares::recorder::kPayloadCapacity> payload{};
    payload.at(0) = static_cast<std::uint8_t>(ares::flight::SubsystemAction::RestartTask);
    payload.at(1) = static_cast<std::uint8_t>(ares::flight::RecoveryTarget::NavigationTask);
    payload.at(2) = 1;
    ares::recorder::store_u32(payload.data() + 4, 1U);
    ASSERT_TRUE(
        recorder.append_record(ares::recorder::RecordType::RecoveryStarted, 10, payload, 8));
    ASSERT_TRUE(recorder.append_record(ares::recorder::RecordType::RecoveryFailed, 20, payload, 8));
    payload.at(0) = static_cast<std::uint8_t>(ares::flight::SubsystemAction::SwitchSensor);
    payload.at(1) = static_cast<std::uint8_t>(ares::flight::RecoveryTarget::BackupGps);
    ASSERT_TRUE(
        recorder.append_record(ares::recorder::RecordType::RecoveryStarted, 30, payload, 8));
    ASSERT_TRUE(
        recorder.append_record(ares::recorder::RecordType::RecoverySucceeded, 40, payload, 8));
    ASSERT_TRUE(
        recorder.seal(50, static_cast<std::uint8_t>(ares::flight::SpacecraftMode::Nominal), 0));
    const ares::recorder::ReplayReport report = ares::recorder::replay_bytes(recorder.encode());
    ASSERT_TRUE(report.ok) << report.error;
    EXPECT_EQ(report.records.at(1).type,
              static_cast<std::uint16_t>(ares::recorder::RecordType::RecoveryStarted));
    EXPECT_EQ(report.records.at(2).type,
              static_cast<std::uint16_t>(ares::recorder::RecordType::RecoveryFailed));
    EXPECT_EQ(report.records.at(3).type,
              static_cast<std::uint16_t>(ares::recorder::RecordType::RecoveryStarted));
    EXPECT_EQ(report.records.at(4).type,
              static_cast<std::uint16_t>(ares::recorder::RecordType::RecoverySucceeded));
    EXPECT_EQ(report.recovery_failures, 1U);
    EXPECT_EQ(report.recovery_successes, 1U);
    EXPECT_EQ(report.task_restarts, 1U);
    EXPECT_EQ(report.recovery_starts, 2U);
}
