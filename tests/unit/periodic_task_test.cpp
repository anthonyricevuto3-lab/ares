#include "ares/core/periodic_task.hpp"

#include <chrono>
#include <stdexcept>
#include <stop_token>
#include <string_view>

#include <gtest/gtest.h>

namespace core = ares::core;
using namespace std::chrono_literals;
using Task = core::PeriodicTask<core::ManualClock>;
using Time = core::ManualClock::time_point;

namespace {

[[nodiscard]] core::TaskId id(std::string_view name) {
    return core::TaskId::make(name).value();
}

} // namespace

TEST(TaskTiming, AcceptsDeadlineEqualToPeriod) {
    EXPECT_TRUE(core::valid_task_timing(core::TaskTiming{1ms, 1ms}));
}

TEST(TaskTiming, RejectsZeroAndInvertedDeadline) {
    EXPECT_FALSE(core::TaskId::make("").has_value());
    EXPECT_FALSE(core::TaskId::make("nav\n").has_value());
    EXPECT_FALSE(core::valid_task_timing(core::TaskTiming{core::Duration::zero(), 1ms}));
    EXPECT_FALSE(core::valid_task_timing(core::TaskTiming{core::Duration{-1}, 1ms}));
    EXPECT_FALSE(core::valid_task_timing(core::TaskTiming{1ms, core::Duration::zero()}));
    EXPECT_FALSE(core::valid_task_timing(core::TaskTiming{1ms, 2ms}));
}

TEST(PeriodicTask, RejectsInvalidConfiguration) {
    core::ManualClock clock;
    EXPECT_THROW(
        (Task{
            id("nav"), core::TaskTiming{1ms, 1ms}, core::TaskWork<core::ManualClock>{}, clock, {}}),
        std::invalid_argument);
}

TEST(PeriodicTask, WaitsUntilArmedAndDue) {
    core::ManualClock clock;
    int runs = 0;
    Task task{id("nav"),
              core::TaskTiming{100ms, 10ms},
              [&runs](Time, std::stop_token) { ++runs; },
              clock,
              {}};

    EXPECT_EQ(task.poll(), core::PollResult::Waiting);
    EXPECT_EQ(runs, 0);

    ASSERT_EQ(task.arm(Time{100ms}), core::ArmStatus::Armed);
    EXPECT_EQ(task.poll(), core::PollResult::Waiting);
    EXPECT_EQ(runs, 0);

    ASSERT_EQ(clock.advance(100ms), core::AdvanceStatus::Applied);
    EXPECT_EQ(task.poll(), core::PollResult::Ran);
    EXPECT_EQ(runs, 1);
    EXPECT_EQ(task.next_release(), Time{200ms});
}

TEST(PeriodicTask, OnTimeWorkDoesNotMissAndSchedulesNextPeriod) {
    core::ManualClock clock;
    bool missed = false;
    bool cycled = false;
    core::TaskHooks<core::ManualClock> hooks{
        [&](const core::TaskCycleEvent<Time>& event) {
            cycled = true;
            EXPECT_FALSE(event.deadline_missed);
            EXPECT_EQ(event.elapsed, 10ms);
        },
        [&](const core::DeadlineMissEvent<Time>&) { missed = true; },
    };
    Task task{id("nav"), core::TaskTiming{100ms, 10ms},
              [&](Time, std::stop_token) {
                  EXPECT_EQ(clock.advance(10ms), core::AdvanceStatus::Applied);
              },
              clock, std::move(hooks)};
    ASSERT_EQ(task.arm(Time{}), core::ArmStatus::Armed);

    EXPECT_EQ(task.poll(), core::PollResult::Ran);
    EXPECT_TRUE(cycled);
    EXPECT_FALSE(missed);
    EXPECT_EQ(task.next_release(), Time{100ms});
}

