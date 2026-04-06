# AGENTS.md

## 项目概述

这是一个用于 `Crimson Desert` 的 x64 ASI mod，输出文件名固定为：

- `player-status-modifier.asi`

构建方式：

- CMake
- MSVC / VS2022
- `safetyhook` 作为 mid-hook 框架，已 vendor 到 `deps/safetyhook`

当前主要功能：

- 血量 / 耐力 / 精力 消耗倍率
- 血量 / 耐力 / 精力 回复倍率
- 玩家伤害倍率
- 物品获得倍率

配置文件：

- `player-status-modifier.ini`

---

## 当前实现状态

当前实现不是纯“读取后补偿”模式。

### 玩家状态三属性

三属性逻辑采用双阶段：

1. `stats` hook 只负责发现玩家真实属性 entry
2. `stat-write` hook 在主写入点直接修改将要写入的值

也就是说：

- `stats` hook 只做白名单发现，不做数值补偿
- 真正的数值修改发生在主写入逻辑
- 如果 `entry` 不在已发现白名单里，绝对不能修改

### 玩家伤害

玩家伤害来自独立 hook，按倍率修改读取出的伤害值。

### 物品获得

当前只处理“获得”：

- `add [r8+rdi+10], rcx`

已知“丢失”指令，但目前不启用，仅保留备注：

- `sub [r15+rax+10], rcx`

---

## 关键逆向结论

## 玩家状态组件识别

已知玩家捕获脚本对应逻辑：

```asm
mov rax,[rdx+68]
mov rdx,[rax+20]
```

当前实现通过该路径获取玩家 `status marker`，后续通过：

```cpp
*(uintptr_t*)component == tracked_status_marker
```

来判定 `stats` 中的对象是否属于玩家。

注意：

- 这里保存的是“marker / 类型标识”，不是 `rsi` 本身
- 玩家判定不能再写成 `component == tracked_component`

## Dragon / Mount 识别

当前已确认一条比旧 `mount-pointer` 批处理入口更干净的链：

```text
player actor + 0xA20 -> current dragon marker
marker + 0x8 -> owner / actor container
([marker + 0x8] + 0x68) -> dragon actor
marker + 0x18 -> stat root
root + 0x58 -> health entry
health entry + 0x480 -> stamina entry
```

已在 live 进程验证样本：

- 玩家 actor：`0x597B21B0400`
- 玩家 marker：`0x597B2260A00`
- 当前 dragon marker：`[player+0xA20] = 0x597D0470500`
- dragon actor：`[[marker+0x8]+0x68] = 0x597B21B0E00`

说明：

- dragon / mount 追踪应当以“玩家先行”为准
- 优先从玩家 actor 直接取当前 dragon marker，不要再回到旧的批量 actor 枚举入口
- 旧的 `mount-pointer hook` 所在点会遍历整批同类 actor，天然高噪声，不能作为稳定 dragon 单点来源
- dragon 的 damage actor 不再做旧的回推猜测，直接使用 `[[marker+0x8]+0x68]`
- 该地址在 CE dissect 中可视作 `FrameEventActorComponent`
- damage 侧如需识别 dragon，应优先复用这条链解析出的 `tracked mount actor`

## 属性访问点

已确认 AOB：

```text
48 8D ?? ?? 48 C1 E0 04 48 03 46 58 ?? 8B ?? 24
```

语义：

```asm
shl rax, 04
add rax, [rsi+58]
```

执行后：

- `rax` 指向属性 entry
- `rsi` 指向状态组件对象

属性 entry 布局：

- `entry + 0x00` : `int32` 属性类型
- `entry + 0x08` : `int64` 当前值
- `entry + 0x18` : `int64` 最大值

属性 ID：

- `0` = Health
- `17` = Stamina
- `18` = Spirit

## 属性主写入点

已确认主写入逻辑附近存在：

```asm
sub rax,[rdi+18]
cmp [rdi+18],rbx
cmovg rax,rdx
mov [rdi+20],rax
inc qword ptr [rdi+48]
mov [rdi+08],rbx
```

