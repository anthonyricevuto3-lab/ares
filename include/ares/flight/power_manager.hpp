#pragma once

#include "ares/hardware/interfaces.hpp"

namespace ares::flight {

// Reads IBatteryMonitor. It does not change spacecraft mode.
template <typename TimePoint> class PowerManager {
public:
    explicit PowerManager(const hardware::IBatteryMonitor<TimePoint>& source) : source_(source) {}

    const hardware::BatterySample<TimePoint>& sample() {
        latest_ = source_.read();
        return latest_;
    }

    [[nodiscard]] const hardware::BatterySample<TimePoint>& state() const noexcept {
        return latest_;
    }

private:
    const hardware::IBatteryMonitor<TimePoint>& source_;
    hardware::BatterySample<TimePoint> latest_{};
};

} // namespace ares::flight
