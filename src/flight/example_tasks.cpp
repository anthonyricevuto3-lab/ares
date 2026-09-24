#include "ares/flight/example_tasks.hpp"

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
    std::size_t used = 0;
    const auto append = [&](std::string_view text) {
        if (used + text.size() > sizeof(buffer)) {
            return false;
        }
        for (const char character : text) {
            buffer[used] = character;
            ++used;
        }
        return true;
    };
    if (!append(action) || !append(" scheduled_ms=")) {
        return;
    }
    const auto scheduled_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(scheduled.time_since_epoch()).count();
    const auto result = std::to_chars(buffer + used, buffer + sizeof(buffer), scheduled_ms);
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
