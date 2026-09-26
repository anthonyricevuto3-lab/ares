#include "ares/flight/flight_tasks.hpp"

#include <sstream>
#include <string>

#include <gtest/gtest.h>

namespace core = ares::core;
namespace flight = ares::flight;

TEST(ExampleTasks, EachTaskRecordsOneNamedCycle) {
    core::ManualClock clock;
    std::ostringstream output;
    core::Logger<core::ManualClock> logger(output, clock, core::LogLevel::Debug);

    flight::HealthPulse<core::ManualClock> health(logger);
    flight::NavigationCadence<core::ManualClock> navigation(logger);
    flight::CommBeacon<core::ManualClock> comms(logger);

    health(core::ManualClock::time_point{}, {});
    navigation(core::ManualClock::time_point{}, {});
    comms(core::ManualClock::time_point{}, {});

    EXPECT_EQ(health.debug_formats(), 1U);
    EXPECT_EQ(navigation.debug_formats(), 1U);
    EXPECT_EQ(comms.debug_formats(), 1U);
    EXPECT_EQ(health.cycles(), 1U);
    EXPECT_EQ(navigation.cycles(), 1U);
    EXPECT_EQ(comms.cycles(), 1U);
    const std::string text = output.str();
    EXPECT_NE(text.find("health"), std::string::npos);
    EXPECT_NE(text.find("pulse"), std::string::npos);
    EXPECT_NE(text.find("navigation"), std::string::npos);
    EXPECT_NE(text.find("cadence"), std::string::npos);
    EXPECT_NE(text.find("comms"), std::string::npos);
    EXPECT_NE(text.find("beacon"), std::string::npos);
}

TEST(ExampleTasks, FilteredDebugDoesNotFormat) {
    core::ManualClock clock;
    std::ostringstream output;
    core::Logger<core::ManualClock> logger(output, clock, core::LogLevel::Info);
    flight::HealthPulse<core::ManualClock> health(logger);
    flight::NavigationCadence<core::ManualClock> navigation(logger);
    flight::CommBeacon<core::ManualClock> comms(logger);

    health(core::ManualClock::time_point{}, {});
    navigation(core::ManualClock::time_point{}, {});
    comms(core::ManualClock::time_point{}, {});

    EXPECT_EQ(health.cycles(), 1U);
    EXPECT_EQ(navigation.cycles(), 1U);
    EXPECT_EQ(comms.cycles(), 1U);
    EXPECT_EQ(health.debug_formats(), 0U);
    EXPECT_EQ(navigation.debug_formats(), 0U);
    EXPECT_EQ(comms.debug_formats(), 0U);
    EXPECT_EQ(output.str().find("scheduled_ms"), std::string::npos);
}
