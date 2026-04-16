#pragma once

#include <Windows.h>

#include <string>

bool InitializeLogger(const std::wstring& path, bool enabled, bool verbose, DWORD max_log_lines);
void UpdateLoggerConfig(bool enabled, bool verbose, DWORD max_log_lines);
void ShutdownLogger();
void Log(const char* format, ...);
