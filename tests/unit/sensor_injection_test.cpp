#include "ares/core/logger.hpp"
#include "ares/flight/example_tasks.hpp"
#include "ares/flight/fdir.hpp"
#include "ares/flight/power_manager.hpp"
#include "ares/flight/sample_limits.hpp"
#include "ares/simulation/chaos_engine.hpp"
#include "ares/simulation/sensors.hpp"

#include <chrono>
#include <span>
#include <sstream>
#include <stop_token>

#include <gtest/gtest.h>

namespace flight = ares::flight;
namespace hardware = ares::hardware;
namespace simulation = ares::simulation;
using namespace std::chrono_literals;
using Clock = ares::core::ManualClock;
using Time = Clock::time_point;

namespace {

struct Rig {
    Clock clock{};
    std::ostringstream out{};
    ares::core::Logger<Clock> logger;
    ares::core::EventLog<flight::SystemEvent<Time>> events{};
    simulation::SpacecraftModel<Clock> model;
    simulation::ChaosEngine<Clock> chaos{};
    simulation::SimulatedImu<Clock> imu;
    simulation::SimulatedGps<Clock> gps;
    simulation::SimulatedBatteryMonitor<Clock> battery;
    flight::FlightExecutive<Clock> executive;
    flight::NavigationCadence<Clock> navigation;
    flight::PowerManager<Clock> power;
    flight::FdirController<Clock> fdir;

    Rig()
        : logger(out, clock), model(clock), imu(model, {}, &chaos), gps(model, {}, &chaos),
          battery(model, {}, &chaos), executive(clock, logger, events),
          navigation(logger, imu, gps, clock,
                     flight::NavigationAgeLimits{flight::limits::kNavigationImuMaxAge,
                                                 flight::limits::kNavigationGpsMaxAge}),
          power(battery, clock, flight::limits::kPowerMaxAge), fdir(events) {
        simulation::SimulationTruth<Clock> truth;
        truth.epoch = Time{};
        truth.voltage = hardware::Millivolts{12400};
        model.set_truth(truth);
    }

    void nominal() {
        ASSERT_EQ(executive.boot_to_standby().status, flight::TransitionStatus::Accepted);
        ASSERT_EQ(executive.accept(flight::Command::StartMission), flight::CommandStatus::Accepted);
    }

    void read_navigation() { navigation(clock.now().time, std::stop_token{}); }
};

} // namespace

TEST(SensorInjection, GpsUnavailableIsReportedByTheSensorAndRecordedByFdir) {
    Rig rig;
    rig.nominal();
    const simulation::ChaosEvent event{
        simulation::InjectionKind::SensorUnavailable, simulation::ChaosTarget::Gps, 0s, 2s, 0, 0};
    ASSERT_TRUE(rig.chaos.load(std::span{&event, 1}, Time{}));
    rig.read_navigation();
    EXPECT_EQ(rig.gps.read().status, hardware::SensorStatus::Unavailable);
    EXPECT_EQ(rig.navigation.solution().gps_usability, flight::SampleUsability::Unavailable);
    EXPECT_EQ(rig.fdir.observe_sensor(flight::FaultSource::PrimaryGps,
                                      rig.navigation.solution().gps_usability, Time{}),
              flight::RegistryStatus::Activated);
    const flight::FaultRecord<Time>* fault = rig.fdir.registry().find(
        flight::FaultType::SensorUnavailable, flight::FaultSource::PrimaryGps);
    ASSERT_NE(fault, nullptr);
    EXPECT_TRUE(fault->active);

    ASSERT_EQ(rig.clock.advance(2s), ares::core::AdvanceStatus::Applied);
    rig.read_navigation();
    EXPECT_EQ(rig.gps.read().status, hardware::SensorStatus::Valid);
    EXPECT_EQ(rig.navigation.solution().gps_usability, flight::SampleUsability::Usable);
    EXPECT_EQ(rig.fdir.observe_sensor(flight::FaultSource::PrimaryGps,
                                      flight::SampleUsability::Usable, rig.clock.now().time),
              flight::RegistryStatus::Cleared);
    EXPECT_FALSE(rig.fdir.registry()
                     .find(flight::FaultType::SensorUnavailable, flight::FaultSource::PrimaryGps)
                     ->active);
}

