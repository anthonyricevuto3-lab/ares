#pragma once

#include "ares/core/clock.hpp"
#include "ares/flight/freshness.hpp"
#include "ares/hardware/interfaces.hpp"

#include <optional>

namespace ares::flight {

// Reads IBatteryMonitor and evaluates freshness. It does not change spacecraft mode.
// latest_observation() is the newest read, including unusable samples.
// last_usable() changes only when usability() is Usable. SensorStatus alone is not acceptance.
template <core::Clock C> class PowerManager {
public:
    using time_point = typename C::time_point;

    PowerManager(const hardware::IBatteryMonitor<time_point>& source, C& clock,
                 core::Duration max_age)
        : source_(source), clock_(clock), max_age_(max_age) {}

    const hardware::BatterySample<time_point>& sample() {
        latest_observation_ = source_.read();
        const core::ClockSample<time_point> now = clock_.now();
        if (now.status != core::ClockStatus::Ok) {
            usability_ = SampleUsability::TimeError;
        } else {
            usability_ = evaluate_freshness(latest_observation_.status, latest_observation_.time,
                                            now.time, max_age_);
        }
        if (usability_ == SampleUsability::Usable) {
            last_usable_ = latest_observation_;
        }
        return latest_observation_;
    }

    [[nodiscard]] const hardware::BatterySample<time_point>& latest_observation() const noexcept {
        return latest_observation_;
    }

    [[nodiscard]] const std::optional<hardware::BatterySample<time_point>>&
    last_usable() const noexcept {
        return last_usable_;
    }

    [[nodiscard]] SampleUsability usability() const noexcept { return usability_; }

private:
    const hardware::IBatteryMonitor<time_point>& source_;
    C& clock_;
    core::Duration max_age_;
    hardware::BatterySample<time_point> latest_observation_{};
    std::optional<hardware::BatterySample<time_point>> last_usable_{};
    SampleUsability usability_{SampleUsability::Unavailable};
};

} // namespace ares::flight
