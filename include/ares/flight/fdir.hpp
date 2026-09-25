#pragma once

#include "ares/core/clock.hpp"
#include "ares/core/event_log.hpp"
#include "ares/flight/executive.hpp"
#include "ares/flight/fault_detection.hpp"
#include "ares/flight/fault_events.hpp"
#include "ares/flight/fault_mailbox.hpp"
#include "ares/flight/fault_policy.hpp"
#include "ares/flight/system_event.hpp"

#include <cstdint>
#include <limits>
#include <stop_token>

namespace ares::flight {

struct BatteryObservationResult {
    RegistryStatus sensor{RegistryStatus::Unchanged};
    RegistryStatus power{RegistryStatus::Unchanged};
};

// Owns FaultRegistry mutation, deadline severity, and SafeMode recovery count.
// Not thread-safe. Other tasks publish into FaultMailbox. This object is the
// consumer. Mode changes go through FlightExecutive::request_mode. A rejected
// or absent request leaves the mode alone. evaluate() does not advance the
// SafeMode counter; apply() does, once per health cycle.
template <core::Clock C, std::size_t Capacity = limits::kFaultRegistryCapacity>
class FdirController {
public:
    using time_point = typename C::time_point;
    using Registry = FaultRegistry<time_point, Capacity>;

    explicit FdirController(core::EventLog<SystemEvent<time_point>>& events,
                            FaultPolicyLimits policy = {}, BatteryThresholds battery = {})
        : events_(events), policy_(policy), battery_(battery) {}

    [[nodiscard]] RegistryStatus observe_sensor(FaultSource source, SampleUsability usability,
                                                time_point time) {
        if (!is_sensor_source(source)) {
            return RegistryStatus::RejectedSource;
        }
        const SensorDetection detection = detect_sensor(usability);
        if (detection.action == SensorDetectionAction::Hold) {
            return RegistryStatus::Unchanged;
        }
        if (detection.action == SensorDetectionAction::Clear) {
            return clear_sensor_faults(source, time);
        }
        return raise_sensor(source, detection.type, time);
    }

    [[nodiscard]] BatteryObservationResult
    observe_battery(SampleUsability usability, hardware::Millivolts voltage, time_point time) {
        BatteryObservationResult result;
        result.sensor = observe_sensor(FaultSource::Battery, usability, time);
        const BatteryCommand command = detect_low_battery(usability, voltage, battery_);
        if (command == BatteryCommand::Activate) {
            result.power = raise_one(FaultType::LowBattery, FaultSource::Battery, time);
            return result;
        }
        if (command == BatteryCommand::Clear) {
            result.power = clear_one(FaultType::LowBattery, FaultSource::Battery, time);
            return result;
        }
        result.power = RegistryStatus::Unchanged;
        return result;
    }

    [[nodiscard]] RegistryStatus observe_deadline(FaultSource source, DeadlineFact fact,
                                                  time_point time) {
        if (!is_task_source(source)) {
            return RegistryStatus::RejectedSource;
        }
        const DeadlineCommand command = detect_deadline(fact);
        if (command == DeadlineCommand::Raise) {
            return raise_deadline(source, time);
        }
        if (command == DeadlineCommand::Clear) {
            return clear_one(FaultType::DeadlineMiss, source, time);
        }
        return RegistryStatus::Unchanged;
    }

    // A sensor window applies Usable first when one was seen, then the newest
    // non-Usable sample. Future and TimeError still hold, so they clear nothing
    // on their own and raise nothing after a Usable clear.
    void ingest(const typename FaultMailbox<C>::Snapshot& sample, time_point time) {
        ingest_sensor(FaultSource::Imu, sample.imu, time);
        ingest_sensor(FaultSource::Gps, sample.gps, time);
        ingest_sensor(FaultSource::Temperature, sample.temperature, time);
        if (sample.battery.pending) {
            (void)observe_battery(sample.battery.usability, sample.battery.voltage, time);
        }
        if (sample.navigation.pending) {
            (void)observe_deadline(FaultSource::NavigationTask, deadline_fact(sample.navigation),
                                   time);
        }
        if (sample.health.pending) {
            (void)observe_deadline(FaultSource::HealthTask, deadline_fact(sample.health), time);
        }
        if (sample.communications.pending) {
            (void)observe_deadline(FaultSource::CommunicationsTask,
                                   deadline_fact(sample.communications), time);
        }
    }

    [[nodiscard]] PolicyDecision evaluate(SpacecraftMode current) const {
        return evaluate_fault_policy(registry_, current, policy_);
    }

    // Requests a mode only when policy named one. The returned decision keeps
    // that request and the machine's TransitionStatus as separate fields.
    // Logging inside the executive cannot roll the transition back. The
    // registry is not updated from the transition result. One call is one
    // SafeMode recovery sample.
    [[nodiscard]] PolicyDecision apply(FlightExecutive<C>& executive) {
        const SpacecraftMode current = executive.mode();
        PolicyDecision decision = evaluate(current);
        consider_safe_recovery(decision, current);
        if (decision.requested_mode.has_value()) {
            decision.transition = executive.request_mode(*decision.requested_mode);
        }
        return decision;
    }

    [[nodiscard]] const Registry& registry() const noexcept { return registry_; }
    [[nodiscard]] std::uint32_t safe_recovery_streak() const noexcept {
        return safe_recovery_streak_;
    }

private:
    void ingest_sensor(FaultSource source, const typename FaultMailbox<C>::SensorSlot& slot,
                       time_point time) {
        if (!slot.pending) {
            return;
        }
        if (slot.usable_seen) {
            (void)observe_sensor(source, SampleUsability::Usable, time);
        }
        if (slot.usability != SampleUsability::Usable) {
            (void)observe_sensor(source, slot.usability, time);
        }
    }