关键写入点：

```asm
mov [rdi+08],rbx
```

说明：

- 这是一个通用属性主写入逻辑
- `rdi` 不是每次都能无条件视为目标属性 entry
- 必须先靠 `stats` hook 发现玩家三属性的真实 entry 白名单
- 只有 `rdi` 命中白名单 entry 时，才允许在这里修改 `rbx`

严格规则：

- 白名单没命中：不改
- `type` 不匹配：不改
- 数值范围不合理：不改

## 玩家伤害点

已确认来自 CE 的 damage 脚本逻辑，关键判断：

- 槽位索引为 `3`
- 来源对象属于玩家

当前实现按倍率修改读取到的伤害结果，不直接写敌方血量。

## 物品获得点

已确认“获得”指令：

```asm
add [r8+rdi+10], rcx
```

当前只在该点对 `rcx` 做倍率放大。

已知“丢失”指令，仅保留备注：

```asm
sub [r15+rax+10], rcx
```

后续如果要做物品消耗倍率，再单独接。

---

## 配置约定

当前 INI 结构：

```ini
[General]
Enabled=1
LogEnabled=1
InitDelayMs=3000
StaleComponentMs=60000
RelockIdleMs=10000

[Damage]
Multiplier=2.0

[Items]
GainMultiplier=2.0

[Health]
ConsumptionMultiplier=0.5
HealMultiplier=2.0

[Stamina]
ConsumptionMultiplier=0.5
HealMultiplier=1.0

[Spirit]
ConsumptionMultiplier=0.5
HealMultiplier=2.0
```

含义：

- `ConsumptionMultiplier=0.5` 表示消耗减半
- `HealMultiplier` 会放大该属性的回复写入；对 `Stamina` 来说也包括自然回耐
- `Damage.Multiplier=2.0` 表示玩家伤害翻倍
- `Items.GainMultiplier=2.0` 表示物品获得翻倍

---

## 代码结构

主要文件：

- `src/dllmain.cpp`：入口与初始化线程
- `src/config.*`：INI 读取
- `src/logger.*`：文件日志
- `src/scanner.*`：AOB 扫描
- `src/hooks.cpp`：所有 `safetyhook` 安装与回调
- `src/mod_logic.*`：运行时状态、entry 发现、倍率逻辑

当前 hook 角色分工：

- `player-pointer hook`：捕获玩家 `status marker`
- `stats hook`：发现玩家三属性 entry
- `stat-write hook`：对白名单 entry 做源头写入调整
- `damage hook`：处理玩家伤害倍率
- `item-gain hook`：处理物品获得倍率

说明：

- dragon / mount 不再使用独立 `mount-pointer hook`
- 当前实现改为在玩家已捕获后，从玩家 actor 直接刷新 dragon marker / root / entry

---

## 修改规则

以后继续改这个项目时，请遵守：

- 三属性逻辑优先挂在主写入点，不要退回读后补偿，除非主写入点失效
- 主写入点修改必须建立在已发现 entry 白名单之上
- 如果 entry 匹配不到，必须完全跳过，不允许猜测
- 玩家判定优先基于已捕获 `status marker`
- dragon / mount 判定优先基于玩家 actor 上的当前 dragon marker 链，不要再接回旧的批量 actor attach 点
- dragon actor 统一按 `marker -> [marker+8] -> +0x68` 解析，不再使用旧的 `marker+0x20` / `actor+0x20` 回推
- 新增 hook 时优先沿用 `safetyhook`
- 新增 AOB 时优先保留主 pattern + fallback pattern
- 高噪声热路径上的日志必须限量，避免刷爆日志

---

## 已知风险

- 游戏更新后 AOB 可能失效
- 部分有效代码位于 `.debug` 等非标准 section，扫描时不能只依赖 `.text`
- 主写入逻辑是热路径，门控写错会导致大范围副作用
- 物品获得 hook 目前只处理 gain，不处理 loss
- 当前仓库内置 `safetyhook` 源码，升级时要注意其 CMake 与 Zydis 依赖变化
