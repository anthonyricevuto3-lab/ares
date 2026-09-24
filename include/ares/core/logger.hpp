#pragma once

#include "ares/core/clock.hpp"

#include <cstdint>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>

namespace ares::core {

enum class LogLevel : std::uint8_t { Debug, Info, Warn, Error };

template <Clock C> class Logger {
public:
    using Level = LogLevel;

    Logger(std::ostream& out, C& clock, Level minimum = Level::Info);
    ~Logger() = default;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    [[nodiscard]] bool enabled(Level level) const noexcept {
        return static_cast<int>(level) >= static_cast<int>(minimum_);
    }
    void debug(std::string_view component, std::string_view message) {
        if (!enabled(Level::Debug)) {
            return;
        }
        log(Level::Debug, component, message);
    }
    void info(std::string_view component, std::string_view message) {
        log(Level::Info, component, message);
    }
    void warn(std::string_view component, std::string_view message) {
        log(Level::Warn, component, message);
    }
    void error(std::string_view component, std::string_view message) {
        log(Level::Error, component, message);
    }
    void log(Level level, std::string_view component, std::string_view message);

private:
    std::ostream& out_;
    C& clock_;
    Level minimum_;
    std::mutex mutex_;
    std::string staging_{};
    bool flushing_{false};
};

extern template class Logger<ManualClock>;
extern template class Logger<SteadyClock>;

} // namespace ares::core
