# Crimson Desert 逆向分析笔记

## 数据结构

### ServerStatusActorComponent（玩家状态组件）

通过逆向分析及参考风灵月影修改器，确认核心数据结构如下：

- **`rsi`** = `ServerStatusActorComponent` 实例指针
  - `[rsi+0x00]` — 类型标识 / vtable（用于判断是否为玩家对象）
  - `[rsi+0x58]` — 属性数组基址指针

### 属性条目（Stats Entry）

AOB 特征码定位属性访问代码：
```
48 8D ?? ?? 48 C1 E0 04 48 03 46 58 ?? 8B ?? 24
```

对应的原始汇编：
```asm
lea rax, [计算的索引]
shl rax, 04              ; index * 16
add rax, [rsi+58]        ; base + offset = entry pointer
```

每个属性条目通过 `[rsi+0x58] + index * 16` 定位，条目内部布局：

| 偏移   | 类型    | 含义                            |
|--------|---------|--------------------------------|
| +0x00  | dword   | 属性类型 ID                     |
| +0x08  | qword   | 当前值（真实值，可直接读写）       |
| +0x18  | qword   | 最大值（存储值 = 实际最大值 × 1000）|

### 属性类型 ID

| ID  | 属性   |
|-----|--------|
| 0   | 血量   |
| 17  | 耐力   |
| 18  | 精力   |

### 玩家判定

风灵月影使用 `player_ServerStatusActorComponent` 符号来区分玩家与其他实体：
```asm
mov rbx, player_ServerStatusActorComponent
mov rbx, [rbx]
cmp [rsi], rbx    ; 比较 vtable/类型标识
jne not_player
```

---

## UI 显示层（缓存结构）

我们最初追踪的地址 `CrimsonDesert.exe+B50F05` 处的指令：
```asm
mov [rdi+0x198], r11    ; 写入显示用的当前值
mov [rdi+0x1A0], r10    ; 写入显示用的最大值
```

这是 UI 层的显示缓存，不是真实数据源。结构体布局：

| 偏移    | 含义                 |
|---------|----------------------|
| +0x188  | 除数因子（通常为 1）   |
| +0x190  | 第二级除数（通常为 10）|
| +0x198  | 编码后的当前值         |
| +0x1A0  | 编码后的最大值         |

函数入口：`CrimsonDesert.exe+B50E60`
- 参数：rcx=this, rdx=新当前值, r8=新最大值, r9=标志

调用链：
```
上层逻辑 → 0x1433F8674: call 0x143386EE0（中间函数）
         → 0x140B50E60（写入显示缓存）
```

---

## 修改方案

### 方案 A：消耗减半（向下取整）

在属性访问 Hook 点，检测值减少并恢复一半差值：

```asm
; 以血量为例，其他属性同理
check_health:
  mov rbx, [rax+08]           ; 读取当前值
  mov rcx, [last_health]      ; 上次记录值
  test rcx, rcx
  jz .save                    ; 首次访问，仅保存
  cmp rbx, rcx
  jge .save                   ; 没有减少，跳过
  ; 值减少了
  sub rcx, rbx                ; delta = old - new
  shr rcx, 1                  ; delta /= 2（向下取整）
  add rbx, rcx                ; new = new + delta/2
  mov [rax+08], rbx           ; 写回
.save:
  mov [last_health], rbx
```

### 方案 B：回复翻倍

同样在属性访问 Hook 点，检测值增加并额外加一倍增量：

```asm
check_health_heal:
  mov rbx, [rax+08]           ; 读取当前值
  mov rcx, [last_health]      ; 上次记录值
  test rcx, rcx
  jz .save
  cmp rbx, rcx
  jle .save                   ; 没有增加，跳过
  ; 值增加了（回血）
  sub rbx, rcx                ; delta = new - old（回复量）
  add rbx, [rax+08]           ; new + delta = old + 2*delta（双倍回复）
  ; 确保不超过最大值
  mov rcx, [rax+18]           ; 最大值（×1000）
  push rdx
  mov rdx, rcx
  ; 简化处理：直接用最大值原始存储比较
  cmp rbx, rdx
  cmovg rbx, rdx              ; 不超过最大值
  pop rdx
  mov [rax+08], rbx
.save:
  mov [last_health], rbx
```

### 方案 C：精确 Hook 写入指令（最佳方案）

使用 watch_write 监控 `[rax+0x08]` 地址，找到直接修改当前值的指令，
在那里 Hook 可以精确控制消耗量和回复量，无需事后补偿。

---

## ASI Mod 实现方案

ASI mod 本质上是一个被游戏加载的 DLL（.asi 扩展名），配合 ASI Loader 使用。

### 项目结构

```
CrimsonDesertMod/
├── dllmain.cpp          # 入口点
├── hooks.cpp            # Hook 逻辑
├── hooks.h
├── memory.cpp           # 内存扫描工具
├── memory.h
├── config.cpp           # 配置读取（INI）
├── config.h
├── CrimsonDesertMod.ini # 用户配置
└── CMakeLists.txt
```

### 依赖

- **MinHook** 或 **safetyhook**：用于内联 Hook
- **ASI Loader**：如 Ultimate ASI Loader（重命名 dinput8.dll / version.dll）

### 核心实现（dllmain.cpp）

