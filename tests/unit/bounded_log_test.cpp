#include "ares/core/bounded_log.hpp"

#include <array>

#include <gtest/gtest.h>

TEST(BoundedLog, OverwritesTheOldestAndCountsOverflow) {
    ares::core::BoundedLog<int, 2> log;
    EXPECT_EQ(log.size(), 0U);
    EXPECT_FALSE(log.overflowed());
    EXPECT_EQ(log.overwrite_count(), 0U);
    using Push = ares::core::BoundedLog<int, 2>::Push;
    EXPECT_EQ(log.push(1), Push::Stored);
    EXPECT_EQ(log.push(2), Push::Stored);
    EXPECT_EQ(log.push(3), Push::Overwrote);
    EXPECT_TRUE(log.overflowed());
    EXPECT_EQ(log.overwrite_count(), 1U);
    EXPECT_EQ(log.size(), 2U);
    std::array<int, 2> values{};
    EXPECT_EQ(log.copy_into(values), 2U);
    EXPECT_EQ(values[0], 2);
    EXPECT_EQ(values[1], 3);
}
