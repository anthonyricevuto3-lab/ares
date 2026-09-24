#pragma once

#include "ares/core/clock.hpp"
#include "ares/flight/events.hpp"
#include "ares/flight/mode.hpp"

#include <cstdint>
#include <functional>

namespace ares::flight {

enum class TransitionStatus : std::uint8_t { Accepted, Rejected };

[[nodiscard]] bool is_legal_transition(SpacecraftMode from, SpacecraftMode to) noexcept;

// Not thread-safe. transition() commits, then notifies. A transition from a
// change callback is rejected and published as Reentrant. A transition from
// inside the rejection callback is rejected and is not published again.
// Clock must outlive the machine.
template <core::Clock C> class ModeMachine {
public:
    using time_point = typename C::time_point;
    using ModeHandler = std::function<void(const ModeChangedEvent<time_point>&)>;
    using RejectHandler = std::function<void(const ModeTransitionRejected<time_point>&)>;

    explicit ModeMachine(C& clock);

    ModeMachine(const ModeMachine&) = delete;
    ModeMachine& operator=(const ModeMachine&) = delete;
    ModeMachine(ModeMachine&&) = delete;
    ModeMachine& operator=(ModeMachine&&) = delete;

    void set_on_change(ModeHandler handler);
    void set_on_reject(RejectHandler handler);
    [[nodiscard]] SpacecraftMode mode() const noexcept;
    [[nodiscard]] bool can_transition(SpacecraftMode target) const noexcept;
    [[nodiscard]] TransitionStatus transition(SpacecraftMode target);

private:
    void reject(SpacecraftMode from, SpacecraftMode attempted, TransitionRejectReason reason);

    C& clock_;
    SpacecraftMode mode_{SpacecraftMode::Boot};
    ModeHandler on_change_{};
    RejectHandler on_reject_{};
    bool notifying_{false};
    bool in_reject_callback_{false};
};

extern template class ModeMachine<core::ManualClock>;
extern template class ModeMachine<core::SteadyClock>;

} // namespace ares::flight
