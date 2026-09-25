#pragma once

#include "ares/core/clock.hpp"
#include "ares/core/logger.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>

namespace ares::simulation {

// Simulation-side condition. This is not a fault record and not a mode request.
enum class InjectionKind : std::uint8_t {
    SensorUnavailable,
    SensorInvalid,
    SensorFreeze,
    BatteryVoltageOverride,
    TaskExecutionDelay,
};

enum class ChaosTarget : std::uint8_t {
    Imu,
    Gps,
    Battery,
    NavigationTask,
    CommunicationsTask,
};

// Active while epoch + start <= now < epoch + start + duration.
// parameter is millivolts for a battery override and nanoseconds for a task delay.
struct ChaosEvent {
    InjectionKind kind{InjectionKind::SensorFreeze};
    ChaosTarget target{ChaosTarget::Gps};
    core::Duration start{core::Duration::zero()};
    core::Duration duration{core::Duration::zero()};
    std::int64_t parameter{0};
    std::uint16_t sequence{0};
};

inline constexpr std::size_t kChaosEventCapacity = 16;

struct ChaosEdge {
    InjectionKind kind{InjectionKind::SensorFreeze};
    ChaosTarget target{ChaosTarget::Gps};
    core::Duration at{core::Duration::zero()};
    bool started{false};
    std::uint16_t sequence{0};
};

inline constexpr std::size_t kChaosEdgeCapacity = kChaosEventCapacity * 2;

// copied is how many edges were stored. truncated is true when more edges were
// pending than the caller buffer could hold. Injection state does not depend on this.
struct EdgeCopyResult {
    std::size_t copied{0};
    bool truncated{false};
};

// What a device would observe at one simulation time. Tests compare this.
struct InjectionView {
    bool imu_unavailable{false};
    bool imu_invalid{false};
    bool imu_frozen{false};
    bool gps_unavailable{false};
    bool gps_invalid{false};
    bool gps_frozen{false};
    std::optional<std::int64_t> battery_millivolts{};
    core::Duration navigation_delay{core::Duration::zero()};
    core::Duration communications_delay{core::Duration::zero()};

    constexpr bool operator==(const InjectionView&) const = default;
};

[[nodiscard]] constexpr std::string_view injection_kind_name(InjectionKind kind) noexcept {
    switch (kind) {
    case InjectionKind::SensorUnavailable:
        return "unavailable";
    case InjectionKind::SensorInvalid:
        return "invalid";
    case InjectionKind::SensorFreeze:
        return "freeze";
    case InjectionKind::BatteryVoltageOverride:
        return "battery";
    case InjectionKind::TaskExecutionDelay:
        return "delay";
    }
    return "injection";
}

[[nodiscard]] constexpr std::string_view chaos_target_name(ChaosTarget target) noexcept {
    switch (target) {
    case ChaosTarget::Imu:
        return "imu";
    case ChaosTarget::Gps:
        return "gps";
    case ChaosTarget::Battery:
        return "battery";
    case ChaosTarget::NavigationTask:
        return "navigation";
    case ChaosTarget::CommunicationsTask:
        return "communications";
    }
    return "target";
}

// Immutable schedule after load. Queries do not allocate and do not take a lock.
// note() is the only mutator after load, and one task calls it.
// The engine does not know about the fault registry, the mode machine, or FDIR.
template <core::Clock C> class ChaosEngine {
public:
    using time_point = typename C::time_point;

    // Strong load. A failure leaves the previously installed schedule unchanged.
    // Overlapping events on the same target are rejected. Different targets may overlap.
    // A zero duration, or a start+duration that does not fit, does not form an interval.
    [[nodiscard]] bool load(std::span<const ChaosEvent> events, time_point epoch) {
        if (events.size() > kChaosEventCapacity || !schedule_accepted(events)) {
            return false;
        }
        count_ = 0;
        for (const ChaosEvent& event : events) {
            ChaosEvent stored = event;
            stored.sequence = static_cast<std::uint16_t>(count_);
            events_.at(count_) = stored;
            ++count_;
        }
        sort_events();
        epoch_ = epoch;
        have_mark_ = false;
        marked_ = core::Duration::zero();
        return true;
    }

    void reset() noexcept {
        count_ = 0;
        have_mark_ = false;
        marked_ = core::Duration::zero();
    }

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] time_point epoch() const noexcept { return epoch_; }

    [[nodiscard]] bool sensor_unavailable(ChaosTarget target, time_point now) const noexcept {
        return covers(InjectionKind::SensorUnavailable, target, now);
    }

