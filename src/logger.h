#pragma once

#include <string>

bool InitializeLogger(const std::wstring& path, bool enabled);
void ShutdownLogger();
void Log(const char* format, ...);
