#pragma once

#include <Windows.h>

#include <functional>

class KeyListener {
public:
    KeyListener();
    ~KeyListener();

    KeyListener(const KeyListener&) = delete;
    KeyListener& operator=(const KeyListener&) = delete;

    bool Start(int virtual_key, DWORD poll_interval_ms, std::function<void()> on_tick);
    void Stop();

private:
    class Impl;
    Impl* impl_ = nullptr;
};
