#include "ares/core/logger.hpp"

#include <chrono>
#include <string>
#include <utility>

namespace ares::core {
namespace {

[[nodiscard]] std::string_view level_name(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "INVALID";
}

[[nodiscard]] std::string one_line(std::string_view text) {
    std::string clean;
    clean.reserve(text.size());
    for (const char character : text) {
        if (character == '\n' || character == '\r') {
            clean.push_back(' ');
        } else {
            clean.push_back(character);
        }
    }
    return clean;
}

template <Clock C>
void write_log(std::ostream& out, std::mutex& mutex, std::string& staging, bool& flushing,
               const C& clock, std::string_view level, std::string_view component,
               std::string_view message) {
    const ClockSample<typename C::time_point> sample = clock.now();
    std::chrono::milliseconds millis{0};
    const bool stamp_ok =
        sample.status == ClockStatus::Ok && checked_duration_convert(sample.time.time_since_epoch(), millis);
    const std::string line = (stamp_ok ? std::to_string(millis.count()) : std::string("clock-error")) +
                             " " + std::string(level) + " " +
                             one_line(component) + " " + one_line(message);
    std::unique_lock lock(mutex);
    staging.append(line);
    staging.push_back('\n');
    if (flushing) {
        return;
    }
    flushing = true;
    while (!staging.empty()) {
        std::string batch;
        batch.swap(staging);
        lock.unlock();
        try {
            out << batch;
            out.flush();
        } catch (...) {
            lock.lock();
            flushing = false;
            throw;
        }
        lock.lock();
    }
    flushing = false;
}

} // namespace

template <Clock C>
Logger<C>::Logger(std::ostream& out, C& clock, Level minimum)
    : out_(out), clock_(clock), minimum_(minimum) {}

template <Clock C>
void Logger<C>::log(Level level, std::string_view component, std::string_view message) {
    if (!enabled(level)) {
        return;
    }
    write_log(out_, mutex_, staging_, flushing_, clock_, level_name(level), component, message);
}

template class Logger<ManualClock>;
template class Logger<SteadyClock>;

} // namespace ares::core