TEST(PeriodicTask, SlowWorkAndLateWakeAreMisses) {
    core::ManualClock clock;
    int misses = 0;
    core::TaskHooks<core::ManualClock> hooks{
        {},
        [&](const core::DeadlineMissEvent<Time>&) { ++misses; },
    };
    Task slow{id("nav"), core::TaskTiming{100ms, 10ms},
              [&](Time, std::stop_token) {
                  EXPECT_EQ(clock.advance(11ms), core::AdvanceStatus::Applied);
              },
              clock, hooks};
    ASSERT_EQ(slow.arm(Time{}), core::ArmStatus::Armed);
    EXPECT_EQ(slow.poll(), core::PollResult::Ran);
    EXPECT_EQ(misses, 1);
    EXPECT_EQ(slow.next_release(), Time{100ms});

    core::ManualClock late_clock;
    int late_misses = 0;
    core::TaskHooks<core::ManualClock> late_hooks{
        {}, [&](const core::DeadlineMissEvent<Time>&) { ++late_misses; }};
    Task late{id("nav"), core::TaskTiming{100ms, 10ms}, [](Time, std::stop_token) {}, late_clock,
              late_hooks};
    ASSERT_EQ(late.arm(Time{}), core::ArmStatus::Armed);
    ASSERT_EQ(late_clock.advance(50ms), core::AdvanceStatus::Applied);
    EXPECT_EQ(late.poll(), core::PollResult::Ran);
    EXPECT_EQ(late_misses, 1);
    EXPECT_EQ(late.next_release(), Time{100ms});
}

TEST(PeriodicTask, RepeatedOverrunSkipsMissedReleases) {
    core::ManualClock clock;
    int misses = 0;
    core::TaskHooks<core::ManualClock> hooks{
        {}, [&](const core::DeadlineMissEvent<Time>&) { ++misses; }};
    Task task{id("nav"), core::TaskTiming{100ms, 10ms},
              [&](Time, std::stop_token) {
                  EXPECT_EQ(clock.advance(50ms), core::AdvanceStatus::Applied);
              },
              clock, hooks};
    ASSERT_EQ(task.arm(Time{}), core::ArmStatus::Armed);

    EXPECT_EQ(task.poll(), core::PollResult::Ran);
    EXPECT_EQ(task.next_release(), Time{100ms});
    EXPECT_EQ(task.poll(), core::PollResult::Waiting);

    ASSERT_EQ(clock.advance(50ms), core::AdvanceStatus::Applied);
    EXPECT_EQ(task.poll(), core::PollResult::Ran);
    EXPECT_EQ(misses, 2);
    EXPECT_EQ(task.next_release(), Time{200ms});
}

TEST(PeriodicTask, JumpOverSeveralPeriodsSchedulesTheFollowingBoundary) {
    core::ManualClock clock;
    Task task{id("nav"),
              core::TaskTiming{100ms, 100ms},
              [&](Time, std::stop_token) {
                  EXPECT_EQ(clock.advance(250ms), core::AdvanceStatus::Applied);
              },
              clock,
              {}};
    ASSERT_EQ(task.arm(Time{}), core::ArmStatus::Armed);
    EXPECT_EQ(task.poll(), core::PollResult::Ran);
    EXPECT_EQ(task.next_release(), Time{300ms});
}

TEST(PeriodicTask, SecondArmDoesNotMoveTheRelease) {
    core::ManualClock clock;
    Task task{id("nav"), core::TaskTiming{100ms, 100ms}, [](Time, std::stop_token) {}, clock, {}};
    ASSERT_EQ(task.arm(Time{}), core::ArmStatus::Armed);
    EXPECT_EQ(task.arm(Time{50ms}), core::ArmStatus::AlreadyArmed);
    EXPECT_EQ(task.next_release(), Time{});
}

TEST(PeriodicTask, NestedPollFromWorkDoesNotStartAnotherCycle) {
    core::ManualClock clock;
    int runs = 0;
    Task* self = nullptr;
    core::PollResult nested = core::PollResult::Ran;
    Task task{id("nav"),
              core::TaskTiming{100ms, 100ms},
              [&](Time, std::stop_token) {
                  ++runs;
                  nested = self->poll();
              },
              clock,
              {}};
    self = &task;
    ASSERT_EQ(task.arm(Time{}), core::ArmStatus::Armed);

    EXPECT_EQ(task.poll(), core::PollResult::Ran);
    EXPECT_EQ(nested, core::PollResult::Reentrant);
    EXPECT_EQ(runs, 1);
    EXPECT_EQ(task.next_release(), Time{100ms});
}

