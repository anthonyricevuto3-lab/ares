#include "ares/core/bounded_log.hpp"
#include "ares/core/task_supervisor.hpp"
#include "ares/flight/flight_tasks.hpp"
#include "ares/flight/executive.hpp"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <stop_token>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace core = ares::core;
namespace flight = ares::flight;
using namespace std::chrono_literals;
using Clock = core::ManualClock;
using Time = Clock::time_point;
using Supervisor = core::TaskSupervisor<Clock>;

namespace {

// Deadlock guard only. Scheduling itself is the manual clock plus the worker signal.
constexpr auto kDeadlockGuard = 5s;

template <typename Event>
std::vector<Event> of_type(const std::vector<flight::SystemEvent<Time>>& recorded) {
    std::vector<Event> selected;
    for (const flight::SystemEvent<Time>& event : recorded) {
        if (const auto* value = std::get_if<Event>(&event)) {
            selected.push_back(*value);
        }
    }
    return selected;
}

template <typename Predicate>
[[nodiscard]] bool wait_for_signal(std::condition_variable& cv, std::unique_lock<std::mutex>& lock,
                                   Predicate ready) {
    return cv.wait_for(lock, kDeadlockGuard, ready);
}

} // namespace

TEST(BootSequence, EntersNominalAndRunsThreeTasks) {
    Clock clock;
    std::ostringstream output;
    core::Logger<Clock> logger(output, clock);
    core::EventLog<flight::SystemEvent<Time>> events;
    flight::FlightExecutive<Clock> executive(clock, logger, events);

    const flight::BootResult boot = executive.boot_to_standby();
    ASSERT_EQ(boot.status, flight::TransitionStatus::Accepted);
    ASSERT_EQ(boot.mode, flight::SpacecraftMode::Standby);
    ASSERT_EQ(executive.accept(flight::Command::StartMission), flight::CommandStatus::Accepted);
    ASSERT_EQ(executive.mode(), flight::SpacecraftMode::Nominal);

    flight::NavigationCadence<Clock> navigation(logger);
    flight::HealthPulse<Clock> health(logger);
    flight::CommBeacon<Clock> comms(logger);

    std::mutex mutex;
    std::condition_variable cv;
    struct Progress {
        std::uint64_t cycles{0};
        std::thread::id id{};
    };
    std::array<Progress, 3> progress{};
    core::BoundedLog<core::TaskCycleEvent<Time>, 32> cycles;
    core::BoundedLog<core::DeadlineMissEvent<Time>, 8> misses;

    const auto make_hooks = [&](std::size_t index) {
        return core::TaskHooks<Clock>{
            [&, index](const core::TaskCycleEvent<Time>& event) {
                EXPECT_EQ(cycles.push(event),
                          (core::BoundedLog<core::TaskCycleEvent<Time>, 32>::Push::Stored));
                {
                    std::lock_guard lock(mutex);
                    progress[index].cycles += 1;
                    progress[index].id = std::this_thread::get_id();
                }
                cv.notify_all();
            },
            [&](const core::DeadlineMissEvent<Time>& event) {
                EXPECT_EQ(misses.push(event),
                          (core::BoundedLog<core::DeadlineMissEvent<Time>, 8>::Push::Stored));
            },
        };
    };

    Supervisor supervisor(clock);
    ASSERT_EQ(supervisor.add(
                  flight::NavigationCadence<Clock>::name, core::TaskTiming{5ms, 5ms},
                  [&](Time scheduled, std::stop_token stop) { navigation(scheduled, stop); },
                  make_hooks(0)),
              core::AddStatus::Ok);
    ASSERT_EQ(supervisor.add(
                  flight::HealthPulse<Clock>::name, core::TaskTiming{10ms, 10ms},
                  [&](Time scheduled, std::stop_token stop) { health(scheduled, stop); },
                  make_hooks(1)),
              core::AddStatus::Ok);
    ASSERT_EQ(supervisor.add(
                  flight::CommBeacon<Clock>::name, core::TaskTiming{15ms, 15ms},
                  [&](Time scheduled, std::stop_token stop) { comms(scheduled, stop); },
                  make_hooks(2)),
              core::AddStatus::Ok);
    ASSERT_EQ(supervisor.start(), core::StartStatus::Ok);

    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(wait_for_signal(cv, lock, [&] {
            return progress[0].cycles >= 1 && progress[1].cycles >= 1 && progress[2].cycles >= 1;
        }));
    }

    // Step one period at a time. A jump past a release is a real deadline miss.
    ASSERT_EQ(clock.advance(5ms), core::AdvanceStatus::Applied);
    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(wait_for_signal(cv, lock, [&] { return progress[0].cycles >= 2; }));
    }
    ASSERT_EQ(clock.advance(5ms), core::AdvanceStatus::Applied);
    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(wait_for_signal(
            cv, lock, [&] { return progress[0].cycles >= 3 && progress[1].cycles >= 2; }));
    }
    ASSERT_EQ(clock.advance(5ms), core::AdvanceStatus::Applied);
    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(wait_for_signal(
            cv, lock, [&] { return progress[0].cycles >= 4 && progress[2].cycles >= 2; }));
    }

    const auto main_id = std::this_thread::get_id();
    {
        std::scoped_lock lock(mutex);
        EXPECT_NE(progress[0].id, main_id);
        EXPECT_NE(progress[1].id, main_id);
        EXPECT_NE(progress[2].id, main_id);
        EXPECT_NE(progress[0].id, progress[1].id);
        EXPECT_NE(progress[1].id, progress[2].id);
        EXPECT_NE(progress[0].id, progress[2].id);
    }

    supervisor.shutdown();
    EXPECT_EQ(supervisor.stopped_workers(), 3U);
    EXPECT_EQ(supervisor.completed_cycles(), 8U);
    EXPECT_EQ(navigation.cycles(), 4U);
    EXPECT_EQ(health.cycles(), 2U);
    EXPECT_EQ(comms.cycles(), 2U);
    EXPECT_EQ(cycles.size(), 8U);
    EXPECT_FALSE(misses.overflowed());
    EXPECT_EQ(misses.overwrite_count(), 0U);
    EXPECT_EQ(misses.size(), 0U);

    const auto frozen = navigation.cycles();
    ASSERT_EQ(clock.advance(15ms), core::AdvanceStatus::Applied);
    EXPECT_EQ(navigation.cycles(), frozen);
    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::Nominal);

    const auto recorded = events.snapshot();
    const auto modes = of_type<flight::ModeChangedEvent<Time>>(recorded);
    ASSERT_EQ(modes.size(), 3U);
    EXPECT_EQ(modes[0],
              (flight::ModeChangedEvent<Time>{flight::SpacecraftMode::Boot,
                                              flight::SpacecraftMode::Initialization, Time{}}));
    EXPECT_EQ(modes[1], (flight::ModeChangedEvent<Time>{flight::SpacecraftMode::Initialization,
                                                        flight::SpacecraftMode::Standby, Time{}}));
    EXPECT_EQ(modes[2], (flight::ModeChangedEvent<Time>{flight::SpacecraftMode::Standby,
                                                        flight::SpacecraftMode::Nominal, Time{}}));
    const auto commands = of_type<flight::CommandEvent<Time>>(recorded);
    ASSERT_EQ(commands.size(), 1U);
    EXPECT_EQ(commands.front(),
              (flight::CommandEvent<Time>{flight::Command::StartMission, true, Time{}}));
    EXPECT_NE(output.str().find("Standby -> Nominal"), std::string::npos);
}

