#include "ares/core/periodic_task.hpp"
#include "ares/simulation/chaos_engine.hpp"
#include "ares/simulation/scenarios.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>

#include <gtest/gtest.h>

namespace simulation = ares::simulation;
using namespace std::chrono_literals;
using Clock = ares::core::ManualClock;
using Time = Clock::time_point;

namespace {

simulation::ChaosEvent freeze_at(ares::core::Duration start, ares::core::Duration duration) {
    return simulation::ChaosEvent{simulation::InjectionKind::SensorFreeze,
                                  simulation::ChaosTarget::Gps,
                                  start,
                                  duration,
                                  0,
                                  0};
}

} // namespace

TEST(ChaosEngine, ActivatesAtTheStartAndClearsAtTheEnd) {
    Clock clock;
    simulation::ChaosEngine<Clock> engine;
    const simulation::ChaosEvent event = freeze_at(5s, 3s);
    ASSERT_TRUE(engine.load(std::span{&event, 1}, Time{}));

    EXPECT_FALSE(engine.sensor_frozen(simulation::ChaosTarget::Gps, Time{5s - 1ns}));
    EXPECT_TRUE(engine.sensor_frozen(simulation::ChaosTarget::Gps, Time{5s}));
    EXPECT_TRUE(engine.sensor_frozen(simulation::ChaosTarget::Gps, Time{8s - 1ns}));
    EXPECT_FALSE(engine.sensor_frozen(simulation::ChaosTarget::Gps, Time{8s}));
    EXPECT_FALSE(engine.sensor_frozen(simulation::ChaosTarget::Imu, Time{6s}));
}

TEST(ChaosEngine, DoesNotActivateEarly) {
    simulation::ChaosEngine<Clock> engine;
    const simulation::ChaosEvent event = freeze_at(5s, 1s);
    ASSERT_TRUE(engine.load(std::span{&event, 1}, Time{}));
    EXPECT_EQ(engine.view_at(Time{0s}).gps_frozen, false);
    EXPECT_EQ(engine.view_at(Time{4999ms}).gps_frozen, false);
    EXPECT_EQ(engine.view_at(Time{5s}).gps_frozen, true);
}

TEST(ChaosEngine, OrdersEventsByTimeThenDeclaration) {
    const simulation::ChaosEvent events[] = {
        {simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 8s, 1s, 0, 0},
        {simulation::InjectionKind::SensorUnavailable, simulation::ChaosTarget::Imu, 5s, 1s, 0, 0},
        {simulation::InjectionKind::BatteryVoltageOverride, simulation::ChaosTarget::Battery, 5s,
         1s, 10800, 0},
    };
    simulation::ChaosEngine<Clock> engine;
    ASSERT_TRUE(engine.load(std::span{events}, Time{}));
    simulation::ChaosEdge edges[8];
    const simulation::EdgeCopyResult copied = engine.copy_edges(-1ns, 20s, edges, 8);
    ASSERT_EQ(copied.copied, 6U);
    EXPECT_FALSE(copied.truncated);
    EXPECT_EQ(edges[0].target, simulation::ChaosTarget::Imu);
    EXPECT_TRUE(edges[0].started);
    EXPECT_EQ(edges[1].target, simulation::ChaosTarget::Battery);
    EXPECT_TRUE(edges[1].started);
    EXPECT_EQ(edges[2].target, simulation::ChaosTarget::Imu);
    EXPECT_FALSE(edges[2].started);
    EXPECT_EQ(edges[3].target, simulation::ChaosTarget::Battery);
    EXPECT_FALSE(edges[3].started);
    EXPECT_EQ(edges[4].target, simulation::ChaosTarget::Gps);
    EXPECT_TRUE(edges[4].started);
}

TEST(ChaosEngine, SameTimestampKeepsDeclarationOrder) {
    const simulation::ChaosEvent events[] = {
        {simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 2s, 1s, 0, 0},
        {simulation::InjectionKind::SensorInvalid, simulation::ChaosTarget::Imu, 2s, 1s, 0, 0},
    };
    simulation::ChaosEngine<Clock> engine;
    ASSERT_TRUE(engine.load(std::span{events}, Time{}));
    simulation::ChaosEdge edges[4];
    const simulation::EdgeCopyResult copied = engine.copy_edges(-1ns, 2s, edges, 4);
    ASSERT_EQ(copied.copied, 2U);
    EXPECT_FALSE(copied.truncated);
    EXPECT_EQ(edges[0].target, simulation::ChaosTarget::Gps);
    EXPECT_EQ(edges[0].sequence, 0U);
    EXPECT_EQ(edges[1].target, simulation::ChaosTarget::Imu);
    EXPECT_EQ(edges[1].sequence, 1U);
    EXPECT_TRUE(edges[0].started);
    EXPECT_TRUE(edges[1].started);
}