TEST(PeriodicTask, NestedPollFromHookDoesNotStartAnotherCycle) {
    core::ManualClock clock;
    Task* self = nullptr;
    int cycles = 0;
    core::PollResult nested = core::PollResult::Ran;
    core::TaskHooks<core::ManualClock> hooks{
        [&](const core::TaskCycleEvent<Time>&) {
            ++cycles;
            nested = self->poll();
        },
        [](const core::DeadlineMissEvent<Time>&) {},
    };
    Task task{id("nav"), core::TaskTiming{100ms, 100ms}, [](Time, std::stop_token) {}, clock,
              std::move(hooks)};
    self = &task;
    ASSERT_EQ(task.arm(Time{}), core::ArmStatus::Armed);

    EXPECT_EQ(task.poll(), core::PollResult::Ran);
    EXPECT_EQ(nested, core::PollResult::Reentrant);
    EXPECT_EQ(cycles, 1);
    EXPECT_EQ(task.next_release(), Time{100ms});
}

TEST(PeriodicTask, WorkExceptionConsumesTheRelease) {
    core::ManualClock clock;
    int runs = 0;
    Task task{id("nav"),
              core::TaskTiming{100ms, 100ms},
              [&](Time, std::stop_token) {
                  ++runs;
                  throw std::runtime_error("cycle failed");
              },
              clock,
              {}};
    ASSERT_EQ(task.arm(Time{}), core::ArmStatus::Armed);

    EXPECT_THROW(static_cast<void>(task.poll()), std::runtime_error);
    EXPECT_EQ(runs, 1);
    EXPECT_TRUE(task.deadline_record().failed);
    EXPECT_TRUE(task.deadline_record().recorded);
    EXPECT_EQ(task.next_release(), Time{100ms});
    EXPECT_EQ(task.poll(), core::PollResult::Waiting);
    EXPECT_EQ(runs, 1);
}

TEST(PeriodicTask, LateJumpReportsSkippedReleasesWithoutBursting) {
    core::ManualClock clock;
    int cycles = 0;
    int misses = 0;
    core::TaskHooks<core::ManualClock> hooks{
        [&](const core::TaskCycleEvent<Time>& event) {
            ++cycles;
            EXPECT_TRUE(event.deadline_missed);
            EXPECT_EQ(event.releases_skipped, 2U);
            EXPECT_EQ(event.elapsed, 250ms);
        },
        [&](const core::DeadlineMissEvent<Time>& event) {
            ++misses;
            EXPECT_EQ(event.releases_skipped, 2U);
        },
    };
    Task task{id("nav"), core::TaskTiming{100ms, 100ms},
              [&](Time, std::stop_token) {
                  EXPECT_EQ(clock.advance(250ms), core::AdvanceStatus::Applied);
              },
              clock, std::move(hooks)};
    ASSERT_EQ(task.arm(Time{}), core::ArmStatus::Armed);

    EXPECT_EQ(task.poll(), core::PollResult::Ran);
    EXPECT_EQ(task.poll(), core::PollResult::Waiting);
    EXPECT_EQ(cycles, 1);
    EXPECT_EQ(misses, 1);
    EXPECT_EQ(task.next_release(), Time{300ms});
}

