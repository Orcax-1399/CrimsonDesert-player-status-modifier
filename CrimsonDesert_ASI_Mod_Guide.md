# Crimson Desert ASI Mod — 实现指南

> 本文档是给下一个 Claude Code session 的上下文，用于从零实现一个 Crimson Desert 属性修改 ASI Mod。

---

## 目标

实现一个 ASI mod，支持以下功能（可通过 INI 配置独立开关）：
- 血量/耐力/精力 消耗减半（向下取整）
- 血量/耐力/精力 回复翻倍
- 可扩展：自定义倍率

---

## 已确认的逆向成果

### AOB 特征码

定位属性访问代码的特征码（来自风灵月影修改器，已验证）：
```
48 8D ?? ?? 48 C1 E0 04 48 03 46 58 ?? 8B ?? 24
```

对应原始汇编：
```asm
lea rax, [某索引计算]     ; 4 bytes（不替换）
shl rax, 04               ; 48 C1 E0 04
add rax, [rsi+58]         ; 48 03 46 58
```

Hook 点在 `shl rax, 04` 处（即 AOB 匹配地址 +4），替换 8 字节：
```
原始：48 C1 E0 04 48 03 46 58
替换：jmp newmem + nop*3
```

### 寄存器上下文（Hook 点触发时）

| 寄存器 | 含义 |
|--------|------|
| rax    | 属性索引（尚未乘16，Hook 内需先执行 shl+add） |
| rsi    | ServerStatusActorComponent 实例指针 |

执行 `shl rax,04; add rax,[rsi+58]` 后，rax 指向属性条目。

### 属性条目内存布局

```
entry + 0x00 : int32  — 属性类型 ID
entry + 0x08 : int64  — 当前值（真实值）
entry + 0x18 : int64  — 最大值（= 实际最大值 × 1000）
```

### 属性类型 ID

| ID | 属性 |
|----|------|
| 0  | 血量 (Health) |
| 17 | 耐力 (Stamina) |
| 18 | 精力 (Spirit) |

### 玩家判定

`[rsi+0x00]`（即 `[rsi]`）存储类型标识。
风灵月影通过 `player_ServerStatusActorComponent` 全局符号判断是否为玩家。
实现时需要在运行时动态识别玩家的类型标识值（方法见下文）。

---

## 技术选型建议

### Hook 框架

推荐 **safetyhook**（https://github.com/cursey/safetyhook）：
- 支持 mid-function hook（我们需要 hook 函数中间的指令，不是函数入口）
- 自动处理 trampoline 和寄存器保存
- 现代 C++，header-only 可选

备选：MinHook（更成熟但 mid-function hook 需要手写 ASM）

### ASI Loader

使用 Ultimate ASI Loader（https://github.com/ThirteenAG/Ultimate-ASI-Loader）：
- 将其 `version.dll` 或 `dinput8.dll` 放到游戏目录
- 自动加载同目录下所有 `.asi` 文件

### 构建工具

- CMake + MSVC（推荐 VS2022）
- 目标：x64 DLL，输出扩展名改为 `.asi`

---

## 实现步骤

### Step 1: 项目脚手架

```
CrimsonDesertMod/
├── CMakeLists.txt
├── src/
│   ├── dllmain.cpp          # DLL 入口
│   ├── scanner.h/.cpp       # AOB 内存扫描
│   ├── hooks.h/.cpp         # Hook 安装与回调逻辑
│   ├── config.h/.cpp        # INI 配置读写
│   └── mod_logic.h/.cpp     # 核心修改逻辑（消耗减半/回复翻倍）
├── deps/
│   └── safetyhook/          # git submodule 或直接包含
└── CrimsonDesertMod.ini     # 配置文件模板
```

### Step 2: AOB 扫描器

```cpp
// 扫描 CrimsonDesert.exe 模块的 .text 段
// 特征码: "48 8D ?? ?? 48 C1 E0 04 48 03 46 58 ?? 8B ?? 24"
// 返回匹配地址，Hook 点 = 匹配地址 + 4
uintptr_t ScanForStatsAccess();
```

