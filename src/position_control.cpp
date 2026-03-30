#include "position_control.h"

#include "config.h"
#include "key_listener.h"
#include "logger.h"

#include <atomic>
#include <cstdint>

namespace {

constexpr DWORD kPositionControlPollMs = 10;

KeyListener g_height_listener{};
KeyListener g_horizontal_listener{};
std::atomic<bool> g_height_listener_started{false};
std::atomic<bool> g_horizontal_listener_started{false};
std::atomic<std::uint32_t> g_pending_height_ticks{0};
std::atomic<bool> g_horizontal_active{false};

bool StartHeightListener(const PositionControlConfig& config) {
    const bool started = g_height_listener.Start(config.key, kPositionControlPollMs, [] {
        g_pending_height_ticks.fetch_add(1, std::memory_order_relaxed);
    });
    if (!started) {
        Log("position-control: failed to start height key listener");
        return false;
    }

    g_height_listener_started.store(true, std::memory_order_release);
    Log("position-control: height control enabled key=0x%X amplitude=%0.3f",
        static_cast<unsigned>(config.key),
        static_cast<double>(config.amplitude));
    return true;
}

void StopHeightListener() {
    if (g_height_listener_started.exchange(false, std::memory_order_acq_rel)) {
        g_height_listener.Stop();
        Log("position-control: height control listener stopped");
    }

    g_pending_height_ticks.store(0, std::memory_order_release);
}

bool StartHorizontalListener(const PositionControlConfig& config) {
    const bool started = g_horizontal_listener.Start(
        config.horizontal_key,
        kPositionControlPollMs,
        {},
        [](const bool is_down) { g_horizontal_active.store(is_down, std::memory_order_release); });
    if (!started) {
        Log("position-control: failed to start horizontal key listener");
        return false;
    }

    g_horizontal_listener_started.store(true, std::memory_order_release);
    Log("position-control: horizontal control enabled key=0x%X multiplier=%0.3f",
        static_cast<unsigned>(config.horizontal_key),
        static_cast<double>(config.horizontal_multiplier));
    return true;
}

void StopHorizontalListener() {
    if (g_horizontal_listener_started.exchange(false, std::memory_order_acq_rel)) {
        g_horizontal_listener.Stop();
        Log("position-control: horizontal control listener stopped");
    }

    g_horizontal_active.store(false, std::memory_order_release);
}

bool ReconfigureHeightListener(const PositionControlConfig& previous, const PositionControlConfig& current) {
    const bool changed = previous.enabled != current.enabled || (previous.enabled && current.enabled && previous.key != current.key);
    if (!changed) {
        return true;
    }

    const bool had_previous_listener = previous.enabled && g_height_listener_started.load(std::memory_order_acquire);
    if (had_previous_listener) {
        StopHeightListener();
    }

    if (!current.enabled) {
        return true;
    }

    if (StartHeightListener(current)) {
        return true;
    }

    if (had_previous_listener) {
        StartHeightListener(previous);
    }
    return false;
}

bool ReconfigureHorizontalListener(const PositionControlConfig& previous, const PositionControlConfig& current) {
    const bool changed = previous.horizontal_enabled != current.horizontal_enabled ||
                         (previous.horizontal_enabled && current.horizontal_enabled &&
                          previous.horizontal_key != current.horizontal_key);
    if (!changed) {
        return true;
    }

    const bool had_previous_listener = previous.horizontal_enabled && g_horizontal_listener_started.load(std::memory_order_acquire);
    if (had_previous_listener) {
        StopHorizontalListener();
    }

    if (!current.horizontal_enabled) {
        return true;
    }

    if (StartHorizontalListener(current)) {
        return true;
    }

    if (had_previous_listener) {
        StartHorizontalListener(previous);
    }
    return false;
}

}  // namespace

bool InitializePositionControl() {
    return ApplyPositionControlConfig(PositionControlConfig{}, GetConfig().position_control);
}

bool ApplyPositionControlConfig(const PositionControlConfig& previous, const PositionControlConfig& current) {
    if (!ReconfigureHeightListener(previous, current)) {
        return false;
    }

    if (!ReconfigureHorizontalListener(previous, current)) {
        if (previous.enabled != current.enabled || (previous.enabled && current.enabled && previous.key != current.key)) {
            ReconfigureHeightListener(current, previous);
        }
        return false;
    }

    return true;
}

void ShutdownPositionControl() {
    StopHeightListener();
    StopHorizontalListener();
}

bool IsPositionControlEnabled() {
    const auto config = GetConfig().position_control;
    return config.enabled || config.horizontal_enabled;
}

bool ConsumeHeightAdjustment(float* const delta) {
    if (delta == nullptr || !GetConfig().position_control.enabled) {
        return false;
    }

    const auto config = GetConfig().position_control;
    const std::uint32_t pending_ticks = g_pending_height_ticks.exchange(0, std::memory_order_acq_rel);
    if (pending_ticks == 0) {
        return false;
    }

    *delta = static_cast<float>(pending_ticks) * config.amplitude;
    return true;
}

bool ConsumeHorizontalMultiplier(float* const multiplier) {
    const auto config = GetConfig().position_control;
    if (multiplier == nullptr || !config.horizontal_enabled) {
        return false;
    }

    if (!g_horizontal_active.load(std::memory_order_acquire)) {
        return false;
    }

    *multiplier = config.horizontal_multiplier;
    return true;
}
