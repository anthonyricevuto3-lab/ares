#pragma once

#include "ares/core/clock.hpp"
#include "ares/flight/fault.hpp"
#include "ares/flight/freshness.hpp"
#include "ares/hardware/units.hpp"

#include <mutex>

namespace ares::flight {

// Latest observation per source, plus a sticky deadline miss.
// Several flight tasks publish. The FDIR owner is the only consumer.
// The mutex covers this mailbox only. It is not a lock around FaultRegistry.
//
// An IMU, GPS, or temperature slot is fixed-size. It keeps the newest
// usability and whether any Usable sample arrived since the last consume.
// A later non-Usable sample does not clear that flag. consume() copies both
// and then clears them. It is not a queue of every intermediate sample.
// Battery is one sample, published by the health task in the cycle that
// consumes it, so that slot has no multi-sample window.
// A deadline miss stays set until consume() takes it; an on-time result
// posted while that miss is still pending does not erase it.
template <core::Clock C> class FaultMailbox {
public:
    struct SensorSlot {
        bool pending{false};
        // Set if Usable was published since the previous consume, even when a
        // later sample replaced that Usable as the newest usability.
        bool usable_seen{false};
        SampleUsability usability{SampleUsability::Unavailable};
    };

    struct BatterySlot {
        bool pending{false};
        SampleUsability usability{SampleUsability::Unavailable};
        hardware::Millivolts voltage{};
    };

    struct DeadlineSlot {
        bool pending{false};
        bool missed{false};
    };

    struct Snapshot {
        SensorSlot imu{};
        SensorSlot gps{};
        SensorSlot temperature{};
        BatterySlot battery{};
        DeadlineSlot navigation{};
        DeadlineSlot health{};
        DeadlineSlot communications{};
    };

    FaultMailbox() = default;
    FaultMailbox(const FaultMailbox&) = delete;
    FaultMailbox& operator=(const FaultMailbox&) = delete;
    FaultMailbox(FaultMailbox&&) = delete;
    FaultMailbox& operator=(FaultMailbox&&) = delete;

    void publish_sensor(FaultSource source, SampleUsability usability) {
        std::lock_guard lock(mutex_);
        SensorSlot* slot = sensor_slot(source);
        if (slot == nullptr) {
            return;
        }
        if (usability == SampleUsability::Usable) {
            slot->usable_seen = true;
        }
        slot->usability = usability;
        slot->pending = true;
    }

    void publish_battery(SampleUsability usability, hardware::Millivolts voltage) {
        std::lock_guard lock(mutex_);
        battery_.usability = usability;
        battery_.voltage = voltage;
        battery_.pending = true;
    }

    void publish_deadline(FaultSource source, bool missed) {
        std::lock_guard lock(mutex_);
        DeadlineSlot* slot = deadline_slot(source);
        if (slot == nullptr) {
            return;
        }
        if (missed) {
            slot->missed = true;
        } else if (!slot->pending) {
            slot->missed = false;
        }
        slot->pending = true;
    }

    [[nodiscard]] Snapshot consume() {
        std::lock_guard lock(mutex_);
        Snapshot out;
        out.imu = imu_;
        out.gps = gps_;
        out.temperature = temperature_;
        out.battery = battery_;
        out.navigation = navigation_;
        out.health = health_;
        out.communications = communications_;
        retire(imu_);
        retire(gps_);
        retire(temperature_);
        battery_.pending = false;
        navigation_.pending = false;
        health_.pending = false;
        communications_.pending = false;
        return out;
    }

private:
    static void retire(SensorSlot& slot) noexcept {
        slot.pending = false;
        slot.usable_seen = false;
    }

    [[nodiscard]] SensorSlot* sensor_slot(FaultSource source) noexcept {
        switch (source) {
        case FaultSource::Imu:
            return &imu_;
        case FaultSource::Gps:
            return &gps_;
        case FaultSource::Temperature:
            return &temperature_;
        case FaultSource::Battery:
        case FaultSource::NavigationTask:
        case FaultSource::HealthTask:
        case FaultSource::CommunicationsTask:
            return nullptr;
        }
        return nullptr;
    }

    [[nodiscard]] DeadlineSlot* deadline_slot(FaultSource source) noexcept {
        switch (source) {
        case FaultSource::NavigationTask:
            return &navigation_;
        case FaultSource::HealthTask:
            return &health_;
        case FaultSource::CommunicationsTask:
            return &communications_;
        case FaultSource::Imu:
        case FaultSource::Gps:
        case FaultSource::Battery:
        case FaultSource::Temperature:
            return nullptr;
        }
        return nullptr;
    }

    std::mutex mutex_{};
    SensorSlot imu_{};
    SensorSlot gps_{};
    SensorSlot temperature_{};
    BatterySlot battery_{};
    DeadlineSlot navigation_{};
    DeadlineSlot health_{};
    DeadlineSlot communications_{};
};

} // namespace ares::flight