    [[nodiscard]] static DeadlineFact
    deadline_fact(const typename FaultMailbox<C>::DeadlineSlot& slot) noexcept {
        return slot.missed ? DeadlineFact::Missed : DeadlineFact::OnTime;
    }

    [[nodiscard]] RegistryStatus raise_deadline(FaultSource source, time_point time) {
        const FaultRecord<time_point>* existing = registry_.find(FaultType::DeadlineMiss, source);
        FaultSeverity previous = FaultSeverity::Advisory;
        bool was_active = false;
        std::uint32_t next_count = 1;
        if (existing != nullptr && existing->active) {
            was_active = true;
            previous = existing->severity;
            next_count = existing->consecutive_count;
            if (next_count < std::numeric_limits<std::uint32_t>::max()) {
                ++next_count;
            }
        }
        const FaultSeverity severity = deadline_severity(next_count);
        const RegistryStatus status =
            registry_.raise(FaultType::DeadlineMiss, source, severity, time);
        if (status != RegistryStatus::Activated && status != RegistryStatus::Updated) {
            return status;
        }
        const FaultRecord<time_point>* record = registry_.find(FaultType::DeadlineMiss, source);
        if (status == RegistryStatus::Activated && record != nullptr) {
            (void)events_.publish(SystemEvent<time_point>{FaultActivatedEvent<time_point>{
                record->type, record->source, record->severity, record->last_detected}});
        }
        if (status == RegistryStatus::Updated && was_active && record != nullptr &&
            record->severity != previous) {
            (void)events_.publish(SystemEvent<time_point>{
                FaultUpdatedEvent<time_point>{record->type, record->source, record->severity,
                                              record->consecutive_count, record->last_detected}});
        }
        return status;
    }

    void consider_safe_recovery(PolicyDecision& decision, SpacecraftMode mode) {
        if (mode != SpacecraftMode::SafeMode || safe_mode_exit_blocked(registry_, policy_)) {
            safe_recovery_streak_ = 0;
            return;
        }
        if (safe_recovery_streak_ < limits::kSafeModeRecoveryCycles) {
            ++safe_recovery_streak_;
        }
        if (safe_recovery_streak_ >= limits::kSafeModeRecoveryCycles) {
            decision.action = RecoveryAction::RecoverToStandby;
            decision.requested_mode = SpacecraftMode::Standby;
        }
    }

    [[nodiscard]] RegistryStatus clear_sensor_faults(FaultSource source, time_point time) {
        bool cleared = false;
        for (const FaultType type : kSensorHealthFaults) {
            if (clear_one(type, source, time) == RegistryStatus::Cleared) {
                cleared = true;
            }
        }
        return cleared ? RegistryStatus::Cleared : RegistryStatus::Unchanged;
    }

    [[nodiscard]] RegistryStatus raise_sensor(FaultSource source, FaultType raised,
                                              time_point time) {
        for (const FaultType type : kSensorHealthFaults) {
            if (type == raised) {
                continue;
            }
            (void)clear_one(type, source, time);
        }
        return raise_one(raised, source, time);
    }

    [[nodiscard]] RegistryStatus raise_one(FaultType type, FaultSource source, time_point time) {
        const FaultSeverity severity = severity_of(type);
        const RegistryStatus status = registry_.raise(type, source, severity, time);
        const FaultRecord<time_point>* record = registry_.find(type, source);
        if (status == RegistryStatus::Activated && record != nullptr) {
            (void)events_.publish(SystemEvent<time_point>{FaultActivatedEvent<time_point>{
                record->type, record->source, record->severity, record->last_detected}});
        }
        const std::uint32_t required =
            policy_.warning_consecutive == 0 ? 1U : policy_.warning_consecutive;
        if (status == RegistryStatus::Updated && record != nullptr &&
            record->severity == FaultSeverity::Warning && record->consecutive_count == required) {
            (void)events_.publish(SystemEvent<time_point>{
                FaultUpdatedEvent<time_point>{record->type, record->source, record->severity,
                                              record->consecutive_count, record->last_detected}});
        }
        return status;
    }

    [[nodiscard]] RegistryStatus clear_one(FaultType type, FaultSource source, time_point time) {
        const RegistryStatus status = registry_.clear(type, source);
        if (status == RegistryStatus::Cleared) {
            (void)events_.publish(
                SystemEvent<time_point>{FaultClearedEvent<time_point>{type, source, time}});
        }
        return status;
    }

    core::EventLog<SystemEvent<time_point>>& events_;
    FaultPolicyLimits policy_;
    BatteryThresholds battery_;
    Registry registry_{};
    std::uint32_t safe_recovery_streak_{0};
};

// Health calls this once per cycle. A stop request returns before consume and
// before apply, so a pending observation is left in the mailbox and no mode
// request is made. The caller discards that mailbox with the runtime after
// the workers have been joined.
template <core::Clock C>
[[nodiscard]] PolicyDecision run_fdir_cycle(FdirController<C>& controller, FaultMailbox<C>& mailbox,
                                            FlightExecutive<C>& executive,
                                            core::ClockSample<typename C::time_point> now,
                                            std::stop_token stop) {
    if (stop.stop_requested() || now.status != core::ClockStatus::Ok) {
        return {};
    }
    controller.ingest(mailbox.consume(), now.time);
    return controller.apply(executive);
}

} // namespace ares::flight
