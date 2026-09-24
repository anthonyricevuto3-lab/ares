#pragma once

#include "ares/hardware/samples.hpp"

namespace ares::hardware {

// Flight software depends on these types only. A simulated sensor and a future
// hardware sensor are interchangeable behind the same reference.

template <typename TimePoint> class IImu {
public:
    virtual ~IImu() = default;
    IImu(const IImu&) = delete;
    IImu& operator=(const IImu&) = delete;
    IImu(IImu&&) = delete;
    IImu& operator=(IImu&&) = delete;

    [[nodiscard]] virtual ImuSample<TimePoint> read() const = 0;

protected:
    IImu() = default;
};

template <typename TimePoint> class IGps {
public:
    virtual ~IGps() = default;
    IGps(const IGps&) = delete;
    IGps& operator=(const IGps&) = delete;
    IGps(IGps&&) = delete;
    IGps& operator=(IGps&&) = delete;

    [[nodiscard]] virtual GpsSample<TimePoint> read() const = 0;

protected:
    IGps() = default;
};

template <typename TimePoint> class IBatteryMonitor {
public:
    virtual ~IBatteryMonitor() = default;
    IBatteryMonitor(const IBatteryMonitor&) = delete;
    IBatteryMonitor& operator=(const IBatteryMonitor&) = delete;
    IBatteryMonitor(IBatteryMonitor&&) = delete;
    IBatteryMonitor& operator=(IBatteryMonitor&&) = delete;

    [[nodiscard]] virtual BatterySample<TimePoint> read() const = 0;

protected:
    IBatteryMonitor() = default;
};

template <typename TimePoint> class ITemperatureSensor {
public:
    virtual ~ITemperatureSensor() = default;
    ITemperatureSensor(const ITemperatureSensor&) = delete;
    ITemperatureSensor& operator=(const ITemperatureSensor&) = delete;
    ITemperatureSensor(ITemperatureSensor&&) = delete;
    ITemperatureSensor& operator=(ITemperatureSensor&&) = delete;

    [[nodiscard]] virtual TemperatureSample<TimePoint> read() const = 0;

protected:
    ITemperatureSensor() = default;
};

} // namespace ares::hardware