TEST(ChaosEngine, ResetDropsEveryInjection) {
    simulation::ChaosEngine<Clock> engine;
    const simulation::ChaosEvent event = freeze_at(0s, 5s);
    ASSERT_TRUE(engine.load(std::span{&event, 1}, Time{}));
    EXPECT_TRUE(engine.view_at(Time{1s}).gps_frozen);
    engine.reset();
    EXPECT_EQ(engine.size(), 0U);
    EXPECT_FALSE(engine.view_at(Time{1s}).gps_frozen);
    EXPECT_EQ(engine.view_at(Time{1s}).navigation_delay, 0ns);
}

TEST(ChaosEngine, SameScheduleAndClockProduceTheSameView) {
    const simulation::ChaosEvent events[] = {
        {simulation::InjectionKind::SensorUnavailable, simulation::ChaosTarget::Gps, 1s, 2s, 0, 0},
        {simulation::InjectionKind::TaskExecutionDelay, simulation::ChaosTarget::NavigationTask, 1s,
         2s, 150000000, 0},
        {simulation::InjectionKind::BatteryVoltageOverride, simulation::ChaosTarget::Battery, 4s,
         1s, 10800, 0},
    };
    simulation::ChaosEngine<Clock> first;
    simulation::ChaosEngine<Clock> second;
    ASSERT_TRUE(first.load(std::span{events}, Time{10s}));
    ASSERT_TRUE(second.load(std::span{events}, Time{10s}));
    const Time samples[] = {Time{10s}, Time{11s}, Time{13s}, Time{14s}, Time{16s}};
    for (const Time sample : samples) {
        EXPECT_EQ(first.view_at(sample), second.view_at(sample));
    }
}

TEST(ChaosEngine, ZeroDurationNeverActivates) {
    simulation::ChaosEngine<Clock> engine;
    const simulation::ChaosEvent event = freeze_at(1s, 0s);
    ASSERT_TRUE(engine.load(std::span{&event, 1}, Time{}));
    EXPECT_FALSE(engine.sensor_frozen(simulation::ChaosTarget::Gps, Time{1s}));
}

TEST(ChaosEngine, NamedScenariosAreBounded) {
    EXPECT_NE(simulation::find_scenario("nominal"), nullptr);
    EXPECT_EQ(simulation::find_scenario("nominal")->count, 0U);
    EXPECT_EQ(simulation::find_scenario("missing"), nullptr);
    for (const simulation::NamedScenario& scenario : simulation::kScenarios) {
        EXPECT_LE(scenario.count, simulation::kChaosEventCapacity);
    }
    EXPECT_EQ(simulation::find_scenario("low_battery")->battery_baseline_mv, 12400);
    EXPECT_EQ(simulation::find_scenario("deadline_storm")->events[0].target,
              simulation::ChaosTarget::NavigationTask);
    const simulation::NamedScenario* restart = simulation::find_scenario("nav_restart");
    const simulation::NamedScenario* failed = simulation::find_scenario("restart_fail");
    ASSERT_NE(restart, nullptr);
    ASSERT_NE(failed, nullptr);
    EXPECT_EQ(restart->count, 1U);
    EXPECT_EQ(restart->events[0].duration, 1s);
    EXPECT_EQ(restart->events[0].parameter, 150000000);
    EXPECT_EQ(failed->events[0].duration, 30s);
    EXPECT_EQ(failed->events[0].parameter, 150000000);
    EXPECT_EQ(failed->events[0].target, simulation::ChaosTarget::NavigationTask);
}

