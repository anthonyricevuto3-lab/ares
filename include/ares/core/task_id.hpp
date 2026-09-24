#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ares::core {

// Fixed-capacity task name. Copied by value into cycle records so the periodic
// path does not allocate a std::string.
class TaskId {
public:
    static constexpr std::size_t kCapacity = 16;

    [[nodiscard]] static std::optional<TaskId> make(std::string_view name) {
        if (name.empty() || name.size() > kCapacity || name.find('\n') != std::string_view::npos ||
            name.find('\r') != std::string_view::npos) {
            return std::nullopt;
        }
        TaskId id;
        for (std::size_t index = 0; index < name.size(); ++index) {
            id.chars_.at(index) = name.at(index);
        }
        id.length_ = static_cast<std::uint8_t>(name.size());
        return id;
    }

    [[nodiscard]] std::string_view text() const noexcept {
        return std::string_view{chars_.data(), length_};
    }

    [[nodiscard]] bool operator==(const TaskId&) const = default;

private:
    std::array<char, kCapacity> chars_{};
    std::uint8_t length_{0};
};

} // namespace ares::core
