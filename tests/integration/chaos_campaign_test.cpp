#include "ares/application.hpp"
#include "ares/core/logger.hpp"
#include "ares/flight/example_tasks.hpp"
#include "ares/flight/fdir.hpp"
#include "ares/flight/power_manager.hpp"
#include "ares/flight/sample_limits.hpp"
#include "ares/flight/thermal_monitor.hpp"
#include "ares/simulation/chaos_engine.hpp"
#include "ares/simulation/scenarios.hpp"
#include "ares/simulation/sensors.hpp"

#include <chrono>
#include <stdexcept>
#include <optional>
#include <span>
#include <sstream>
#include <stop_token>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace flight = ares::flight;
namespace hardware = ares::hardware;
namespace simulation = ares::simulation;
using namespace std::chrono_literals;
using Clock = ares::core::ManualClock;
using Time = Clock::time_point;

namespace {

struct Mark {
    enum class Kind : std::uint8_t { Mode, Activated, Cleared };
    Kind kind{Kind::Mode};
    flight::SpacecraftMode from{flight::SpacecraftMode::Boot};
    flight::SpacecraftMode to{flight::SpacecraftMode::Boot};
    flight::FaultType type{flight::FaultType::SensorStale};
    flight::FaultSource source{flight::FaultSource::Gps};
    Time time{};

    bool operator==(const Mark&) const = default;
};

std::optional<flight::FaultSource> source_for(std::string_view name) {
    if (name == flight::NavigationCadence<Clock>::name) {
        return flight::FaultSource::NavigationTask;
    }
    if (name == flight::HealthPulse<Clock>::name) {
        return flight::FaultSource::HealthTask;
    }
    if (name == flight::CommBeacon<Clock>::name) {
        return flight::FaultSource::CommunicationsTask;
    }
    return std::nullopt;
}

struct Campaign {
    Clock clock{};
    std::ostringstream out{};
    ares::core::Logger<Clock> logger;
    ares::core::EventLog<flight::SystemEvent<Time>> events{};
    simulation::ChaosEngine<Clock> chaos{};
    simulation::SpacecraftModel<Clock> model;
    simulation::SimulatedImu<Clock> imu;
    simulation::SimulatedGps<Clock> gps;
    simulation::SimulatedBatteryMonitor<Clock> battery;
    simulation::SimulatedTemperatureSensor<Clock> temperature;
    flight::FlightExecutive<Clock> executive;
    flight::HealthPulse<Clock> health;
    flight::NavigationCadence<Clock> navigation;
    flight::CommBeacon<Clock> comms;
    flight::PowerManager<Clock> power;
    flight::ThermalMonitor<Clock> thermal;
    flight::FaultMailbox<Clock> mailbox{};
    flight::FdirController<Clock> fdir;
    ares::core::SimulatedExecution navigation_execution{};
    ares::core::SimulatedExecution communications_execution{};
    std::optional<ares::core::PeriodicTask<Clock>> navigation_task{};
    std::optional<ares::core::PeriodicTask<Clock>> health_task{};
    std::optional<ares::core::PeriodicTask<Clock>> comms_task{};