```cpp
#include <Windows.h>
#include <cstdint>
#include "MinHook.h"  // 或 safetyhook

// ============ 配置 ============
struct ModConfig {
    bool  healthHalfConsumption = false;
    bool  healthDoubleHeal      = false;
    bool  staminaHalfConsumption = false;
    bool  staminaDoubleHeal      = false;
    bool  spiritHalfConsumption  = false;
    bool  spiritDoubleHeal       = false;
    // 可扩展：自定义倍率等
};

static ModConfig g_config;

// ============ 状态追踪 ============
struct StatTracker {
    int64_t lastHealth  = 0;
    int64_t lastStamina = 0;
    int64_t lastSpirit  = 0;
};

static StatTracker g_tracker;

// ============ AOB 扫描 ============
// 特征码: 48 8D ?? ?? 48 C1 E0 04 48 03 46 58
uintptr_t FindPattern(uintptr_t base, size_t size,
                      const char* pattern, const char* mask) {
    for (size_t i = 0; i < size; i++) {
        bool found = true;
        for (size_t j = 0; mask[j]; j++) {
            if (mask[j] == 'x' &&
                *(uint8_t*)(base + i + j) != (uint8_t)pattern[j]) {
                found = false;
                break;
            }
        }
        if (found) return base + i;
    }
    return 0;
}

// ============ Hook 回调 ============
// 在 shl rax,04; add rax,[rsi+58] 之后插入的逻辑
// rax = 属性条目指针, rsi = ServerStatusActorComponent

void ProcessStatEntry(int64_t* entryBase, uintptr_t rsi,
                      uintptr_t playerVtable) {
    // 玩家判定
    if (*(uintptr_t*)rsi != playerVtable) return;

    int32_t statType = *(int32_t*)entryBase;
    int64_t* currentVal = (int64_t*)((uint8_t*)entryBase + 0x08);
    int64_t* maxVal     = (int64_t*)((uint8_t*)entryBase + 0x18);

    int64_t* lastVal = nullptr;
    bool halfConsumption = false;
    bool doubleHeal = false;

    switch (statType) {
        case 0:  // 血量
            lastVal = &g_tracker.lastHealth;
            halfConsumption = g_config.healthHalfConsumption;
            doubleHeal = g_config.healthDoubleHeal;
            break;
        case 17: // 耐力
            lastVal = &g_tracker.lastStamina;
            halfConsumption = g_config.staminaHalfConsumption;
            doubleHeal = g_config.staminaDoubleHeal;
            break;
        case 18: // 精力
            lastVal = &g_tracker.lastSpirit;
            halfConsumption = g_config.spiritHalfConsumption;
            doubleHeal = g_config.spiritDoubleHeal;
            break;
        default:
            return;
    }

    int64_t cur = *currentVal;
    int64_t last = *lastVal;

    if (last == 0) {
        *lastVal = cur;
        return;
    }

    int64_t delta = cur - last;

    if (delta < 0 && halfConsumption) {
        // 消耗减半（向下取整）
        // delta 是负数，消耗量 = |delta|
        // 恢复一半: cur += |delta| / 2
        int64_t restore = (-delta) >> 1;
        cur += restore;
        *currentVal = cur;
    }
    else if (delta > 0 && doubleHeal) {
        // 回复翻倍
        int64_t extra = delta; // 额外加一倍
        cur += extra;
        // 上限检查
        int64_t maxV = *maxVal;
        if (cur > maxV) cur = maxV;
        *currentVal = cur;
    }

    *lastVal = cur;
}

// ============ ASM Hook（使用内联汇编或 trampoline）============
//
// 实际的 Hook 需要用 MinHook/safetyhook 创建 trampoline，
// 在 shl rax,04; add rax,[rsi+58] 执行后调用 ProcessStatEntry。
//
// 伪代码：
// 1. 执行原始指令: shl rax,04; add rax,[rsi+58]
// 2. 保存寄存器
// 3. 调用 ProcessStatEntry(rax, rsi, g_playerVtable)
// 4. 恢复寄存器
// 5. 跳回原始代码

// ============ 入口 ============
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // 1. 读取 INI 配置
        // 2. AOB 扫描定位 Hook 点
        // 3. 安装 Hook
        // 建议在新线程中执行，等待游戏模块加载完成
        CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            Sleep(5000); // 等待游戏初始化
            // LoadConfig();
            // InstallHooks();
            return 0;
        }, nullptr, 0, nullptr);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        // 卸载 Hook
        MH_Uninitialize();
    }
    return TRUE;
}
```

### 配置文件（CrimsonDesertMod.ini）

```ini
[Health]
HalfConsumption=1
DoubleHeal=0

[Stamina]
HalfConsumption=0
DoubleHeal=1

[Spirit]
HalfConsumption=0
DoubleHeal=1
```

### 安装方式

1. 下载 Ultimate ASI Loader，将对应的代理 DLL（如 `version.dll`）放到游戏目录
2. 将编译好的 `.asi` 文件放到游戏目录
3. 将 `.ini` 配置文件放到同目录
4. 启动游戏即可自动加载

### 注意事项

- 游戏更新后 AOB 特征码可能失效，需要重新扫描
- `player_ServerStatusActorComponent` 的识别需要在运行时动态获取
- 多线程安全：属性访问可能在多个线程触发，考虑使用原子操作
- 反作弊：如果游戏有反作弊保护，ASI 注入可能被检测
