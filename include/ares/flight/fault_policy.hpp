#pragma once

#include "ares/flight/fault_registry.hpp"
#include "ares/flight/fdir_limits.hpp"
#include "ares/flight/mode_machine.hpp"

#include <cstdint>
#include <optional>

namespace ares::flight {

// Consecutive Warning detections required before Degraded. Zero is treated as one
// so a blank limit cannot mean "every active warning, including a count of zero".
struct FaultPolicyLimits {
    std::uint32_t warning_consecutive{limits::kWarningPersistence};
};

enum class RecoveryAction : std::uint8_t { None, ContinueDegraded, EnterSafeMode };

// action is the recovery the faults require. It is not proof that a mode
// change occurred. requested_mode is set only when the current mode has a
// legal edge to that target. transition is empty until FdirController::apply
// records what the mode machine returned. Emergency is not requested.
// SafeMode is not left by this policy.
struct PolicyDecision {
    RecoveryAction action{RecoveryAction::None};
    std::optional<SpacecraftMode> requested_mode{};
    std::optional<TransitionStatus> transition{};
};

template <typename TimePoint, std::size_t Capacity>
[[nodiscard]] PolicyDecision
evaluate_fault_policy(const FaultRegistry<TimePoint, Capacity>& registry, SpacecraftMode current,
                      FaultPolicyLimits limits) noexcept {
    const std::uint32_t required =
        limits.warning_consecutive == 0 ? 1U : limits.warning_consecutive;

    bool critical = registry.saturated();
    bool persistent_warning = false;
    registry.for_each_active([&](const FaultRecord<TimePoint>& fault) {
        if (fault.severity == FaultSeverity::Critical) {
            critical = true;
        }
        if (fault.severity == FaultSeverity::Warning && fault.consecutive_count >= required) {
            persistent_warning = true;
        }
    });

    PolicyDecision decision;
    if (critical) {
        decision.action = RecoveryAction::EnterSafeMode;
        if (current == SpacecraftMode::Nominal || current == SpacecraftMode::Degraded) {
            decision.requested_mode = SpacecraftMode::SafeMode;
        }
        return decision;
    }
    if (persistent_warning) {
        decision.action = RecoveryAction::ContinueDegraded;
        if (current == SpacecraftMode::Nominal) {
            decision.requested_mode = SpacecraftMode::Degraded;
        }
        return decision;
    }
    decision.action = RecoveryAction::None;
    if (current == SpacecraftMode::Degraded) {
        decision.requested_mode = SpacecraftMode::Nominal;
    }
    return decision;
}

} // namespace ares::flight
