#pragma once

#include <Windows.h>

#include <string>

struct StatConfig {
    double consumption_multiplier = 1.0;
    double heal_multiplier = 1.0;
};

struct DamageConfig {
    double multiplier = 2.0;
};

struct ItemConfig {
    double gain_multiplier = 2.0;
};

struct PositionControlConfig {
    bool enabled = false;
    int key = VK_F6;
    float amplitude = 0.1f;
};

struct GeneralConfig {
    bool enabled = true;
    bool log_enabled = true;
    DWORD init_delay_ms = 3000;
    DWORD stale_component_ms = 60000;
    DWORD relock_idle_ms = 10000;
};

struct ModConfig {
    GeneralConfig general;
    DamageConfig damage;
    ItemConfig items;
    PositionControlConfig position_control;
    StatConfig health{0.5, 2.0};
    StatConfig stamina{0.5, 1.0};
    StatConfig spirit{0.5, 2.0};
};

bool LoadConfig(const std::wstring& config_path);
const ModConfig& GetConfig();
const std::wstring& GetLoadedConfigPath();