TEST(ChaosEngine, CommunicationsDelayMissesThroughTheDeadlineMonitor) {
    Clock clock;
    simulation::ChaosEngine<Clock> engine;
    const simulation::ChaosEvent event{simulation::InjectionKind::TaskExecutionDelay,
                                       simulation::ChaosTarget::CommunicationsTask,
                                       0s,
                                       1s,
                                       150000000,
                                       0};
    ASSERT_TRUE(engine.load(std::span{&event, 1}, Time{}));
    const std::optional<ares::core::TaskId> id = ares::core::TaskId::make("comms");
    ASSERT_TRUE(id.has_value());
    ares::core::SimulatedExecution note;
    ares::core::PeriodicTask<Clock> task(
        *id, ares::core::TaskTiming{100ms, 100ms},
        [&](Time scheduled, std::stop_token) {
            note.extra =
                engine.execution_delay(simulation::ChaosTarget::CommunicationsTask, scheduled);
        },
        clock, {});
    task.bind_simulated_execution(&note);
    ASSERT_EQ(task.arm(Time{}), ares::core::ArmStatus::Armed);
    EXPECT_EQ(task.poll(), ares::core::PollResult::Ran);
    EXPECT_TRUE(task.deadline_record().missed);
    EXPECT_FALSE(task.deadline_record().unusable);
}

TEST(ChaosEngine, NavigationDelayIsOnTimeOutsideTheWindow) {
    Clock clock;
    simulation::ChaosEngine<Clock> engine;
    const simulation::ChaosEvent event{simulation::InjectionKind::TaskExecutionDelay,
                                       simulation::ChaosTarget::NavigationTask,
                                       5s,
                                       1s,
                                       150000000,
                                       0};
    ASSERT_TRUE(engine.load(std::span{&event, 1}, Time{}));
    const std::optional<ares::core::TaskId> id = ares::core::TaskId::make("navigation");
    ASSERT_TRUE(id.has_value());
    ares::core::SimulatedExecution note;
    ares::core::PeriodicTask<Clock> task(
        *id, ares::core::TaskTiming{100ms, 100ms},
        [&](Time scheduled, std::stop_token) {
            note.extra = engine.execution_delay(simulation::ChaosTarget::NavigationTask, scheduled);
        },
        clock, {});
    task.bind_simulated_execution(&note);
    ASSERT_EQ(task.arm(Time{}), ares::core::ArmStatus::Armed);
    EXPECT_EQ(task.poll(), ares::core::PollResult::Ran);
    EXPECT_FALSE(task.deadline_record().missed);
}

TEST(ChaosEngine, RejectsOverlappingInjectionsOnOneTarget) {
    const simulation::ChaosEvent conflicts[][2] = {
        {{simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 1s, 2s, 0, 0},
         {simulation::InjectionKind::SensorInvalid, simulation::ChaosTarget::Gps, 2s, 2s, 0, 0}},
        {{simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 1s, 2s, 0, 0},
         {simulation::InjectionKind::SensorUnavailable, simulation::ChaosTarget::Gps, 2s, 2s, 0,
          0}},
        {{simulation::InjectionKind::SensorInvalid, simulation::ChaosTarget::Gps, 1s, 2s, 0, 0},
         {simulation::InjectionKind::SensorUnavailable, simulation::ChaosTarget::Gps, 2s, 2s, 0,
          0}},
        {{simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 1s, 2s, 0, 0},
         {simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 2s, 2s, 0, 0}},
        {{simulation::InjectionKind::BatteryVoltageOverride, simulation::ChaosTarget::Battery, 1s,
          2s, 10800, 0},
         {simulation::InjectionKind::BatteryVoltageOverride, simulation::ChaosTarget::Battery, 2s,
          2s, 10900, 0}},
        {{simulation::InjectionKind::TaskExecutionDelay, simulation::ChaosTarget::NavigationTask,
          1s, 2s, 150000000, 0},
         {simulation::InjectionKind::TaskExecutionDelay, simulation::ChaosTarget::NavigationTask,
          2s, 2s, 150000000, 0}},
    };
    for (const auto& pair : conflicts) {
        simulation::ChaosEngine<Clock> engine;
        EXPECT_FALSE(engine.load(std::span{pair}, Time{}));
        EXPECT_EQ(engine.size(), 0U);
    }
}