    explicit Campaign(const simulation::NamedScenario& scenario)
        : logger(out, clock), model(clock), imu(model, {}, &chaos), gps(model, {}, &chaos),
          battery(model, {}, &chaos), temperature(model), executive(clock, logger, events),
          health(logger),
          navigation(logger, imu, gps, clock,
                     flight::NavigationAgeLimits{flight::limits::kNavigationImuMaxAge,
                                                 flight::limits::kNavigationGpsMaxAge}),
          comms(logger), power(battery, clock, flight::limits::kPowerMaxAge),
          thermal(temperature, clock, flight::limits::kThermalMaxAge), fdir(events) {
        simulation::SimulationTruth<Clock> truth;
        truth.epoch = Time{};
        truth.voltage = hardware::Millivolts{
            scenario.battery_baseline_mv > 0 ? scenario.battery_baseline_mv : 12000};
        model.set_truth(truth);
        const std::span<const simulation::ChaosEvent> schedule{scenario.events, scenario.count};
        if (!chaos.load(schedule, Time{}) ||
            executive.boot_to_standby().status != flight::TransitionStatus::Accepted ||
            executive.accept(flight::Command::StartMission) != flight::CommandStatus::Accepted) {
            throw std::logic_error("campaign failed to boot");
        }

        const auto make_hooks = [this] {
            return ares::core::TaskHooks<Clock>{
                [this](const ares::core::TaskCycleEvent<Time>& event) {
                    if (const std::optional<flight::FaultSource> source =
                            source_for(event.id.text())) {
                        mailbox.publish_deadline(*source, event.deadline_missed);
                    }
                },
                {},
            };
        };
        const auto navigation_id = ares::core::TaskId::make(flight::NavigationCadence<Clock>::name);
        const auto health_id = ares::core::TaskId::make(flight::HealthPulse<Clock>::name);
        const auto comms_id = ares::core::TaskId::make(flight::CommBeacon<Clock>::name);
        if (!navigation_id.has_value() || !health_id.has_value() || !comms_id.has_value()) {
            throw std::logic_error("campaign task name was rejected");
        }
        navigation_task.emplace(
            *navigation_id, ares::core::TaskTiming{100ms, 100ms},
            [this](Time scheduled, std::stop_token stop) {
                navigation(scheduled, stop);
                if (!stop.stop_requested()) {
                    mailbox.publish_sensor(flight::FaultSource::Imu,
                                           navigation.solution().imu_usability);
                    mailbox.publish_sensor(flight::FaultSource::Gps,
                                           navigation.solution().gps_usability);
                }
                navigation_execution.extra =
                    chaos.execution_delay(simulation::ChaosTarget::NavigationTask, scheduled);
            },
            clock, make_hooks());
        health_task.emplace(
            *health_id, ares::core::TaskTiming{200ms, 200ms},
            [this](Time scheduled, std::stop_token stop) {
                const ares::core::ClockSample<Time> now = clock.now();
                if (now.status == ares::core::ClockStatus::Ok) {
                    chaos.note(logger, now.time);
                }
                flight::run_health_cycle(power, thermal, health, scheduled, stop);
                if (stop.stop_requested()) {
                    return;
                }
                mailbox.publish_sensor(flight::FaultSource::Temperature, thermal.usability());
                mailbox.publish_battery(power.usability(), power.latest_observation().voltage);
                (void)flight::run_fdir_cycle(fdir, mailbox, executive, clock.now(), stop);
            },
            clock, make_hooks());
        comms_task.emplace(
            *comms_id, ares::core::TaskTiming{400ms, 400ms},
            [this](Time scheduled, std::stop_token stop) {
                comms(scheduled, stop);
                communications_execution.extra =
                    chaos.execution_delay(simulation::ChaosTarget::CommunicationsTask, scheduled);
            },
            clock, make_hooks());
        navigation_task->bind_simulated_execution(&navigation_execution);
        comms_task->bind_simulated_execution(&communications_execution);
        if (navigation_task->arm(Time{}) != ares::core::ArmStatus::Armed ||
            health_task->arm(Time{}) != ares::core::ArmStatus::Armed ||
            comms_task->arm(Time{}) != ares::core::ArmStatus::Armed) {
            throw std::logic_error("campaign tasks did not arm");
        }
        poll_ready();
    }

    void poll_ready() {
        for (int guard = 0; guard < 8; ++guard) {
            const ares::core::PollResult navigation_result = navigation_task->poll();
            const ares::core::PollResult health_result = health_task->poll();
            const ares::core::PollResult comms_result = comms_task->poll();
            if (navigation_result != ares::core::PollResult::Ran &&
                health_result != ares::core::PollResult::Ran &&
                comms_result != ares::core::PollResult::Ran) {
                break;
            }
        }
    }

    void run_for(ares::core::Duration total) {
        const auto ticks = total / 100ms;
        for (ares::core::Duration::rep tick = 0; tick < ticks; ++tick) {
            ASSERT_EQ(clock.advance(100ms), ares::core::AdvanceStatus::Applied);
            poll_ready();
        }
    }

