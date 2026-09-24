#include "ares/core/event_log.hpp"

#include <thread>

#include <gtest/gtest.h>

TEST(EventLog, StartsEmpty) {
    ares::core::EventLog<int> log;
    EXPECT_EQ(log.size(), 0U);
    EXPECT_FALSE(log.overflowed());
    EXPECT_TRUE(log.snapshot().empty());
}

TEST(EventLog, ConcurrentPublishKeepsEveryEventInsideCapacity) {
    ares::core::EventLog<int, 256> log;
    constexpr int count = 100;
    {
        std::jthread ones([&log] {
            for (int value = 0; value < count; ++value) {
                EXPECT_EQ(log.publish(1), decltype(log)::Status::Stored);
            }
        });
        std::jthread twos([&log] {
            for (int value = 0; value < count; ++value) {
                EXPECT_EQ(log.publish(2), decltype(log)::Status::Stored);
            }
        });
    }

    const auto values = log.snapshot();
    ASSERT_EQ(values.size(), static_cast<std::size_t>(count * 2));
    EXPECT_FALSE(log.overflowed());
    int ones = 0;
    int twos = 0;
    for (const int value : values) {
        if (value == 1) {
            ++ones;
        } else if (value == 2) {
            ++twos;
        }
    }
    EXPECT_EQ(ones, count);
    EXPECT_EQ(twos, count);
}

TEST(EventLog, FullBufferKeepsTheNewest) {
    ares::core::EventLog<int, 2> log;
    EXPECT_EQ(log.publish(1), decltype(log)::Status::Stored);
    EXPECT_EQ(log.publish(2), decltype(log)::Status::Stored);
    EXPECT_EQ(log.publish(3), decltype(log)::Status::Overwrote);
    EXPECT_TRUE(log.overflowed());
    EXPECT_EQ(log.overwrite_count(), 1U);
    const auto values = log.snapshot();
    ASSERT_EQ(values.size(), 2U);
    EXPECT_EQ(values[0], 2);
    EXPECT_EQ(values[1], 3);
}
