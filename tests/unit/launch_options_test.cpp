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
