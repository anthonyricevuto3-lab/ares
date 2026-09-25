#pragma once

#include "ares/core/event_log.hpp"
#include "ares/core/logger.hpp"
#include "ares/flight/command.hpp"
#include "ares/flight/mode_machine.hpp"
#include "ares/flight/system_event.hpp"

#include <cstdint>
#include <functional>
#include <utility>

namespace ares::flight {

struct BootResult {
    TransitionStatus status{TransitionStatus::Rejected};
    SpacecraftMode mode{SpacecraftMode::Boot};
};

// Mission bootstrap. Clock, logger, and the event log must outlive the executive.
// boot_to_standby reports the mode actually reached, including a partial boot.
template <core::Clock C> class FlightExecutive {
public:
    using time_point = typename C::time_point;

    FlightExecutive(C& clock, core::Logger<C>& logger,
                    core::EventLog<SystemEvent<time_point>>& events);

    FlightExecutive(const FlightExecutive&) = delete;
    FlightExecutive& operator=(const FlightExecutive&) = delete;
    FlightExecutive(FlightExecutive&&) = delete;
    FlightExecutive& operator=(FlightExecutive&&) = delete;

    [[nodiscard]] BootResult boot_to_standby();
    [[nodiscard]] CommandStatus accept(Command command);
    [[nodiscard]] SpacecraftMode mode() const noexcept;
    // Same commit-then-notify path as every other transition. The mode is
    // committed before logging. A logging failure does not undo it.
    [[nodiscard]] TransitionStatus request_mode(SpacecraftMode target);
    // Test seam. Invoked at the end of a mode-change callback, and again at the
    // end of a rejection callback. A nested request from the change callback is
    // reentrant. A nested request from the rejection callback is rejected and
    // is not published again. Flight leaves this empty.
    void set_mode_callback_probe(std::function<void()> probe) { probe_ = std::move(probe); }

private:
    [[nodiscard]] time_point stamp() const;
    void store(const SystemEvent<time_point>& event);
    C& clock_;
    core::Logger<C>& logger_;
    core::EventLog<SystemEvent<time_point>>& events_;
    ModeMachine<C> modes_;
    std::function<void()> probe_{};
};

extern template class FlightExecutive<core::ManualClock>;
extern template class FlightExecutive<core::SteadyClock>;

} // namespace ares::flight