TEST(ChaosEngine, AllowsOverlapOnDifferentTargets) {
    const simulation::ChaosEvent events[] = {
        {simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 1s, 2s, 0, 0},
        {simulation::InjectionKind::BatteryVoltageOverride, simulation::ChaosTarget::Battery, 1s,
         2s, 10800, 0},
        {simulation::InjectionKind::TaskExecutionDelay, simulation::ChaosTarget::NavigationTask, 1s,
         2s, 150000000, 0},
    };
    simulation::ChaosEngine<Clock> engine;
    ASSERT_TRUE(engine.load(std::span{events}, Time{}));
    const simulation::InjectionView view = engine.view_at(Time{1500ms});
    EXPECT_TRUE(view.gps_frozen);
    EXPECT_EQ(view.battery_millivolts, std::optional<std::int64_t>{10800});
    EXPECT_EQ(view.navigation_delay, 150ms);
}

TEST(ChaosEngine, FailedLoadKeepsThePreviousSchedule) {
    simulation::ChaosEngine<Clock> engine;
    const simulation::ChaosEvent kept = freeze_at(0s, 5s);
    ASSERT_TRUE(engine.load(std::span{&kept, 1}, Time{}));
    const simulation::ChaosEvent overlap[] = {
        {simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 1s, 3s, 0, 0},
        {simulation::InjectionKind::SensorInvalid, simulation::ChaosTarget::Gps, 2s, 3s, 0, 0},
    };
    EXPECT_FALSE(engine.load(std::span{overlap}, Time{9s}));
    EXPECT_EQ(engine.size(), 1U);
    EXPECT_TRUE(engine.view_at(Time{1s}).gps_frozen);
    EXPECT_FALSE(engine.sensor_invalid(simulation::ChaosTarget::Gps, Time{2s}));

    simulation::ChaosEvent too_many[simulation::kChaosEventCapacity + 1];
    for (std::size_t index = 0; index < simulation::kChaosEventCapacity + 1; ++index) {
        too_many[index] =
            freeze_at(ares::core::Duration{static_cast<ares::core::Duration::rep>(index) * 2}, 1ns);
    }
    EXPECT_FALSE(engine.load(std::span{too_many}, Time{}));
    EXPECT_EQ(engine.size(), 1U);
    EXPECT_TRUE(engine.view_at(Time{1s}).gps_frozen);
}

TEST(ChaosEngine, EdgeCopyReportsTruncationWithoutChangingInjection) {
    simulation::ChaosEngine<Clock> engine;
    const simulation::ChaosEvent event = freeze_at(1s, 1s);
    ASSERT_TRUE(engine.load(std::span{&event, 1}, Time{}));
    simulation::ChaosEdge exact[2];
    const simulation::EdgeCopyResult fitted = engine.copy_edges(-1ns, 5s, exact, 2);
    EXPECT_EQ(fitted.copied, 2U);
    EXPECT_FALSE(fitted.truncated);

    simulation::ChaosEdge small[1];
    const simulation::EdgeCopyResult truncated = engine.copy_edges(-1ns, 5s, small, 1);
    EXPECT_EQ(truncated.copied, 1U);
    EXPECT_TRUE(truncated.truncated);
    EXPECT_TRUE(engine.view_at(Time{1s}).gps_frozen);
    EXPECT_FALSE(engine.view_at(Time{2s}).gps_frozen);
}

TEST(ChaosEngine, GpsAndPrimaryGpsAreOneTargetAndBackupIsNot) {
    simulation::ChaosEngine<Clock> engine;
    const simulation::ChaosEvent overlap[] = {
        {simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 1s, 2s, 0, 0},
        {simulation::InjectionKind::SensorInvalid, simulation::ChaosTarget::PrimaryGps, 2s, 2s, 0,
         0},
    };
    EXPECT_FALSE(engine.load(std::span{overlap}, Time{}));
    EXPECT_EQ(engine.size(), 0U);

    const simulation::ChaosEvent separate[] = {
        {simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 1s, 2s, 0, 0},
        {simulation::InjectionKind::SensorInvalid, simulation::ChaosTarget::BackupGps, 1s, 2s, 0,
         0},
    };
    ASSERT_TRUE(engine.load(std::span{separate}, Time{}));
    EXPECT_TRUE(engine.sensor_frozen(simulation::ChaosTarget::PrimaryGps, Time{1s}));
    EXPECT_TRUE(engine.sensor_invalid(simulation::ChaosTarget::BackupGps, Time{1s}));
    EXPECT_FALSE(engine.sensor_invalid(simulation::ChaosTarget::Gps, Time{1s}));
}
