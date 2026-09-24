#include "ares/flight/mode_machine.hpp"

#include <utility>

namespace ares::flight {
namespace {

class FlagGuard {
public:
    explicit FlagGuard(bool& flag) : flag_(flag) { flag_ = true; }
    ~FlagGuard() { flag_ = false; }

    FlagGuard(const FlagGuard&) = delete;
    FlagGuard& operator=(const FlagGuard&) = delete;

private:
    bool& flag_;
};

} // namespace

bool is_legal_transition(SpacecraftMode from, SpacecraftMode to) noexcept {
    switch (from) {
    case SpacecraftMode::Boot:
        return to == SpacecraftMode::Initialization;
    case SpacecraftMode::Initialization:
        return to == SpacecraftMode::Standby;
    case SpacecraftMode::Standby:
        return to == SpacecraftMode::Nominal;
    case SpacecraftMode::Nominal:
        return to == SpacecraftMode::Standby || to == SpacecraftMode::Degraded ||
               to == SpacecraftMode::SafeMode || to == SpacecraftMode::Emergency;
    case SpacecraftMode::Degraded:
        return to == SpacecraftMode::Nominal || to == SpacecraftMode::SafeMode ||
               to == SpacecraftMode::Emergency;
    case SpacecraftMode::SafeMode:
        return to == SpacecraftMode::Standby || to == SpacecraftMode::Emergency;
    case SpacecraftMode::Emergency:
        return to == SpacecraftMode::SafeMode;
    }
    return false;
}

template <core::Clock C> ModeMachine<C>::ModeMachine(C& clock) : clock_(clock) {}

template <core::Clock C> void ModeMachine<C>::set_on_change(ModeHandler handler) {
    on_change_ = std::move(handler);
}

template <core::Clock C> void ModeMachine<C>::set_on_reject(RejectHandler handler) {
    on_reject_ = std::move(handler);
}

template <core::Clock C> SpacecraftMode ModeMachine<C>::mode() const noexcept {
    return mode_;
}

template <core::Clock C> bool ModeMachine<C>::can_transition(SpacecraftMode target) const noexcept {
    if (notifying_ || in_reject_callback_) {
        return false;
    }
    return is_known_mode(mode_) && is_known_mode(target) && is_legal_transition(mode_, target);
}

template <core::Clock C>
void ModeMachine<C>::reject(SpacecraftMode from, SpacecraftMode attempted,
                            TransitionRejectReason reason) {
    if (!on_reject_ || in_reject_callback_) {
        return;
    }
    const FlagGuard guard(in_reject_callback_);
    const core::ClockSample<time_point> sample = clock_.now();
    const time_point stamp = sample.status == core::ClockStatus::Ok ? sample.time : time_point{};
    on_reject_(ModeTransitionRejected<time_point>{from, attempted, reason, stamp});
}

template <core::Clock C> TransitionStatus ModeMachine<C>::transition(SpacecraftMode target) {
    if (notifying_ || in_reject_callback_) {
        if (!in_reject_callback_) {
            reject(mode_, target, TransitionRejectReason::Reentrant);
        }
        return TransitionStatus::Rejected;
    }

    const SpacecraftMode from = mode_;
    if (!is_known_mode(from) || !is_known_mode(target)) {
        reject(from, target, TransitionRejectReason::InvalidMode);
        return TransitionStatus::Rejected;
    }
    if (!is_legal_transition(from, target)) {
        reject(from, target, TransitionRejectReason::Illegal);
        return TransitionStatus::Rejected;
    }

    mode_ = target;
    if (on_change_) {
        const FlagGuard guard(notifying_);
        const core::ClockSample<time_point> sample = clock_.now();
        const time_point stamp = sample.status == core::ClockStatus::Ok ? sample.time : time_point{};
        on_change_(ModeChangedEvent<time_point>{from, target, stamp});
    }
    return TransitionStatus::Accepted;
}

template class ModeMachine<core::ManualClock>;
template class ModeMachine<core::SteadyClock>;

} // namespace ares::flight
