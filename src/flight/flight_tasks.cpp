#include "ares/flight/flight_tasks.hpp"

#include "ares/flight/navigation.hpp"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <system_error>

namespace ares::flight {
namespace {

template <core::Clock C>
void record_cycle(std::atomic<std::uint64_t>& cycles, std::atomic<std::uint64_t>& formats,
                  core::Logger<C>& logger, std::string_view name, std::string_view action,
                  typename C::time_point scheduled, std::stop_token stop) {
    if (stop.stop_requested()) {
        return;
    }
    cycles.fetch_add(1, std::memory_order_acq_rel);
    if (!logger.enabled(core::LogLevel::Debug)) {
        return;
    }
    formats.fetch_add(1, std::memory_order_acq_rel);

    char buffer[64];
    char* cursor = buffer;
    char* const end = buffer + sizeof(buffer);
    const auto append = [&](std::string_view text) {
        if (static_cast<std::size_t>(end - cursor) < text.size()) {
            return false;
        }
        for (const char character : text) {
            *cursor = character;
            ++cursor;
        }
        return true;
    };
    if (!append(action) || !append(" scheduled_ms=")) {
        return;
    }
    const auto scheduled_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(scheduled.time_since_epoch()).count();
    const auto result = std::to_chars(cursor, end, scheduled_ms);
    if (result.ec != std::errc{}) {
        return;
    }
    logger.debug(name, std::string_view{buffer, static_cast<std::size_t>(result.ptr - buffer)});
}

} // namespace

template <core::Clock C>
void HealthPulse<C>::operator()(typename C::time_point scheduled, std::stop_token stop) {
    record_cycle(cycles_, debug_formats_, logger_, name, action, scheduled, stop);
}

template <core::Clock C>
void NavigationCadence<C>::operator()(typename C::time_point scheduled, std::stop_token stop) {
    if (!stop.stop_requested() && imu_ != nullptr && clock_ != nullptr &&
        (gps_ != nullptr || selector_ != nullptr)) {
        if (generation_ != nullptr) {
            const std::uint32_t generation = generation_->load(std::memory_order_acquire);
            if (generation != seen_generation_) {
                solution_ = {};
                last_usable_.reset();
                seen_generation_ = generation;
            }
        }
        const auto imu = imu_->read();
        hardware::GpsSample<time_point> selected;
        if (selector_ != nullptr) {
            const typename GpsSelector<C>::Sample pair = selector_->read_both();
            primary_gps_ = pair.primary;
            backup_gps_ = pair.backup;
            selected = pair.selected;
        } else {
            selected = gps_->read();
            primary_gps_ = selected;
        }
        const core::ClockSample<time_point> now = clock_->now();
        solution_ = combine_navigation(imu, selected, now.status, now.time, limits_);
        if (solution_.usability == SampleUsability::Usable) {
            last_usable_ = solution_;
        }
    }
    record_cycle(cycles_, debug_formats_, logger_, name, action, scheduled, stop);
}

template <core::Clock C>
void CommBeacon<C>::operator()(typename C::time_point scheduled, std::stop_token stop) {
    record_cycle(cycles_, debug_formats_, logger_, name, action, scheduled, stop);
}

template class HealthPulse<core::ManualClock>;
template class HealthPulse<core::SteadyClock>;
template class NavigationCadence<core::ManualClock>;
template class NavigationCadence<core::SteadyClock>;
template class CommBeacon<core::ManualClock>;
template class CommBeacon<core::SteadyClock>;

} // namespace ares::flight
