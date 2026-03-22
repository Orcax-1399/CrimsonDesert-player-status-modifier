#pragma once

#include <Windows.h>

#include <string>

struct StatConfig {
    double consumption_multiplier = 0.5;
    double heal_multiplier = 2.0;
};

struct DamageConfig {
    double multiplier = 2.0;
};

struct ItemConfig {
    double gain_multiplier = 2.0;
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
    StatConfig health;
    StatConfig stamina;
    StatConfig spirit;
};

bool LoadConfig(const std::wstring& config_path);
const ModConfig& GetConfig();
const std::wstring& GetLoadedConfigPath();
