#include "ares/flight/freshness.hpp"

#include <chrono>

#include <gtest/gtest.h>

namespace flight = ares::flight;
namespace hardware = ares::hardware;
using namespace std::chrono_literals;
using Time = ares::core::ManualEpoch::time_point;

TEST(Freshness, FreshValidSampleIsUsable) {
    EXPECT_EQ(flight::evaluate_freshness(hardware::SensorStatus::Valid, Time{}, Time{}, 100ms),
              flight::SampleUsability::Usable);
    EXPECT_EQ(flight::evaluate_freshness(hardware::SensorStatus::Valid, Time{}, Time{100ms}, 100ms),
              flight::SampleUsability::Usable);
}

TEST(Freshness, OldValidSampleIsStale) {
    EXPECT_EQ(
        flight::evaluate_freshness(hardware::SensorStatus::Valid, Time{}, Time{100ms + 1ns}, 100ms),
        flight::SampleUsability::Stale);
}

TEST(Freshness, InvalidStatusStaysInvalid) {
    EXPECT_EQ(flight::evaluate_freshness(hardware::SensorStatus::Invalid, Time{}, Time{}, 1s),
              flight::SampleUsability::Invalid);
}

TEST(Freshness, UnavailableStatusStaysUnavailable) {
    EXPECT_EQ(flight::evaluate_freshness(hardware::SensorStatus::Unavailable, Time{}, Time{}, 1s),
              flight::SampleUsability::Unavailable);
}

TEST(Freshness, ReportedStaleStaysStaleWhenYoung) {
    EXPECT_EQ(flight::evaluate_freshness(hardware::SensorStatus::Stale, Time{}, Time{}, 1s),
              flight::SampleUsability::Stale);
}

TEST(Freshness, FutureTimestampIsNotUsable) {
    EXPECT_EQ(flight::evaluate_freshness(hardware::SensorStatus::Valid, Time{5s}, Time{}, 1s),
              flight::SampleUsability::Future);
    EXPECT_EQ(flight::evaluate_freshness(hardware::SensorStatus::Invalid, Time{5s}, Time{}, 1s),
              flight::SampleUsability::Future);
}

TEST(Freshness, NegativeLimitAndUnrepresentableAgeAreErrors) {
    EXPECT_EQ(flight::evaluate_freshness(hardware::SensorStatus::Valid, Time{}, Time{}, -1ns),
              flight::SampleUsability::TimeError);
    const Time earliest{ares::core::Duration::min()};
    const Time latest{ares::core::Duration::max()};
    EXPECT_EQ(flight::evaluate_freshness(hardware::SensorStatus::Valid, earliest, latest, 1s),
              flight::SampleUsability::TimeError);
    EXPECT_EQ(flight::evaluate_freshness(hardware::SensorStatus::Invalid, earliest, latest, 1s),
              flight::SampleUsability::TimeError);
    EXPECT_EQ(flight::evaluate_freshness(hardware::SensorStatus::Stale, earliest, latest, 1s),
              flight::SampleUsability::TimeError);
    EXPECT_EQ(flight::evaluate_freshness(hardware::SensorStatus::Unavailable, earliest, latest, 1s),
              flight::SampleUsability::TimeError);
}

TEST(Freshness, BoundariesKeepReportedStatusUnlessValidIsOld) {
    EXPECT_EQ(flight::evaluate_freshness(hardware::SensorStatus::Valid, Time{}, Time{100ms}, 100ms),
              flight::SampleUsability::Usable);
    EXPECT_EQ(
        flight::evaluate_freshness(hardware::SensorStatus::Valid, Time{}, Time{100ms + 1ns}, 100ms),
        flight::SampleUsability::Stale);
    EXPECT_EQ(flight::evaluate_freshness(hardware::SensorStatus::Valid, Time{1ns}, Time{}, 1s),
              flight::SampleUsability::Future);
    EXPECT_EQ(flight::evaluate_freshness(hardware::SensorStatus::Invalid, Time{}, Time{10s}, 1s),
              flight::SampleUsability::Invalid);
    EXPECT_EQ(
        flight::evaluate_freshness(hardware::SensorStatus::Unavailable, Time{}, Time{10s}, 1s),
        flight::SampleUsability::Unavailable);
    EXPECT_EQ(flight::evaluate_freshness(hardware::SensorStatus::Stale, Time{}, Time{10s}, 1s),
              flight::SampleUsability::Stale);
    EXPECT_EQ(flight::evaluate_freshness(hardware::SensorStatus::Valid, Time{}, Time{}, -1ns),
              flight::SampleUsability::TimeError);
}
