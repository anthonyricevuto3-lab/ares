#include "ares/simulation/random_source.hpp"

#include <limits>

#include <gtest/gtest.h>

namespace simulation = ares::simulation;

TEST(DeterministicRng, SameSeedRepeats) {
    simulation::DeterministicRng first{42};
    simulation::DeterministicRng second{42};
    for (int draw = 0; draw < 8; ++draw) {
        EXPECT_EQ(first.next(), second.next());
    }
}

TEST(DeterministicRng, DifferentSeedDiverges) {
    simulation::DeterministicRng first{1};
    simulation::DeterministicRng second{2};
    EXPECT_NE(first.next(), second.next());
}

TEST(DeterministicRng, ZeroAmplitudeDoesNotDraw) {
    simulation::DeterministicRng rng{7};
    (void)rng.next();
    simulation::DeterministicRng replay{7};
    (void)replay.next();
    std::int64_t offset = 5;
    EXPECT_TRUE(rng.try_offset(0, offset));
    EXPECT_EQ(offset, 0);
    EXPECT_TRUE(rng.try_offset(-3, offset));
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(rng.next(), replay.next());
}

TEST(DeterministicRng, OffsetStaysInsideAmplitude) {
    simulation::DeterministicRng rng{99};
    constexpr std::int64_t kAmplitude = 4;
    for (int draw = 0; draw < 32; ++draw) {
        std::int64_t offset = 0;
        ASSERT_TRUE(rng.try_offset(kAmplitude, offset));
        EXPECT_GE(offset, -kAmplitude);
        EXPECT_LE(offset, kAmplitude);
    }
}

TEST(DeterministicRng, SensorStreamsAreIndependent) {
    constexpr std::uint64_t kMission = 11;
    const auto imu = simulation::derive_sensor_seed(kMission, simulation::SensorStream::Imu);
    const auto gps = simulation::derive_sensor_seed(kMission, simulation::SensorStream::Gps);
    const auto battery =
        simulation::derive_sensor_seed(kMission, simulation::SensorStream::Battery);
    const auto thermal =
        simulation::derive_sensor_seed(kMission, simulation::SensorStream::Temperature);
    EXPECT_NE(imu, gps);
    EXPECT_NE(gps, battery);
    EXPECT_NE(battery, thermal);

    simulation::DeterministicRng imu_stream{imu};
    simulation::DeterministicRng gps_stream{gps};
    simulation::DeterministicRng gps_alone{gps};
    for (int draw = 0; draw < 16; ++draw) {
        (void)imu_stream.next();
    }
    EXPECT_EQ(gps_stream.next(), gps_alone.next());
    EXPECT_EQ(simulation::derive_sensor_seed(kMission, simulation::SensorStream::Gps), gps);
}

TEST(DeterministicRng, AmplitudeLimitsDoNotWrapTheDraw) {
    constexpr auto kMax = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t kLargest = (kMax - 1) / 2;
    simulation::DeterministicRng rng{1234};
    simulation::DeterministicRng replay{1234};
    std::int64_t offset = 0;
    ASSERT_TRUE(rng.try_offset(1, offset));
    EXPECT_GE(offset, -1);
    EXPECT_LE(offset, 1);
    (void)replay.next();
    EXPECT_EQ(rng.next(), replay.next());

    simulation::DeterministicRng wide{99};
    simulation::DeterministicRng wide_replay{99};
    ASSERT_TRUE(wide.try_offset(kLargest, offset));
    EXPECT_GE(offset, -kLargest);
    EXPECT_LE(offset, kLargest);
    (void)wide_replay.next();
    EXPECT_EQ(wide.next(), wide_replay.next());

    simulation::DeterministicRng rejected{99};
    simulation::DeterministicRng rejected_replay{99};
    offset = 77;
    EXPECT_FALSE(rejected.try_offset(kLargest + 1, offset));
    EXPECT_EQ(offset, 77);
    EXPECT_EQ(rejected.next(), rejected_replay.next());
}
