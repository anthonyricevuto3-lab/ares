#pragma once

#include "ares/hardware/interfaces.hpp"
#include "ares/hardware/samples.hpp"

#include <atomic>
#include <cstdint>

namespace ares::flight {

enum class GpsSelection : std::uint8_t { Primary = 0, Backup = 1 };

// One packed word so selection and isolation change together.
// Health stores. Navigation loads once per sample.
template <core::Clock C> class GpsSelector {
public:
    using time_point = typename C::time_point;
    struct Sample {
        hardware::GpsSample<time_point> primary{};
        hardware::GpsSample<time_point> backup{};
        hardware::GpsSample<time_point> selected{};
        GpsSelection selection{GpsSelection::Primary};
        bool primary_isolated{false};
    };

    GpsSelector(const hardware::IGps<time_point>& primary, const hardware::IGps<time_point>& backup)
        : primary_(primary), backup_(backup) {}

    void failover_to_backup() noexcept {
        state_.store(pack(GpsSelection::Backup, true), std::memory_order_release);
    }

    [[nodiscard]] GpsSelection selection() const noexcept {
        return unpack(state_.load(std::memory_order_acquire)).selection;
    }

    [[nodiscard]] bool primary_isolated() const noexcept {
        return unpack(state_.load(std::memory_order_acquire)).isolated;
    }

    // Reads each device once. The selection used for `selected` is the snapshot
    // taken before either read.
    [[nodiscard]] Sample read_both() const {
        const View view = unpack(state_.load(std::memory_order_acquire));
        Sample sample;
        sample.primary = primary_.read();
        sample.backup = backup_.read();
        sample.selection = view.selection;
        sample.primary_isolated = view.isolated;
        sample.selected = view.selection == GpsSelection::Backup ? sample.backup : sample.primary;
        return sample;
    }

private:
    struct View {
        GpsSelection selection{GpsSelection::Primary};
        bool isolated{false};
    };

    static std::uint16_t pack(GpsSelection selection, bool isolated) noexcept {
        const auto bits = static_cast<std::uint32_t>(selection);
        return static_cast<std::uint16_t>((bits << 8U) | (isolated ? 1U : 0U));
    }

    static View unpack(std::uint16_t word) noexcept {
        View view;
        view.selection = static_cast<GpsSelection>(word >> 8);
        view.isolated = (word & 1U) != 0U;
        return view;
    }

    const hardware::IGps<time_point>& primary_;
    const hardware::IGps<time_point>& backup_;
    std::atomic<std::uint16_t> state_{0};
};

} // namespace ares::flight
