#include "position_control.h"

#include "config.h"
#include "key_listener.h"
#include "logger.h"

#include <atomic>

namespace {

constexpr DWORD kPositionControlPollMs = 10;

KeyListener g_height_listener{};
std::atomic<bool> g_listener_started{false};
std::atomic<float> g_pending_height_delta{0.0f};

}  // namespace

bool InitializePositionControl() {
    const auto& config = GetConfig().position_control;
    if (!config.enabled) {
        return true;
    }

    const bool started = g_height_listener.Start(config.key, kPositionControlPollMs, [amplitude = config.amplitude] {
        g_pending_height_delta.fetch_add(amplitude, std::memory_order_relaxed);
    });

    if (!started) {
        Log("position-control: failed to start height key listener");
        return false;
    }

    g_listener_started.store(true, std::memory_order_release);
    Log("position-control: height control enabled key=0x%X amplitude=%0.3f",
        static_cast<unsigned>(config.key),
        static_cast<double>(config.amplitude));
    return true;
}

void ShutdownPositionControl() {
    if (!g_listener_started.exchange(false, std::memory_order_acq_rel)) {
        g_pending_height_delta.store(0.0f, std::memory_order_release);
        return;
    }

    g_height_listener.Stop();
    g_pending_height_delta.store(0.0f, std::memory_order_release);
    Log("position-control: height control listener stopped");
}

bool IsPositionControlEnabled() {
    return GetConfig().position_control.enabled;
}

bool ConsumeHeightAdjustment(float* const delta) {
    if (delta == nullptr || !IsPositionControlEnabled()) {
        return false;
    }

    const float pending = g_pending_height_delta.exchange(0.0f, std::memory_order_acq_rel);
    if (pending == 0.0f) {
        return false;
    }

    *delta = pending;
    return true;
}
