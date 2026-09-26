#include "ares/core/event_log.hpp"
#include "ares/core/periodic_task.hpp"
#include "ares/flight/fault_mailbox.hpp"
#include "ares/flight/mode.hpp"
#include "ares/flight/fault_registry.hpp"
#include "ares/flight/fdir_limits.hpp"
#include "ares/flight/recovery.hpp"
#include "ares/flight/system_event.hpp"
#include "ares/recorder/flight_recorder.hpp"
#include "ares/recorder/format.hpp"
#include "ares/recorder/replay.hpp"
#include "ares/simulation/chaos_engine.hpp"

#include <chrono>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

using Time = ares::core::ManualClock::time_point;

} // namespace

TEST(ResourceBudget, FixedTablesStayWithinASmallCeiling) {
    EXPECT_EQ(ares::flight::limits::kFaultRegistryCapacity, 19U);
    EXPECT_EQ(ares::core::EventLog<ares::flight::SystemEvent<Time>>::capacity, 64U);
    EXPECT_EQ(ares::simulation::kChaosEventCapacity, 16U);
    EXPECT_EQ(ares::simulation::kChaosEdgeCapacity, 32U);
    EXPECT_EQ(ares::recorder::kFormatMajor, 1U);
    EXPECT_EQ(ares::recorder::kFormatMinor, 0U);
    EXPECT_LE(sizeof(ares::flight::FaultRegistry<Time, 19>), 16384U);
    EXPECT_LE(sizeof(ares::core::EventLog<ares::flight::SystemEvent<Time>>), 65536U);
    EXPECT_LE(sizeof(ares::core::BoundedLog<ares::core::DeadlineMissEvent<Time>, 32>), 16384U);
    EXPECT_LE(sizeof(ares::simulation::ChaosEngine<ares::core::ManualClock>), 16384U);
    EXPECT_LE(sizeof(ares::flight::FaultMailbox<ares::core::ManualClock>), 8192U);
    EXPECT_LE(sizeof(ares::flight::RecoveryManager<Time>), 1024U);
    EXPECT_GE(sizeof(ares::recorder::FlightRecorder<>), 8192U);
    EXPECT_LE(sizeof(ares::recorder::FlightRecorder<>), 65536U);
}

TEST(ResourceBudget, RegistryUpdatesStayInsideTheFixedTable) {
    ares::flight::FaultRegistry<Time, ares::flight::limits::kFaultRegistryCapacity> registry;
    const auto started = std::chrono::steady_clock::now();
    for (int step = 0; step < 10000; ++step) {
        (void)registry.raise(ares::flight::FaultType::DeadlineMiss,
                             ares::flight::FaultSource::NavigationTask,
                             ares::flight::FaultSeverity::Advisory, Time{});
    }
    EXPECT_LE(registry.occupied_count(), ares::flight::limits::kFaultRegistryCapacity);
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds{5});
}

TEST(ResourceBudget, FullRecordingReplaysWithoutGrowingPastCapacity) {
    ares::recorder::FlightRecorder<> recorder;
    recorder.arm();
    ASSERT_TRUE(recorder.record_mission_start(1, "nominal", 0));
    const auto started = std::chrono::steady_clock::now();
    while (recorder.size() + 1U < 256U) {
        ASSERT_TRUE(recorder.record_gps(0, false, static_cast<std::int64_t>(recorder.size())));
    }
    EXPECT_EQ(recorder.size(), 255U);
    EXPECT_FALSE(recorder.record_gps(0, false, 255));
    EXPECT_TRUE(recorder.overflowed());
    ASSERT_TRUE(
        recorder.seal(256, static_cast<std::uint8_t>(ares::flight::SpacecraftMode::Nominal), 0));
    EXPECT_EQ(recorder.size(), 256U);
    const ares::recorder::ReplayReport report = ares::recorder::replay_bytes(recorder.encode());
    EXPECT_TRUE(report.ok) << report.error;
    EXPECT_EQ(report.records.size(), 256U);
    EXPECT_TRUE(report.overflow);
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds{5});
}

TEST(ResourceBudget, SchedulerPollsStayBounded) {
    ares::core::ManualClock clock;
    const auto ident = ares::core::TaskId::make("nav");
    ASSERT_TRUE(ident.has_value());
    int runs = 0;
    ares::core::PeriodicTask<ares::core::ManualClock> task{
        *ident,
        ares::core::TaskTiming{std::chrono::milliseconds{100}, std::chrono::milliseconds{100}},
        [&runs](Time, std::stop_token) { ++runs; },
        clock,
        {}};
    ASSERT_EQ(task.arm(Time{std::chrono::milliseconds{100}}), ares::core::ArmStatus::Armed);
    ASSERT_EQ(clock.advance(std::chrono::milliseconds{100}), ares::core::AdvanceStatus::Applied);
    const auto started = std::chrono::steady_clock::now();
    int ran = 0;
    for (int step = 0; step < 20000; ++step) {
        if (task.poll() == ares::core::PollResult::Ran) {
            ++ran;
        }
        if (clock.advance(std::chrono::milliseconds{100}) != ares::core::AdvanceStatus::Applied) {
            break;
        }
    }
    EXPECT_EQ(ran, 20000);
    EXPECT_EQ(runs, 20000);
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds{5});
}
