#include "ares/core/logger.hpp"

#include <sstream>
#include <string>
#include <thread>

#include <gtest/gtest.h>

namespace core = ares::core;
using namespace std::chrono_literals;
using Log = core::Logger<core::ManualClock>;

TEST(Logger, FiltersBelowMinimumAndStampsMonotonicTime) {
    core::ManualClock clock;
    std::ostringstream output;
    Log logger(output, clock, Log::Level::Info);

    logger.debug("mode", "hidden");
    ASSERT_EQ(clock.advance(5ms), core::AdvanceStatus::Applied);
    logger.info("mode", "ready");
    logger.warn("mode", "caution");
    logger.error("mode", "failed");

    EXPECT_EQ(output.str(), "5 INFO mode ready\n5 WARN mode caution\n5 ERROR mode failed\n");
    EXPECT_FALSE(logger.enabled(Log::Level::Debug));
    EXPECT_TRUE(logger.enabled(Log::Level::Info));
}

TEST(Logger, CollapsesNewlinesIntoOneRecord) {
    core::ManualClock clock;
    std::ostringstream output;
    Log logger(output, clock);
    logger.info("mode", "a\nb\r");
    EXPECT_EQ(output.str(), "0 INFO mode a b \n");
}

TEST(Logger, ConcurrentLinesStayIntact) {
    core::ManualClock clock;
    std::ostringstream output;
    Log logger(output, clock);
    constexpr int lines_per_thread = 50;

    {
        std::jthread alpha([&logger] {
            for (int line = 0; line < lines_per_thread; ++line) {
                logger.info("load", "alpha");
            }
        });
        std::jthread beta([&logger] {
            for (int line = 0; line < lines_per_thread; ++line) {
                logger.info("load", "beta");
            }
        });
    }

    std::istringstream input(output.str());
    std::string line;
    int alpha = 0;
    int beta = 0;
    int other = 0;
    while (std::getline(input, line)) {
        if (line == "0 INFO load alpha") {
            ++alpha;
        } else if (line == "0 INFO load beta") {
            ++beta;
        } else {
            ++other;
        }
    }
    EXPECT_EQ(alpha, lines_per_thread);
    EXPECT_EQ(beta, lines_per_thread);
    EXPECT_EQ(other, 0);
}
