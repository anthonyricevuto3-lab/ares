#pragma once

#include "ares/core/event_log.hpp"
#include "ares/flight/fault_events.hpp"
#include "ares/flight/fdir_limits.hpp"
#include "ares/flight/system_event.hpp"

#include <cstdint>
#include <limits>

namespace ares::flight {

// Facts already recorded by FDIR. Recovery does not detect them again.
struct NavigationRecoveryFact {
    bool active{false};
    bool critical{false};
    std::uint32_t consecutive{0};
    std::uint32_t generation{0};
    bool observation_pending{false};
    std::uint32_t observation_generation{0};
    std::uint8_t on_time_run{0};
};

struct GpsRecoveryFact {
    bool primary_selected{true};
    bool primary_persistent{false};
    std::uint8_t backup_usable_run{0};
    std::uint8_t backup_unusable_run{0};
};

struct RecoveryCommand {
    bool restart_navigation{false};
    bool failover_to_backup{false};
};

// One health-thread owner. Does not write the registry and does not request a mode.
template <typename TimePoint> class RecoveryManager {
public:
    [[nodiscard]] RecoveryCommand observe(const NavigationRecoveryFact& navigation,
                                          const GpsRecoveryFact& gps, TimePoint time,
                                          core::EventLog<SystemEvent<TimePoint>>& events) {
        RecoveryCommand command;
        observe_navigation(navigation, time, events, command);
        observe_gps(gps, time, events, command);
        return command;
    }

    [[nodiscard]] bool blocks_operational_progress() const noexcept {
        return blocks(navigation_.state) || blocks(gps_.state);
    }

    // A verified backup lets an isolated primary warning stay in the registry
    // without blocking Nominal or Standby. The record itself is not cleared.
    [[nodiscard]] bool suppress_primary_warning() const noexcept {
        return gps_.state == RecoveryState::Succeeded && gps_.isolated;
    }

    [[nodiscard]] RecoveryState navigation_state() const noexcept { return navigation_.state; }
    [[nodiscard]] RecoveryState gps_state() const noexcept { return gps_.state; }
    [[nodiscard]] std::uint8_t navigation_attempts() const noexcept { return navigation_.attempts; }
    [[nodiscard]] std::uint8_t gps_attempts() const noexcept { return gps_.attempts; }
    [[nodiscard]] std::uint32_t acknowledged_generation() const noexcept {
        return navigation_.generation;
    }
    [[nodiscard]] bool primary_isolated() const noexcept { return gps_.isolated; }

private:
    struct Episode {
        RecoveryState state{RecoveryState::Idle};
        std::uint8_t attempts{0};
        std::uint32_t generation{0};
        std::uint32_t target_generation{0};
        std::uint32_t baseline{0};
        bool isolated{false};
        bool window_open{false};
    };

    static bool blocks(RecoveryState state) noexcept {
        return state == RecoveryState::Requested || state == RecoveryState::Executing ||
               state == RecoveryState::Verifying || state == RecoveryState::Failed;
    }

    static bool five_new_misses(std::uint32_t count, std::uint32_t baseline) noexcept {
        if (count >= baseline) {
            return (count - baseline) >= limits::kDeadlineCriticalAfter;
        }
        return count >= limits::kDeadlineCriticalAfter;
    }

    void publish(core::EventLog<SystemEvent<TimePoint>>& events, TimePoint time,
                 SubsystemAction action, RecoveryTarget target, std::uint8_t attempt,
                 std::uint32_t generation, RecoveryNotice notice = RecoveryNotice::Started) {
        (void)events.publish(SystemEvent<TimePoint>{
            RecoveryEvent<TimePoint>{action, target, attempt, generation, time, notice}});
    }

    void observe_navigation(const NavigationRecoveryFact& fact, TimePoint time,
                            core::EventLog<SystemEvent<TimePoint>>& events,
                            RecoveryCommand& command) {
        if ((navigation_.state == RecoveryState::Succeeded ||
             navigation_.state == RecoveryState::Failed) &&
            !fact.active) {
            navigation_ = {};
        }

        if (navigation_.state == RecoveryState::Idle && fact.active && fact.critical) {
            if (fact.generation == std::numeric_limits<std::uint32_t>::max()) {
                navigation_.state = RecoveryState::Failed;
                publish(events, time, SubsystemAction::RestartTask, RecoveryTarget::NavigationTask,
                        0, fact.generation, RecoveryNotice::Failed);
                return;
            }
            navigation_.state = RecoveryState::Executing;
            navigation_.target_generation = fact.generation + 1;
            command.restart_navigation = true;
        }

        if (navigation_.state == RecoveryState::Executing) {
            if (fact.generation == navigation_.target_generation) {
                if (navigation_.attempts < std::numeric_limits<std::uint8_t>::max()) {
                    ++navigation_.attempts;
                }
                navigation_.generation = fact.generation;
                navigation_.baseline = fact.consecutive;
                navigation_.state = RecoveryState::Verifying;
                publish(events, time, SubsystemAction::RestartTask, RecoveryTarget::NavigationTask,
                        navigation_.attempts, fact.generation);
            } else {
                command.restart_navigation = true;
            }
        }

        if (navigation_.state != RecoveryState::Verifying) {
            return;
        }
        if (fact.observation_pending && fact.observation_generation == navigation_.generation &&
            fact.on_time_run >= limits::kRecoveryVerifyCount) {
            navigation_.state = RecoveryState::Succeeded;
            publish(events, time, SubsystemAction::RestartTask, RecoveryTarget::NavigationTask,
                    navigation_.attempts, navigation_.generation, RecoveryNotice::Succeeded);
            return;
        }
        if (!five_new_misses(fact.consecutive, navigation_.baseline) || !fact.active) {
            return;
        }
        if (navigation_.attempts >= limits::kNavigationRestartAttempts ||
            fact.generation == std::numeric_limits<std::uint32_t>::max()) {
            navigation_.state = RecoveryState::Failed;
            publish(events, time, SubsystemAction::RestartTask, RecoveryTarget::NavigationTask,
                    navigation_.attempts, navigation_.generation, RecoveryNotice::Failed);
            return;
        }
        navigation_.state = RecoveryState::Executing;
        navigation_.target_generation = fact.generation + 1;
        command.restart_navigation = true;
    }

    void observe_gps(const GpsRecoveryFact& fact, TimePoint time,
                     core::EventLog<SystemEvent<TimePoint>>& events, RecoveryCommand& command) {
        if (gps_.state == RecoveryState::Idle && fact.primary_selected && fact.primary_persistent) {
            gps_.state = RecoveryState::Executing;
            gps_.isolated = true;
            gps_.attempts = 1;
            gps_.window_open = false;
            command.failover_to_backup = true;
            gps_.state = RecoveryState::Verifying;
            publish(events, time, SubsystemAction::SwitchSensor, RecoveryTarget::BackupGps, 1, 0);
            return;
        }
        if (gps_.state != RecoveryState::Verifying) {
            return;
        }
        if (!gps_.window_open) {
            gps_.window_open = true;
        }
        if (fact.backup_usable_run >= limits::kRecoveryVerifyCount) {
            gps_.state = RecoveryState::Succeeded;
            publish(events, time, SubsystemAction::SwitchSensor, RecoveryTarget::BackupGps, 1, 0,
                    RecoveryNotice::Succeeded);
            return;
        }
        if (fact.backup_unusable_run >= limits::kRecoveryVerifyCount) {
            gps_.state = RecoveryState::Failed;
            publish(events, time, SubsystemAction::SwitchSensor, RecoveryTarget::BackupGps, 1, 0,
                    RecoveryNotice::Failed);
        }
    }

    Episode navigation_{};
    Episode gps_{};
};

} // namespace ares::flight