注意事项：
- 获取模块基址和大小：`GetModuleHandle(NULL)` + PE header 解析
- 通配符 `??` 匹配任意字节
- 如果特征码匹配多个结果，取第一个（或全部验证）

### Step 3: Mid-Function Hook

这是最关键的部分。需要在 `shl rax,04; add rax,[rsi+58]` 这 8 字节上安装 Hook。

**使用 safetyhook 的 MidHook：**

```cpp
#include <safetyhook.hpp>

SafetyHookMid g_statsHook;

void StatsAccessCallback(safetyhook::Context& ctx) {
    // 此时原始指令尚未执行，需要手动计算
    // rax 是原始索引值
    uintptr_t rsi = ctx.rsi;
    uintptr_t index = ctx.rax;

    // 模拟原始指令
    uintptr_t arrayBase = *(uintptr_t*)(rsi + 0x58);
    uintptr_t entry = arrayBase + (index << 4);

    // 调用核心逻辑
    ProcessStatEntry(entry, rsi);

    // 注意：safetyhook MidHook 会自动执行被替换的原始指令
    // 如果不是这样，需要在回调中手动设置 rax
}

void InstallHooks() {
    uintptr_t hookAddr = ScanForStatsAccess() + 4;
    g_statsHook = safetyhook::create_mid(hookAddr, StatsAccessCallback);
}
```

**如果 safetyhook MidHook 不适用（因为需要在原始指令执行后读取结果）：**

手写 trampoline 方案：

```asm
; trampoline 入口
push rbx
push rcx

; 执行被替换的原始指令
shl rax, 04
add rax, [rsi+58]

; 此时 rax = entry 指针
; 保存并调用 C++ 函数
; ... 保存所有 volatile 寄存器 ...
; mov rcx, rax      ; arg1 = entry
; mov rdx, rsi      ; arg2 = component
; call ProcessStatEntry
; ... 恢复寄存器 ...

pop rcx
pop rbx
jmp return_address
```

### Step 4: 核心修改逻辑

```cpp
struct StatTracker {
    int64_t last = 0;
};

static StatTracker g_health, g_stamina, g_spirit;
static uintptr_t g_playerVtable = 0;  // 运行时识别

void ProcessStatEntry(uintptr_t entry, uintptr_t rsi) {
    // 玩家判定
    if (g_playerVtable != 0 && *(uintptr_t*)rsi != g_playerVtable)
        return;

    int32_t type = *(int32_t*)entry;
    int64_t* curVal = (int64_t*)(entry + 0x08);
    int64_t* maxVal = (int64_t*)(entry + 0x18);

    StatTracker* tracker = nullptr;
    float consumptionMul = 1.0f;  // 消耗倍率，0.5 = 减半
    float healMul = 1.0f;         // 回复倍率，2.0 = 翻倍

    switch (type) {
        case 0:  tracker = &g_health;  /* 从配置读取倍率 */ break;
        case 17: tracker = &g_stamina; break;
        case 18: tracker = &g_spirit;  break;
        default: return;
    }

    int64_t cur = *curVal;
    int64_t last = tracker->last;

    if (last == 0) { tracker->last = cur; return; }

    int64_t delta = cur - last;

    if (delta < 0 && consumptionMul < 1.0f) {
        // 消耗：delta 为负数
        int64_t absDelta = -delta;
        int64_t newDelta = (int64_t)(absDelta * consumptionMul);
        // 向下取整：消耗更少 = 恢复差值
        int64_t restore = absDelta - newDelta;
        *curVal = cur + restore;
        cur = *curVal;
    }
    else if (delta > 0 && healMul > 1.0f) {
        // 回复：delta 为正数
        int64_t extra = (int64_t)(delta * (healMul - 1.0f));
        int64_t newVal = cur + extra;
        // 上限检查
        int64_t maxV = *maxVal;
        if (newVal > maxV) newVal = maxV;
        *curVal = newVal;
        cur = newVal;
    }

    tracker->last = cur;
}
```