TEST(SensorInjection, GpsInvalidFollowsTheSamePath) {
    Rig rig;
    const simulation::ChaosEvent event{
        simulation::InjectionKind::SensorInvalid, simulation::ChaosTarget::Gps, 0s, 1s, 0, 0};
    ASSERT_TRUE(rig.chaos.load(std::span{&event, 1}, Time{}));
    rig.read_navigation();
    EXPECT_EQ(rig.gps.read().status, hardware::SensorStatus::Invalid);
    EXPECT_EQ(rig.navigation.solution().gps_usability, flight::SampleUsability::Invalid);
    EXPECT_EQ(rig.fdir.observe_sensor(flight::FaultSource::PrimaryGps,
                                      flight::SampleUsability::Invalid, Time{}),
              flight::RegistryStatus::Activated);
    ASSERT_EQ(rig.clock.advance(1s), ares::core::AdvanceStatus::Applied);
    rig.read_navigation();
    EXPECT_EQ(rig.navigation.solution().gps_usability, flight::SampleUsability::Usable);
}

TEST(SensorInjection, ImuInvalidIsIndependentOfGps) {
    Rig rig;
    const simulation::ChaosEvent event{
        simulation::InjectionKind::SensorInvalid, simulation::ChaosTarget::Imu, 0s, 1s, 0, 0};
    ASSERT_TRUE(rig.chaos.load(std::span{&event, 1}, Time{}));
    rig.read_navigation();
    EXPECT_EQ(rig.imu.read().status, hardware::SensorStatus::Invalid);
    EXPECT_EQ(rig.gps.read().status, hardware::SensorStatus::Valid);
    EXPECT_EQ(rig.navigation.solution().imu_usability, flight::SampleUsability::Invalid);
    EXPECT_EQ(rig.navigation.solution().gps_usability, flight::SampleUsability::Usable);
}

TEST(SensorInjection, GpsFreezeBecomesStaleThroughFreshnessThenRestores) {
    Rig rig;
    const simulation::ChaosEvent event{
        simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 0s, 2s, 0, 0};
    ASSERT_TRUE(rig.chaos.load(std::span{&event, 1}, Time{}));
    rig.read_navigation();
    const hardware::GpsSample<Time> captured = rig.gps.read();
    EXPECT_EQ(captured.time, Time{});
    EXPECT_EQ(captured.status, hardware::SensorStatus::Valid);
    EXPECT_EQ(rig.navigation.solution().gps_usability, flight::SampleUsability::Usable);

    ASSERT_EQ(rig.clock.advance(1s), ares::core::AdvanceStatus::Applied);
    rig.read_navigation();
    EXPECT_EQ(rig.gps.read().time, Time{});
    EXPECT_EQ(rig.navigation.solution().gps_usability, flight::SampleUsability::Usable);

    ASSERT_EQ(rig.clock.advance(1ns), ares::core::AdvanceStatus::Applied);
    rig.read_navigation();
    EXPECT_EQ(rig.gps.read().time, Time{});
    EXPECT_EQ(rig.gps.read().status, hardware::SensorStatus::Valid);
    EXPECT_EQ(rig.navigation.solution().gps_usability, flight::SampleUsability::Stale);
    EXPECT_EQ(rig.fdir.observe_sensor(flight::FaultSource::PrimaryGps,
                                      flight::SampleUsability::Stale, rig.clock.now().time),
              flight::RegistryStatus::Activated);

    ASSERT_EQ(rig.clock.advance(2s), ares::core::AdvanceStatus::Applied);
    rig.read_navigation();
    EXPECT_EQ(rig.gps.read().time, rig.clock.now().time);
    EXPECT_EQ(rig.navigation.solution().gps_usability, flight::SampleUsability::Usable);
    EXPECT_EQ(rig.fdir.observe_sensor(flight::FaultSource::PrimaryGps,
                                      flight::SampleUsability::Usable, rig.clock.now().time),
              flight::RegistryStatus::Cleared);
}

