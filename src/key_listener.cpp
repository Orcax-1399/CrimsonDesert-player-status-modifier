#include "key_listener.h"

#include <atomic>
#include <thread>
#include <utility>

class KeyListener::Impl {
public:
    bool Start(const int virtual_key, const DWORD poll_interval_ms, std::function<void()> on_tick) {
        if (running_.load(std::memory_order_acquire)) {
            return false;
        }

        virtual_key_ = virtual_key;
        poll_interval_ms_ = poll_interval_ms == 0 ? 10 : poll_interval_ms;
        on_tick_ = std::move(on_tick);
        if (!on_tick_) {
            return false;
        }

        running_.store(true, std::memory_order_release);
        thread_ = std::thread([this] {
            while (running_.load(std::memory_order_acquire)) {
                if ((GetAsyncKeyState(virtual_key_) & 0x8000) != 0) {
                    on_tick_();
                }

                Sleep(poll_interval_ms_);
            }
        });

        return true;
    }

    void Stop() {
        if (!running_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        if (thread_.joinable()) {
            thread_.join();
        }

        on_tick_ = {};
    }

    ~Impl() {
        Stop();
    }

private:
    std::atomic<bool> running_{false};
    int virtual_key_ = 0;
    DWORD poll_interval_ms_ = 10;
    std::function<void()> on_tick_{};
    std::thread thread_{};
};

KeyListener::KeyListener() : impl_(new Impl()) {}

KeyListener::~KeyListener() {
    delete impl_;
    impl_ = nullptr;
}

bool KeyListener::Start(const int virtual_key, const DWORD poll_interval_ms, std::function<void()> on_tick) {
    return impl_->Start(virtual_key, poll_interval_ms, std::move(on_tick));
}

void KeyListener::Stop() {
    impl_->Stop();
}