TEST(BootSequence, DeadlineMissDoesNotChangeMode) {
    Clock clock;
    std::ostringstream output;
    core::Logger<Clock> logger(output, clock);
    core::EventLog<flight::SystemEvent<Time>> events;
    flight::FlightExecutive<Clock> executive(clock, logger, events);
    const flight::BootResult boot = executive.boot_to_standby();
    ASSERT_EQ(boot.status, flight::TransitionStatus::Accepted);
    ASSERT_EQ(executive.accept(flight::Command::StartMission), flight::CommandStatus::Accepted);

    std::mutex mutex;
    std::condition_variable cv;
    bool missed = false;
    Supervisor supervisor(clock);
    ASSERT_EQ(supervisor.add(
                  "overrun", core::TaskTiming{1s, 10ms},
                  [&](Time, std::stop_token) {
                      EXPECT_EQ(clock.advance(11ms), core::AdvanceStatus::Applied);
                  },
                  core::TaskHooks<Clock>{
                      [](const core::TaskCycleEvent<Time>&) {},
                      [&](const core::DeadlineMissEvent<Time>& event) {
                          EXPECT_EQ(event.id.text(), "overrun");
                          EXPECT_GT(event.elapsed, 10ms);
                          std::lock_guard lock(mutex);
                          missed = true;
                          cv.notify_all();
                      },
                  }),
              core::AddStatus::Ok);
    ASSERT_EQ(supervisor.start(), core::StartStatus::Ok);
    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(wait_for_signal(cv, lock, [&] { return missed; }));
    }
    supervisor.shutdown();

    EXPECT_EQ(executive.mode(), flight::SpacecraftMode::Nominal);
    EXPECT_EQ(of_type<flight::ModeChangedEvent<Time>>(events.snapshot()).size(), 3U);
}