TEST(SensorInjection, BatteryOverrideEntersSafeModeAndRecoveryRequestsStandby) {
    Rig rig;
    rig.nominal();
    const simulation::ChaosEvent event{simulation::InjectionKind::BatteryVoltageOverride,
                                       simulation::ChaosTarget::Battery,
                                       0s,
                                       1s,
                                       10800,
                                       0};
    ASSERT_TRUE(rig.chaos.load(std::span{&event, 1}, Time{}));
    const hardware::BatterySample<Time>& low = rig.power.sample();
    EXPECT_EQ(low.voltage, hardware::Millivolts{10800});
    EXPECT_EQ(low.status, hardware::SensorStatus::Valid);
    EXPECT_EQ(rig.power.usability(), flight::SampleUsability::Usable);
    EXPECT_EQ(rig.fdir.observe_battery(flight::SampleUsability::Usable, low.voltage, Time{}).power,
              flight::RegistryStatus::Activated);
    const flight::PolicyDecision entered = rig.fdir.apply(rig.executive);
    EXPECT_EQ(entered.action, flight::RecoveryAction::EnterSafeMode);
    EXPECT_EQ(rig.executive.mode(), flight::SpacecraftMode::SafeMode);

    ASSERT_EQ(rig.clock.advance(1s), ares::core::AdvanceStatus::Applied);
    const hardware::BatterySample<Time>& restored = rig.power.sample();
    EXPECT_EQ(restored.voltage, hardware::Millivolts{12400});
    EXPECT_EQ(rig.fdir
                  .observe_battery(flight::SampleUsability::Usable, restored.voltage,
                                   rig.clock.now().time)
                  .power,
              flight::RegistryStatus::Cleared);
    EXPECT_EQ(rig.fdir.apply(rig.executive).requested_mode, std::nullopt);
    EXPECT_EQ(rig.executive.mode(), flight::SpacecraftMode::SafeMode);
    EXPECT_EQ(rig.fdir.safe_recovery_streak(), 1U);
    (void)rig.fdir.apply(rig.executive);
    EXPECT_EQ(rig.executive.mode(), flight::SpacecraftMode::SafeMode);
    const flight::PolicyDecision standby = rig.fdir.apply(rig.executive);
    EXPECT_EQ(standby.action, flight::RecoveryAction::RecoverToStandby);
    EXPECT_EQ(*standby.requested_mode, flight::SpacecraftMode::Standby);
    EXPECT_EQ(*standby.transition, flight::TransitionStatus::Accepted);
    EXPECT_EQ(rig.executive.mode(), flight::SpacecraftMode::Standby);
    EXPECT_NE(rig.executive.mode(), flight::SpacecraftMode::Nominal);
}

TEST(SensorInjection, BackToBackFreezesCaptureDistinctSamples) {
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    simulation::ChaosEngine<Clock> chaos;
    simulation::SimulatedGps<Clock> gps(model, {}, &chaos);
    const simulation::ChaosEvent events[] = {
        {simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 1s, 1s, 0, 0},
        {simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 2s, 1s, 0, 0},
    };
    ASSERT_TRUE(chaos.load(std::span{events}, Time{}));
    ASSERT_EQ(clock.advance(1s), ares::core::AdvanceStatus::Applied);
    const hardware::GpsSample<Time> first = gps.read();
    ASSERT_EQ(clock.advance(1s), ares::core::AdvanceStatus::Applied);
    const hardware::GpsSample<Time> second = gps.read();
    EXPECT_EQ(first.time, Time{1s});
    EXPECT_EQ(second.time, Time{2s});
    EXPECT_NE(second.time, first.time);
    EXPECT_NE(chaos.active_sequence(simulation::InjectionKind::SensorFreeze,
                                    simulation::ChaosTarget::Gps, Time{1s}),
              chaos.active_sequence(simulation::InjectionKind::SensorFreeze,
                                    simulation::ChaosTarget::Gps, Time{2s}));
}

TEST(SensorInjection, FreezeGapWithoutAReadCapturesTheLaterEvent) {
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    simulation::ChaosEngine<Clock> chaos;
    simulation::SimulatedGps<Clock> gps(model, {}, &chaos);
    const simulation::ChaosEvent events[] = {
        {simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 1s, 1s, 0, 0},
        {simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 3s, 1s, 0, 0},
    };
    ASSERT_TRUE(chaos.load(std::span{events}, Time{}));
    ASSERT_EQ(clock.advance(1s), ares::core::AdvanceStatus::Applied);
    const hardware::GpsSample<Time> first = gps.read();
    ASSERT_EQ(clock.advance(2s), ares::core::AdvanceStatus::Applied);
    const hardware::GpsSample<Time> second = gps.read();
    EXPECT_EQ(first.time, Time{1s});
    EXPECT_EQ(second.time, Time{3s});
}