TEST(PeriodicTask, ReleaseBoundaries) {
    const auto run_once = [](core::Duration lag, Time expected_next, std::uint64_t skipped,
                             bool missed) {
        core::ManualClock clock;
        bool saw_miss = false;
        core::Duration elapsed = core::Duration::max();
        std::uint64_t reported_skips = 99;
        core::TaskHooks<core::ManualClock> hooks{
            [&](const core::TaskCycleEvent<Time>& event) {
                elapsed = event.elapsed;
                reported_skips = event.releases_skipped;
            },
            [&](const core::DeadlineMissEvent<Time>&) { saw_miss = true; },
        };
        Task task{id("nav"), core::TaskTiming{100ms, 100ms},
                  [&](Time, std::stop_token) {
                      EXPECT_EQ(clock.advance(lag), core::AdvanceStatus::Applied);
                  },
                  clock, std::move(hooks)};
        if (task.arm(Time{}) != core::ArmStatus::Armed) {
            ADD_FAILURE();
            return;
        }
        EXPECT_EQ(task.poll(), core::PollResult::Ran);
        EXPECT_EQ(task.next_release(), expected_next);
        EXPECT_EQ(elapsed, lag);
        EXPECT_EQ(reported_skips, skipped);
        EXPECT_EQ(saw_miss, missed);
        if (expected_next > Time{lag}) {
            EXPECT_EQ(task.poll(), core::PollResult::Waiting);
        }
    };

    run_once(40ms, Time{100ms}, 0, false);
    run_once(100ms, Time{100ms}, 0, false);
    run_once(100ms + 1ns, Time{200ms}, 1, true);
    run_once(250ms, Time{300ms}, 2, true);
    run_once(10000 * 100ms + 1ns, Time{10001 * 100ms}, 10000, true);
    run_once(10000 * 100ms, Time{10000 * 100ms}, 9999, true);
}

TEST(PeriodicTask, HookThrowDoesNotRerunWork) {
    const auto check = [](bool throw_from_miss) {
        core::ManualClock clock;
        int runs = 0;
        int misses = 0;
        core::TaskHooks<core::ManualClock> hooks{
            [&](const core::TaskCycleEvent<Time>&) {
                if (!throw_from_miss) {
                    throw std::runtime_error("cycle hook failed");
                }
            },
            [&](const core::DeadlineMissEvent<Time>&) {
                ++misses;
                if (throw_from_miss) {
                    throw std::runtime_error("deadline hook failed");
                }
            },
        };
        Task task{id("nav"), core::TaskTiming{100ms, 10ms},
                  [&](Time, std::stop_token) {
                      ++runs;
                      EXPECT_EQ(clock.advance(50ms), core::AdvanceStatus::Applied);
                  },
                  clock, std::move(hooks)};
        if (task.arm(Time{}) != core::ArmStatus::Armed) {
            ADD_FAILURE();
            return;
        }
        EXPECT_EQ(task.poll(), core::PollResult::HookError);
        EXPECT_EQ(runs, 1);
        EXPECT_EQ(misses, 1);
        EXPECT_EQ(task.next_release(), Time{100ms});
        EXPECT_TRUE(task.deadline_record().recorded);
        EXPECT_TRUE(task.deadline_record().missed);
        EXPECT_FALSE(task.deadline_record().failed);
        EXPECT_EQ(task.poll(), core::PollResult::Waiting);
        EXPECT_EQ(runs, 1);
    };

    check(true);
    check(false);
}

TEST(PeriodicTask, UnrepresentableNextReleaseIsAnError) {
    core::ManualClock clock;
    ASSERT_EQ(clock.advance(core::Duration::max()), core::AdvanceStatus::Applied);
    Task task{id("nav"), core::TaskTiming{1ns, 1ns}, [](Time, std::stop_token) {}, clock, {}};
    ASSERT_EQ(task.arm(Time::max()), core::ArmStatus::Armed);
    EXPECT_EQ(task.poll(), core::PollResult::ScheduleError);
    EXPECT_EQ(task.next_release(), Time::max());
    EXPECT_TRUE(task.deadline_record().recorded);
    EXPECT_EQ(task.poll(), core::PollResult::Waiting);
}

TEST(ManualClock, AdvanceToTheLimitIsRejected) {
    core::ManualClock clock;
    ASSERT_EQ(clock.advance(core::Duration::max()), core::AdvanceStatus::Applied);
    EXPECT_EQ(clock.advance(1ns), core::AdvanceStatus::Unrepresentable);
    EXPECT_EQ(clock.now().time, Time::max());
}
