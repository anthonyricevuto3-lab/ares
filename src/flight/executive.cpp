#include "ares/flight/executive.hpp"

#include <string>
#include <utility>

namespace ares::flight {
namespace {

// Logging is best-effort after the mode and the typed event are already stored.
void logging_failed() noexcept {}

} // namespace

template <core::Clock C> typename C::time_point FlightExecutive<C>::stamp() const {
    const core::ClockSample<time_point> sample = clock_.now();
    return sample.status == core::ClockStatus::Ok ? sample.time : time_point{};
}

template <core::Clock C> void FlightExecutive<C>::store(const SystemEvent<time_point>& event) {
    (void)events_.publish(event);
}

template <core::Clock C>
FlightExecutive<C>::FlightExecutive(C& clock, core::Logger<C>& logger,
                                    core::EventLog<SystemEvent<time_point>>& events)
    : clock_(clock), logger_(logger), events_(events), modes_(clock) {
    modes_.set_on_change([this](const ModeChangedEvent<time_point>& event) {
        store(SystemEvent<time_point>{event});
        try {
            if (events_.overflowed()) {
                logger_.warn("event", "flight event log overwrote the oldest record");
            }
            logger_.info("mode", std::string(to_string(event.from)) + " -> " +
                                     std::string(to_string(event.to)));
        } catch (...) {
            logging_failed();
        }
        if (probe_) {
            probe_();
        }
    });
    modes_.set_on_reject([this](const ModeTransitionRejected<time_point>& event) {
        store(SystemEvent<time_point>{event});
        try {
            logger_.warn("mode", std::string(to_string(event.from)) + " -> " +
                                     std::string(to_string(event.attempted)) + " rejected (" +
                                     std::string(to_string(event.reason)) + ")");
        } catch (...) {
            logging_failed();
        }
    });
}

template <core::Clock C> BootResult FlightExecutive<C>::boot_to_standby() {
    if (modes_.mode() != SpacecraftMode::Boot) {
        const SpacecraftMode current = modes_.mode();
        store(SystemEvent<time_point>{ModeTransitionRejected<time_point>{
            current, current, TransitionRejectReason::BootRejected, stamp()}});
        try {
            logger_.warn("executive",
                         std::string("boot rejected while in ") + std::string(to_string(current)));
        } catch (...) {
            logging_failed();
        }
        return BootResult{TransitionStatus::Rejected, current};
    }
    if (modes_.transition(SpacecraftMode::Initialization) != TransitionStatus::Accepted) {
        return BootResult{TransitionStatus::Rejected, modes_.mode()};
    }
    if (modes_.transition(SpacecraftMode::Standby) != TransitionStatus::Accepted) {
        return BootResult{TransitionStatus::Rejected, modes_.mode()};
    }
    return BootResult{TransitionStatus::Accepted, SpacecraftMode::Standby};
}

template <core::Clock C> CommandStatus FlightExecutive<C>::accept(Command command) {
    const auto reject_command = [this, command] {
        store(SystemEvent<time_point>{CommandEvent<time_point>{command, false, stamp()}});
        try {
            logger_.warn("command", std::string(to_string(command)) + " rejected");
        } catch (...) {
            logging_failed();
        }
        return CommandStatus::Rejected;
    };
    if (command != Command::StartMission) {
        return reject_command();
    }
    if (modes_.transition(SpacecraftMode::Nominal) != TransitionStatus::Accepted) {
        return reject_command();
    }
    store(SystemEvent<time_point>{CommandEvent<time_point>{command, true, stamp()}});
    try {
        logger_.info("command", std::string(to_string(command)) + " accepted");
    } catch (...) {
        logging_failed();
    }
    return CommandStatus::Accepted;
}

template <core::Clock C> SpacecraftMode FlightExecutive<C>::mode() const noexcept {
    return modes_.mode();
}

template <core::Clock C> TransitionStatus FlightExecutive<C>::request_mode(SpacecraftMode target) {
    return modes_.transition(target);
}

template class FlightExecutive<core::ManualClock>;
template class FlightExecutive<core::SteadyClock>;

} // namespace ares::flight
