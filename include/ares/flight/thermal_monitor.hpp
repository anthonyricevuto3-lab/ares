#pragma once

#include "ares/hardware/interfaces.hpp"

namespace ares::flight {

// Reads ITemperatureSensor. It does not change spacecraft mode.
template <typename TimePoint> class ThermalMonitor {
public:
    explicit ThermalMonitor(const hardware::ITemperatureSensor<TimePoint>& source)
        : source_(source) {}

    const hardware::TemperatureSample<TimePoint>& sample() {
        latest_ = source_.read();
        return latest_;
    }

    [[nodiscard]] const hardware::TemperatureSample<TimePoint>& state() const noexcept {
        return latest_;
    }

private:
    const hardware::ITemperatureSensor<TimePoint>& source_;
    hardware::TemperatureSample<TimePoint> latest_{};
};

} // namespace ares::flight