    [[nodiscard]] std::vector<Mark> trace() const {
        std::vector<Mark> marks;
        for (const flight::SystemEvent<Time>& event : events.snapshot()) {
            if (const auto* mode = std::get_if<flight::ModeChangedEvent<Time>>(&event)) {
                marks.push_back(Mark{Mark::Kind::Mode, mode->from, mode->to, {}, {}, mode->time});
            } else if (const auto* activated =
                           std::get_if<flight::FaultActivatedEvent<Time>>(&event)) {
                marks.push_back(Mark{Mark::Kind::Activated,
                                     {},
                                     {},
                                     activated->type,
                                     activated->source,
                                     activated->time});
            } else if (const auto* cleared = std::get_if<flight::FaultClearedEvent<Time>>(&event)) {
                marks.push_back(Mark{
                    Mark::Kind::Cleared, {}, {}, cleared->type, cleared->source, cleared->time});
            }
        }
        return marks;
    }
};

[[nodiscard]] bool saw_mode(const std::vector<Mark>& marks, flight::SpacecraftMode from,
                            flight::SpacecraftMode to) {
    for (const Mark& mark : marks) {
        if (mark.kind == Mark::Kind::Mode && mark.from == from && mark.to == to) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool saw_fault(const std::vector<Mark>& marks, Mark::Kind kind,
                             flight::FaultType type, flight::FaultSource source) {
    for (const Mark& mark : marks) {
        if (mark.kind == kind && mark.type == type && mark.source == source) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(ChaosCampaign, GpsStaleDegradesThenReturnsToNominal) {
    Campaign campaign(*simulation::find_scenario("gps_stale"));
    campaign.run_for(10s);
    const std::vector<Mark> marks = campaign.trace();
    EXPECT_TRUE(saw_fault(marks, Mark::Kind::Activated, flight::FaultType::SensorStale,
                          flight::FaultSource::Gps));
    EXPECT_TRUE(saw_mode(marks, flight::SpacecraftMode::Nominal, flight::SpacecraftMode::Degraded));
    EXPECT_TRUE(saw_fault(marks, Mark::Kind::Cleared, flight::FaultType::SensorStale,
                          flight::FaultSource::Gps));
    EXPECT_TRUE(saw_mode(marks, flight::SpacecraftMode::Degraded, flight::SpacecraftMode::Nominal));
    EXPECT_EQ(campaign.executive.mode(), flight::SpacecraftMode::Nominal);
    const flight::FaultRecord<Time>* stale =
        campaign.fdir.registry().find(flight::FaultType::SensorStale, flight::FaultSource::Gps);
    ASSERT_NE(stale, nullptr);
    EXPECT_FALSE(stale->active);
}

TEST(ChaosCampaign, LowBatteryReachesStandbyWithoutResumingNominal) {
    Campaign campaign(*simulation::find_scenario("low_battery"));
    campaign.run_for(5s);
    const std::vector<Mark> marks = campaign.trace();
    EXPECT_TRUE(saw_fault(marks, Mark::Kind::Activated, flight::FaultType::LowBattery,
                          flight::FaultSource::Battery));
    EXPECT_TRUE(saw_mode(marks, flight::SpacecraftMode::Nominal, flight::SpacecraftMode::SafeMode));
    EXPECT_TRUE(saw_fault(marks, Mark::Kind::Cleared, flight::FaultType::LowBattery,
                          flight::FaultSource::Battery));
    EXPECT_TRUE(saw_mode(marks, flight::SpacecraftMode::SafeMode, flight::SpacecraftMode::Standby));
    EXPECT_FALSE(
        saw_mode(marks, flight::SpacecraftMode::SafeMode, flight::SpacecraftMode::Nominal));
    EXPECT_EQ(campaign.executive.mode(), flight::SpacecraftMode::Standby);
    Time cleared_at{};
    Time standby_at{};
    bool saw_clear = false;
    bool saw_standby = false;
    for (const Mark& mark : marks) {
        if (mark.kind == Mark::Kind::Cleared && mark.type == flight::FaultType::LowBattery) {
            cleared_at = mark.time;
            saw_clear = true;
        }
        if (mark.kind == Mark::Kind::Mode && mark.to == flight::SpacecraftMode::Standby &&
            mark.from == flight::SpacecraftMode::SafeMode) {
            standby_at = mark.time;
            saw_standby = true;
        }
    }
    ASSERT_TRUE(saw_clear && saw_standby);
    EXPECT_LT(cleared_at, standby_at);
}

TEST(ChaosCampaign, DeadlineStormEscalatesThroughTheMonitor) {
    Campaign campaign(*simulation::find_scenario("deadline_storm"));
    campaign.run_for(6s);
    const std::vector<Mark> marks = campaign.trace();
    EXPECT_TRUE(saw_fault(marks, Mark::Kind::Activated, flight::FaultType::DeadlineMiss,
                          flight::FaultSource::NavigationTask));
    EXPECT_TRUE(saw_mode(marks, flight::SpacecraftMode::Nominal, flight::SpacecraftMode::Degraded));
    EXPECT_TRUE(
        saw_mode(marks, flight::SpacecraftMode::Degraded, flight::SpacecraftMode::SafeMode));
    EXPECT_TRUE(saw_fault(marks, Mark::Kind::Cleared, flight::FaultType::DeadlineMiss,
                          flight::FaultSource::NavigationTask));
    EXPECT_TRUE(saw_mode(marks, flight::SpacecraftMode::SafeMode, flight::SpacecraftMode::Standby));
    EXPECT_FALSE(
        saw_mode(marks, flight::SpacecraftMode::SafeMode, flight::SpacecraftMode::Nominal));
    EXPECT_EQ(campaign.executive.mode(), flight::SpacecraftMode::Standby);
    const flight::FaultRecord<Time>* deadline = campaign.fdir.registry().find(
        flight::FaultType::DeadlineMiss, flight::FaultSource::NavigationTask);
    ASSERT_NE(deadline, nullptr);
    EXPECT_FALSE(deadline->active);
    EXPECT_GE(deadline->occurrence_count, 5U);

    std::vector<flight::FaultSeverity> severities;
    for (const flight::SystemEvent<Time>& event : campaign.events.snapshot()) {
        if (const auto* activated = std::get_if<flight::FaultActivatedEvent<Time>>(&event)) {
            if (activated->type == flight::FaultType::DeadlineMiss &&
                activated->source == flight::FaultSource::NavigationTask) {
                severities.push_back(activated->severity);
            }
        } else if (const auto* updated = std::get_if<flight::FaultUpdatedEvent<Time>>(&event)) {
            if (updated->type == flight::FaultType::DeadlineMiss &&
                updated->source == flight::FaultSource::NavigationTask) {
                severities.push_back(updated->severity);
            }
        }
    }
    ASSERT_EQ(severities.size(), 3U);
    EXPECT_EQ(severities[0], flight::FaultSeverity::Advisory);
    EXPECT_EQ(severities[1], flight::FaultSeverity::Warning);
    EXPECT_EQ(severities[2], flight::FaultSeverity::Critical);
}

TEST(ChaosCampaign, MixedFaultsKeepSafeModeUntilBothRecover) {
    Campaign campaign(*simulation::find_scenario("mixed_faults"));
    campaign.run_for(8s);
    EXPECT_EQ(campaign.executive.mode(), flight::SpacecraftMode::SafeMode);
    EXPECT_EQ(campaign.fdir.safe_recovery_streak(), 0U);
    const flight::FaultRecord<Time>* stale =
        campaign.fdir.registry().find(flight::FaultType::SensorStale, flight::FaultSource::Gps);
    ASSERT_NE(stale, nullptr);
    EXPECT_TRUE(stale->active);
    EXPECT_GE(stale->consecutive_count, 3U);
    const flight::FaultRecord<Time>* battery =
        campaign.fdir.registry().find(flight::FaultType::LowBattery, flight::FaultSource::Battery);
    ASSERT_NE(battery, nullptr);
    EXPECT_FALSE(battery->active);

    campaign.run_for(3s);
    EXPECT_EQ(campaign.executive.mode(), flight::SpacecraftMode::Standby);
    EXPECT_FALSE(saw_mode(campaign.trace(), flight::SpacecraftMode::SafeMode,
                          flight::SpacecraftMode::Nominal));
    EXPECT_FALSE(campaign.fdir.registry()
                     .find(flight::FaultType::SensorStale, flight::FaultSource::Gps)
                     ->active);
}

TEST(ChaosCampaign, RepeatedRunMatchesTheFaultAndModeTrace) {
    Campaign first(*simulation::find_scenario("gps_stale"));
    first.run_for(10s);
    Campaign second(*simulation::find_scenario("gps_stale"));
    second.run_for(10s);
    EXPECT_EQ(first.trace(), second.trace());
    EXPECT_EQ(first.executive.mode(), second.executive.mode());
}

TEST(ChaosCampaign, UnknownScenarioNameIsRejected) {
    char program[] = "ares";
    char flag[] = "--scenario";
    char name[] = "not-a-scenario";
    char duration[] = "--duration-ms";
    char value[] = "1";
    char* argv[] = {program, flag, name, duration, value};
    EXPECT_EQ(ares::run(5, argv), ares::to_int(ares::ExitCode::UsageError));
}