TEST(SensorInjection, OneFreezeRepeatsTheCapturedSample) {
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    simulation::SensorNoise noise;
    noise.position = 10;
    simulation::ChaosEngine<Clock> chaos;
    simulation::SimulatedGps<Clock> gps(model, noise, &chaos);
    const simulation::ChaosEvent event{
        simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 0s, 5s, 0, 0};
    ASSERT_TRUE(chaos.load(std::span{&event, 1}, Time{}));
    const hardware::GpsSample<Time> first = gps.read();
    const auto identity = chaos.active_sequence(simulation::InjectionKind::SensorFreeze,
                                                simulation::ChaosTarget::Gps, Time{});
    ASSERT_EQ(clock.advance(1s), ares::core::AdvanceStatus::Applied);
    const hardware::GpsSample<Time> second = gps.read();
    ASSERT_EQ(clock.advance(1s), ares::core::AdvanceStatus::Applied);
    const hardware::GpsSample<Time> third = gps.read();
    EXPECT_EQ(second.time, first.time);
    EXPECT_EQ(third.time, first.time);
    EXPECT_EQ(second.position, first.position);
    EXPECT_EQ(third.position, first.position);
    EXPECT_EQ(chaos.active_sequence(simulation::InjectionKind::SensorFreeze,
                                    simulation::ChaosTarget::Gps, Time{2s}),
              identity);
}

TEST(SensorInjection, FrozenReadsDoNotDrawNoise) {
    const auto sample_after = [](bool extra_frozen_reads) {
        Clock clock;
        simulation::SpacecraftModel<Clock> model(clock);
        simulation::SensorNoise noise;
        noise.position = 10;
        simulation::ChaosEngine<Clock> chaos;
        simulation::SimulatedGps<Clock> gps(model, noise, &chaos);
        const simulation::ChaosEvent event{
            simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 0s, 2s, 0, 0};
        EXPECT_TRUE(chaos.load(std::span{&event, 1}, Time{}));
        (void)gps.read();
        if (extra_frozen_reads) {
            EXPECT_EQ(clock.advance(1s), ares::core::AdvanceStatus::Applied);
            (void)gps.read();
            (void)gps.read();
        }
        EXPECT_EQ(clock.advance(extra_frozen_reads ? 1s : 2s), ares::core::AdvanceStatus::Applied);
        return gps.read();
    };
    const hardware::GpsSample<Time> with_repeats = sample_after(true);
    const hardware::GpsSample<Time> without_repeats = sample_after(false);
    EXPECT_EQ(with_repeats.time, Time{2s});
    EXPECT_EQ(without_repeats.time, Time{2s});
    EXPECT_EQ(with_repeats.position, without_repeats.position);
    EXPECT_EQ(with_repeats.velocity, without_repeats.velocity);
}

TEST(SensorInjection, RestoredReadUsesTheCurrentTimeAndALaterFreezeRecaptures) {
    Clock clock;
    simulation::SpacecraftModel<Clock> model(clock);
    simulation::ChaosEngine<Clock> chaos;
    simulation::SimulatedGps<Clock> gps(model, {}, &chaos);
    const simulation::ChaosEvent events[] = {
        {simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 1s, 1s, 0, 0},
        {simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 4s, 1s, 0, 0},
    };
    ASSERT_TRUE(chaos.load(std::span{events}, Time{}));
    ASSERT_EQ(clock.advance(1s), ares::core::AdvanceStatus::Applied);
    EXPECT_EQ(gps.read().time, Time{1s});
    ASSERT_EQ(clock.advance(2s), ares::core::AdvanceStatus::Applied);
    EXPECT_EQ(gps.read().time, Time{3s});
    ASSERT_EQ(clock.advance(1s), ares::core::AdvanceStatus::Applied);
    EXPECT_EQ(gps.read().time, Time{4s});
}

TEST(SensorInjection, FreezeCapturesRepeatForTheSameClockSequence) {
    const auto second_stamp = [] {
        Clock clock;
        simulation::SpacecraftModel<Clock> model(clock);
        simulation::ChaosEngine<Clock> chaos;
        simulation::SimulatedGps<Clock> gps(model, {}, &chaos);
        const simulation::ChaosEvent events[] = {
            {simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 1s, 1s, 0, 0},
            {simulation::InjectionKind::SensorFreeze, simulation::ChaosTarget::Gps, 2s, 1s, 0, 0},
        };
        EXPECT_TRUE(chaos.load(std::span{events}, Time{}));
        EXPECT_EQ(clock.advance(1s), ares::core::AdvanceStatus::Applied);
        const Time first = gps.read().time;
        EXPECT_EQ(clock.advance(1s), ares::core::AdvanceStatus::Applied);
        const Time second = gps.read().time;
        EXPECT_EQ(first, Time{1s});
        return second;
    };
    EXPECT_EQ(second_stamp(), second_stamp());
    EXPECT_EQ(second_stamp(), Time{2s});
}