### Step 5: 玩家 vtable 动态识别

方案 A（简单）：
- 首次 Hook 触发时记录第一个遇到的 `[rsi]` 值
- 假设游戏加载后第一个触发属性访问的就是玩家
- 风险：可能误判 NPC

方案 B（可靠）：
- AOB 扫描风灵月影使用的 `player_ServerStatusActorComponent` 引用
- 或者通过多次采样，找到 type=0 且值最大的那个实体作为玩家

方案 C（最可靠）：
- 找到玩家 Controller / Pawn 的指针链
- 从中获取 ServerStatusActorComponent
- 这需要额外的逆向工作

建议先用方案 A 快速验证，后续迭代改进。

### Step 6: INI 配置

```ini
[General]
; 是否启用 mod
Enabled=1

[Health]
; 消耗倍率（0.5 = 减半，1.0 = 不变，0 = 无消耗）
ConsumptionMultiplier=0.5
; 回复倍率（2.0 = 翻倍，1.0 = 不变）
HealMultiplier=2.0

[Stamina]
ConsumptionMultiplier=0.5
HealMultiplier=2.0

[Spirit]
ConsumptionMultiplier=0.5
HealMultiplier=2.0
```

使用 `GetPrivateProfileString` / `GetPrivateProfileInt` 读取。

### Step 7: DLL 入口

```cpp
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
            // 等待游戏主模块加载完成
            while (!GetModuleHandle("CrimsonDesert.exe"))
                Sleep(100);
            Sleep(3000);  // 额外等待初始化

            LoadConfig();
            if (!InstallHooks()) {
                // AOB 扫描失败，可能游戏更新了
                MessageBoxA(NULL, "AOB pattern not found. Game may have updated.",
                           "CrimsonDesertMod", MB_ICONWARNING);
            }
            return 0;
        }, nullptr, 0, nullptr);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        // safetyhook 析构时自动卸载 hook
    }
    return TRUE;
}
```

---

## 构建与部署

### CMakeLists.txt 要点

```cmake
cmake_minimum_required(VERSION 3.20)
project(CrimsonDesertMod LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

# safetyhook
add_subdirectory(deps/safetyhook)

add_library(CrimsonDesertMod SHARED
    src/dllmain.cpp
    src/scanner.cpp
    src/hooks.cpp
    src/config.cpp
    src/mod_logic.cpp
)
target_link_libraries(CrimsonDesertMod PRIVATE safetyhook)

# 输出为 .asi
set_target_properties(CrimsonDesertMod PROPERTIES
    SUFFIX ".asi"
    OUTPUT_NAME "CrimsonDesertMod"
)
```

### 部署步骤

1. `cmake -B build -A x64 && cmake --build build --config Release`
2. 将 `CrimsonDesertMod.asi` + `CrimsonDesertMod.ini` 复制到游戏目录
3. 将 Ultimate ASI Loader 的 `version.dll` 复制到游戏目录
4. 启动游戏

---

## 已知风险与注意事项

1. **游戏更新**：每次更新后 AOB 可能失效，需要重新验证特征码
2. **反作弊**：Crimson Desert 如有反作弊（EAC/BattlEye），ASI 注入会被检测。目前分析基于单机/离线模式
3. **多线程**：属性访问可能多线程触发，核心逻辑中的 `last` 值读写应考虑原子操作
4. **最大值单位**：最大值存储为 实际值 × 1000，比较时注意单位
5. **事后补偿的局限**：方案 A/B 是在读取时补偿，有一帧延迟。如需更精确控制，需要额外用 watch_write 找到直接写入 `[entry+0x08]` 的指令并 Hook

---

## 参考资源

- 逆向分析详细笔记：`~/Desktop/CrimsonDesert_RE_Notes.md`
- safetyhook: https://github.com/cursey/safetyhook
- Ultimate ASI Loader: https://github.com/ThirteenAG/Ultimate-ASI-Loader
- MinHook: https://github.com/TsudaKageworke/minhook
