#include "ares/recorder/replay.hpp"
#include "ares/version.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] bool read_file(const std::string& path, std::vector<std::uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        return false;
    }
    input.seekg(0, std::ios::beg);
    std::vector<char> raw(static_cast<std::size_t>(size));
    input.read(raw.data(), size);
    if (!input && !input.eof()) {
        return false;
    }
    bytes.assign(raw.begin(), raw.end());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    bool summary_only = false;
    bool verify_only = false;
    std::string path;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index] == nullptr ? "" : argv[index];
        if (argument == "--summary") {
            summary_only = true;
            continue;
        }
        if (argument == "--verify") {
            verify_only = true;
            continue;
        }
        if (argument == "--help" || argument == "-h") {
            std::cout << "ARES replay " << ares::kVersionString
                      << "\n"
                         "Usage: ares-replay [--verify] [--summary] FILE\n"
                         "\n"
                         "--verify    Print valid or invalid and exit 0 or 1.\n"
                         "--summary   Print the summary without the timeline.\n"
                         "FILE        Recording written by ares --record.\n";
            return 0;
        }
        if (!path.empty() || argument.empty() || argument.starts_with('-')) {
            std::cerr << "Usage: ares-replay [--verify] [--summary] FILE\n";
            return 2;
        }
        path = std::string(argument);
    }
    if (path.empty()) {
        std::cerr << "Usage: ares-replay [--verify] [--summary] FILE\n";
        return 2;
    }
    std::vector<std::uint8_t> bytes;
    if (!read_file(path, bytes)) {
        std::cerr << "record open failed: " << path << '\n';
        return 1;
    }
    const ares::recorder::ReplayReport report = ares::recorder::replay_bytes(bytes);
    if (verify_only && !summary_only) {
        if (report.ok) {
            std::cout << "valid\n";
            return 0;
        }
        std::cout << "invalid: " << report.error << "\n";
        return 1;
    }
    if (!summary_only) {
        std::cout << ares::recorder::format_timeline(report);
    }
    std::cout << ares::recorder::format_summary(report);
    return report.ok ? 0 : 1;
}
