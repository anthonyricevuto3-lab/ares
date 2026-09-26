#include "ares/launch_options.hpp"

#include <initializer_list>
#include <span>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

ares::ArgumentParse parse(std::initializer_list<std::string_view> args) {
    return ares::parse_arguments(std::span<const std::string_view>(args.begin(), args.size()));
}

} // namespace

TEST(LaunchOptions, EmptyArgumentsUseTheDefaultDuration) {
    const ares::ArgumentParse parsed = parse({});
    EXPECT_EQ(parsed.status, ares::ArgumentStatus::Ok);
    EXPECT_EQ(parsed.options.run_for, std::chrono::milliseconds{250});
    EXPECT_TRUE(parsed.message.empty());
}

TEST(LaunchOptions, SkipsProgramNameAndParsesDuration) {
    const ares::ArgumentParse parsed = parse({"ares", "--duration-ms", "10"});
    EXPECT_EQ(parsed.status, ares::ArgumentStatus::Ok);
    EXPECT_EQ(parsed.options.run_for, std::chrono::milliseconds{10});
}

TEST(LaunchOptions, AcceptsZeroDuration) {
    const ares::ArgumentParse parsed = parse({"--duration-ms", "0"});
    EXPECT_EQ(parsed.status, ares::ArgumentStatus::Ok);
    EXPECT_EQ(parsed.options.run_for, std::chrono::milliseconds{0});
}

TEST(LaunchOptions, HelpDoesNotRequireAProgramName) {
    const ares::ArgumentParse parsed = parse({"--help"});
    EXPECT_EQ(parsed.status, ares::ArgumentStatus::Help);
    EXPECT_NE(parsed.message.find("Usage:"), std::string::npos);
    EXPECT_NE(parsed.message.find("ARES v0.7.0"), std::string::npos);
    EXPECT_NE(parsed.message.find("--list-scenarios"), std::string::npos);
}

TEST(LaunchOptions, ListsScenariosInCatalogOrder) {
    const ares::ArgumentParse parsed = parse({"--list-scenarios"});
    EXPECT_EQ(parsed.status, ares::ArgumentStatus::ListScenarios);
    const std::string& text = parsed.message;
    EXPECT_EQ(text.find("nominal"), 0U);
    EXPECT_LT(text.find("nominal"), text.find("gps_stale"));
    EXPECT_LT(text.find("gps_stale"), text.find("nav_restart"));
    EXPECT_LT(text.find("nav_restart"), text.find("restart_fail"));
    EXPECT_NE(text.find("Primary GPS freeze"), std::string::npos);
}

TEST(LaunchOptions, DefaultsToTheNominalScenarioAndAcceptsASeed) {
    const ares::ArgumentParse parsed = parse({"ares", "--scenario", "gps_stale", "--seed", "42"});
    EXPECT_EQ(parsed.status, ares::ArgumentStatus::Ok);
    EXPECT_EQ(parsed.options.scenario, "gps_stale");
    EXPECT_EQ(parsed.options.seed, 42U);
    EXPECT_EQ(parse({}).options.scenario, "nominal");
    EXPECT_EQ(parse({}).options.seed, 0U);
    EXPECT_EQ(parse({"--scenario"}).message, "missing value for --scenario");
    EXPECT_EQ(parse({"--seed", "-1"}).message, "invalid --seed value");
    EXPECT_EQ(parse({"--scenario", "gps_stale", "--scenario", "nominal"}).message,
              "duplicate --scenario");
    EXPECT_EQ(parse({"--record", "mission.bin"}).options.record_path, "mission.bin");
    EXPECT_EQ(parse({"--record"}).message, "missing value for --record");
    EXPECT_EQ(parse({"--record", "a.bin", "--record", "b.bin"}).message, "duplicate --record");
}

TEST(LaunchOptions, RejectsDuplicateMissingAndInvalidDurations) {
    EXPECT_EQ(parse({"--duration-ms"}).status, ares::ArgumentStatus::Error);
    EXPECT_EQ(parse({"--duration-ms", "12x"}).message, "invalid --duration-ms value");
    EXPECT_EQ(parse({"--duration-ms", "-1"}).status, ares::ArgumentStatus::Error);
    EXPECT_EQ(parse({"--duration-ms", "1", "--duration-ms", "2"}).message,
              "duplicate --duration-ms");
    EXPECT_EQ(parse({"ares", "extra"}).message, "unknown argument: extra");
    const std::string huge(20, '9');
    EXPECT_EQ(parse({"--duration-ms", huge}).status, ares::ArgumentStatus::Error);
}