    [[nodiscard]] bool sensor_invalid(ChaosTarget target, time_point now) const noexcept {
        return covers(InjectionKind::SensorInvalid, target, now);
    }

    [[nodiscard]] bool sensor_frozen(ChaosTarget target, time_point now) const noexcept {
        return covers(InjectionKind::SensorFreeze, target, now);
    }

    // Declaration-order identity of the one active event of this kind and target.
    // Two freeze events on the same sensor have different sequences.
    [[nodiscard]] std::optional<std::uint16_t>
    active_sequence(InjectionKind kind, ChaosTarget target, time_point now) const noexcept {
        const ChaosEvent* event = find_active(kind, target, now);
        if (event == nullptr) {
            return std::nullopt;
        }
        return event->sequence;
    }

    [[nodiscard]] std::optional<std::int64_t> battery_millivolts(time_point now) const noexcept {
        const ChaosEvent* event =
            find_active(InjectionKind::BatteryVoltageOverride, ChaosTarget::Battery, now);
        if (event == nullptr) {
            return std::nullopt;
        }
        return event->parameter;
    }

    [[nodiscard]] core::Duration execution_delay(ChaosTarget target,
                                                 time_point now) const noexcept {
        const ChaosEvent* event = find_active(InjectionKind::TaskExecutionDelay, target, now);
        if (event == nullptr || event->parameter <= 0) {
            return core::Duration::zero();
        }
        return core::Duration{event->parameter};
    }

    [[nodiscard]] InjectionView view_at(time_point now) const noexcept {
        InjectionView view;
        view.imu_unavailable = sensor_unavailable(ChaosTarget::Imu, now);
        view.imu_invalid = sensor_invalid(ChaosTarget::Imu, now);
        view.imu_frozen = sensor_frozen(ChaosTarget::Imu, now);
        view.gps_unavailable = sensor_unavailable(ChaosTarget::Gps, now);
        view.gps_invalid = sensor_invalid(ChaosTarget::Gps, now);
        view.gps_frozen = sensor_frozen(ChaosTarget::Gps, now);
        view.battery_millivolts = battery_millivolts(now);
        view.navigation_delay = execution_delay(ChaosTarget::NavigationTask, now);
        view.communications_delay = execution_delay(ChaosTarget::CommunicationsTask, now);
        return view;
    }

    // Edges whose time is in (after, through]. Declaration order is sequence.
    // An end at the same time as a start is reported first.
    // Truncation does not change which events are active.
    [[nodiscard]] EdgeCopyResult copy_edges(core::Duration after, core::Duration through,
                                            ChaosEdge* out, std::size_t capacity) const noexcept {
        std::size_t pending = 0;
        for (std::size_t index = 0; index < count_; ++index) {
            const ChaosEvent& event = events_.at(index);
            core::Duration end{core::Duration::zero()};
            if (!event_end(event, end)) {
                continue;
            }
            if (crossed(after, through, event.start)) {
                ++pending;
            }
            if (crossed(after, through, end)) {
                ++pending;
            }
        }
        if (out == nullptr || capacity == 0) {
            return EdgeCopyResult{0, pending > 0};
        }
        std::size_t written = 0;
        for (std::size_t index = 0; index < count_ && written < capacity; ++index) {
            const ChaosEvent& event = events_.at(index);
            core::Duration end{core::Duration::zero()};
            if (!event_end(event, end)) {
                continue;
            }
            if (crossed(after, through, event.start) && written < capacity) {
                out[written] =
                    ChaosEdge{event.kind, event.target, event.start, true, event.sequence};
                ++written;
            }
            if (crossed(after, through, end) && written < capacity) {
                out[written] = ChaosEdge{event.kind, event.target, end, false, event.sequence};
                ++written;
            }
        }
        sort_edges(out, written);
        return EdgeCopyResult{written, pending > written};
    }

    // One caller. Logging is best-effort and does not change which events are active.
    void note(core::Logger<C>& logger, time_point now) {
        core::Duration elapsed{core::Duration::zero()};
        if (!elapsed_since_epoch(now, elapsed)) {
            return;
        }
        const core::Duration after = have_mark_ ? marked_ : core::Duration{-1};
        std::array<ChaosEdge, kChaosEdgeCapacity> edges{};
        const EdgeCopyResult copied = copy_edges(after, elapsed, edges.data(), edges.size());
        have_mark_ = true;
        marked_ = elapsed;
        for (std::size_t index = 0; index < copied.copied; ++index) {
            log_edge(logger, edges.at(index));
        }
    }

private:
    [[nodiscard]] static bool crossed(core::Duration after, core::Duration through,
                                      core::Duration at) noexcept {
        return at > after && at <= through;
    }

