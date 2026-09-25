#pragma once

#include "ares/core/clock.hpp"
#include "ares/flight/fault.hpp"
#include "ares/flight/freshness.hpp"
#include "ares/hardware/units.hpp"

#include <cstdint>
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
        // Final consecutive physical-sample runs. Not cleared by consume, so a
        // run can span health cycles. A non-usable sample zeros the usable run.
        std::uint8_t usable_run{0};
        std::uint8_t unusable_run{0};
    };

    struct BatterySlot {
        bool pending{false};
        SampleUsability usability{SampleUsability::Unavailable};
        hardware::Millivolts voltage{};
    };

    struct DeadlineSlot {
        bool pending{false};
        bool missed{false};
        // Generation that produced on_time_run. A generation change zeros the run
        // so an older completion cannot verify the new generation.
        std::uint32_t generation{0};
        std::uint8_t on_time_run{0};
    };

    struct Snapshot {
        SensorSlot imu{};
        SensorSlot gps{};
        SensorSlot backup_gps{};
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
            slot->unusable_run = 0;
            if (slot->usable_run < 3) {
                ++slot->usable_run;
            }
        } else {
            slot->usable_run = 0;
            if (slot->unusable_run < 3) {
                ++slot->unusable_run;
            }
        }
        slot->usability = usability;
        slot->pending = true;
    }

    // Starts a new backup verification window. Does not touch fault records.
    void rebaseline_backup_run() {
        std::lock_guard lock(mutex_);
        backup_gps_.usable_run = 0;
        backup_gps_.unusable_run = 0;
    }

    void publish_battery(SampleUsability usability, hardware::Millivolts voltage) {
        std::lock_guard lock(mutex_);
        battery_.usability = usability;
        battery_.voltage = voltage;
        battery_.pending = true;
    }

    void publish_deadline(FaultSource source, bool missed, std::uint32_t generation = 0) {
        std::lock_guard lock(mutex_);
        DeadlineSlot* slot = deadline_slot(source);
        if (slot == nullptr) {
            return;
        }
        if (slot->generation != generation) {
            slot->on_time_run = 0;
            slot->generation = generation;
        }
        if (missed) {
            slot->missed = true;
            slot->on_time_run = 0;
        } else {
            if (!slot->pending) {
                slot->missed = false;
            }
            if (slot->on_time_run < 3) {
                ++slot->on_time_run;
            }
        }
        slot->pending = true;
    }

    [[nodiscard]] Snapshot consume() {
        std::lock_guard lock(mutex_);
        Snapshot out;
        out.imu = imu_;
        out.gps = gps_;
        out.backup_gps = backup_gps_;
        out.temperature = temperature_;
        out.battery = battery_;
        out.navigation = navigation_;
        out.health = health_;
        out.communications = communications_;
        retire(imu_);
        retire(gps_);
        retire(backup_gps_);
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
        case FaultSource::PrimaryGps:
            return &gps_;
        case FaultSource::BackupGps:
            return &backup_gps_;
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
        case FaultSource::PrimaryGps:
        case FaultSource::BackupGps:
        case FaultSource::Battery:
        case FaultSource::Temperature:
            return nullptr;
        }
        return nullptr;
    }

    std::mutex mutex_{};
    SensorSlot imu_{};
    SensorSlot gps_{};
    SensorSlot backup_gps_{};
    SensorSlot temperature_{};
    BatterySlot battery_{};
    DeadlineSlot navigation_{};
    DeadlineSlot health_{};
    DeadlineSlot communications_{};
};

} // namespace ares::flight
