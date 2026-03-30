#include "logger.h"

#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace {

std::mutex g_log_mutex;
std::ofstream g_log_stream;
std::filesystem::path g_log_path;
bool g_log_enabled = false;

bool OpenLogStream(const std::ios::openmode mode) {
    if (g_log_path.empty()) {
        return false;
    }

    if (g_log_stream.is_open()) {
        g_log_stream.flush();
        g_log_stream.close();
    }

    g_log_stream.open(g_log_path, mode | std::ios::binary);
    return g_log_stream.is_open();
}

}  // namespace

bool InitializeLogger(const std::wstring& path, const bool enabled) {
    std::lock_guard lock(g_log_mutex);

    g_log_path = std::filesystem::path(path);
    g_log_enabled = enabled;
    if (!g_log_enabled) {
        if (g_log_stream.is_open()) {
            g_log_stream.flush();
            g_log_stream.close();
        }
        return true;
    }

    return OpenLogStream(std::ios::out | std::ios::trunc);
}

void SetLoggerEnabled(const bool enabled) {
    std::lock_guard lock(g_log_mutex);

    if (!enabled) {
        if (g_log_stream.is_open()) {
            g_log_stream.flush();
            g_log_stream.close();
        }
        g_log_enabled = false;
        return;
    }

    if (!g_log_stream.is_open() && !OpenLogStream(std::ios::out | std::ios::app)) {
        g_log_enabled = false;
        return;
    }

    g_log_enabled = true;
}

void ShutdownLogger() {
    std::lock_guard lock(g_log_mutex);

    if (g_log_stream.is_open()) {
        g_log_stream.flush();
        g_log_stream.close();
    }

    g_log_enabled = false;
}

void Log(const char* format, ...) {
    std::lock_guard lock(g_log_mutex);
    if (!g_log_enabled || !g_log_stream.is_open()) {
        return;
    }

    char payload[1024]{};
    va_list args;
    va_start(args, format);
    vsnprintf_s(payload, sizeof(payload), _TRUNCATE, format, args);
    va_end(args);

    SYSTEMTIME local_time{};
    GetLocalTime(&local_time);

    char prefix[64]{};
    _snprintf_s(prefix,
                sizeof(prefix),
                _TRUNCATE,
                "[%04u-%02u-%02u %02u:%02u:%02u.%03u] ",
                local_time.wYear,
                local_time.wMonth,
                local_time.wDay,
                local_time.wHour,
                local_time.wMinute,
                local_time.wSecond,
                local_time.wMilliseconds);

    g_log_stream << prefix << payload << "\r\n";
    g_log_stream.flush();
}