    [[nodiscard]] static bool schedule_accepted(std::span<const ChaosEvent> events) noexcept {
        for (auto left = events.begin(); left != events.end(); ++left) {
            core::Duration left_end{core::Duration::zero()};
            if (!event_end(*left, left_end)) {
                continue;
            }
            for (auto right = left + 1; right != events.end(); ++right) {
                if (left->target != right->target) {
                    continue;
                }
                core::Duration right_end{core::Duration::zero()};
                if (!event_end(*right, right_end)) {
                    continue;
                }
                if (left->start < right_end && right->start < left_end) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] static bool event_end(const ChaosEvent& event, core::Duration& end) noexcept {
        if (event.duration <= core::Duration::zero()) {
            return false;
        }
        if (event.start > core::Duration::max() - event.duration) {
            return false;
        }
        end = event.start + event.duration;
        return true;
    }

    [[nodiscard]] bool elapsed_since_epoch(time_point now, core::Duration& elapsed) const noexcept {
        if (now < epoch_) {
            return false;
        }
        return core::checked_time_between(now, epoch_, elapsed);
    }

    [[nodiscard]] bool covers(InjectionKind kind, ChaosTarget target,
                              time_point now) const noexcept {
        return find_active(kind, target, now) != nullptr;
    }

    [[nodiscard]] const ChaosEvent* find_active(InjectionKind kind, ChaosTarget target,
                                                time_point now) const noexcept {
        core::Duration elapsed{core::Duration::zero()};
        if (!elapsed_since_epoch(now, elapsed)) {
            return nullptr;
        }
        for (std::size_t index = 0; index < count_; ++index) {
            const ChaosEvent& event = events_.at(index);
            if (event.kind != kind || event.target != target) {
                continue;
            }
            core::Duration end{core::Duration::zero()};
            if (!event_end(event, end)) {
                continue;
            }
            if (elapsed >= event.start && elapsed < end) {
                return &event;
            }
        }
        return nullptr;
    }

    void sort_events() noexcept {
        for (std::size_t index = 1; index < count_; ++index) {
            const ChaosEvent key = events_.at(index);
            std::size_t cursor = index;
            while (cursor > 0 && event_before(key, events_.at(cursor - 1))) {
                events_.at(cursor) = events_.at(cursor - 1);
                --cursor;
            }
            events_.at(cursor) = key;
        }
    }

    [[nodiscard]] static bool event_before(const ChaosEvent& left,
                                           const ChaosEvent& right) noexcept {
        if (left.start != right.start) {
            return left.start < right.start;
        }
        return left.sequence < right.sequence;
    }

    static void sort_edges(ChaosEdge* edges, std::size_t count) noexcept {
        for (std::size_t index = 1; index < count; ++index) {
            const ChaosEdge key = edges[index];
            std::size_t cursor = index;
            while (cursor > 0 && edge_before(key, edges[cursor - 1])) {
                edges[cursor] = edges[cursor - 1];
                --cursor;
            }
            edges[cursor] = key;
        }
    }

    [[nodiscard]] static bool edge_before(const ChaosEdge& left, const ChaosEdge& right) noexcept {
        if (left.at != right.at) {
            return left.at < right.at;
        }
        if (left.started != right.started) {
            return !left.started;
        }
        return left.sequence < right.sequence;
    }

    static void log_edge(core::Logger<C>& logger, const ChaosEdge& edge) {
        char buffer[96];
        char* cursor = buffer;
        char* const end = buffer + sizeof(buffer);
        const auto append = [&cursor, end](std::string_view text) {
            for (const char character : text) {
                if (cursor + 1 >= end) {
                    return;
                }
                *cursor = character;
                ++cursor;
            }
        };
        append("t_ms=");
        const auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(edge.at).count();
        const auto written = std::to_chars(cursor, end - 1, milliseconds);
        if (written.ec != std::errc{}) {
            return;
        }
        cursor = written.ptr;
        append(" ");
        append(chaos_target_name(edge.target));
        append(" ");
        append(injection_kind_name(edge.kind));
        append(edge.started ? " started" : " restored");
        *cursor = '\0';
        logger.info("chaos", std::string_view{buffer, static_cast<std::size_t>(cursor - buffer)});
    }

    std::array<ChaosEvent, kChaosEventCapacity> events_{};
    std::size_t count_{0};
    time_point epoch_{};
    bool have_mark_{false};
    core::Duration marked_{core::Duration::zero()};
};

} // namespace ares::simulation
