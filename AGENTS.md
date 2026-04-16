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
- 亲密度获得倍率

配置文件：

- `player-status-modifier.ini`

---

## 当前实现状态

当前实现不是纯“读取后补偿”模式。

### 玩家状态三属性

当前三属性已经不是统一一条链：

1. `stats` hook 只负责发现玩家真实属性 entry
2. `player spirit` 已从共享 `stat-write` 迁到独立 `spirit-delta` 语义点
3. `player health` 不再依赖 `stat-write` 做回复倍率，当前优先走 `damage/incoming` 语义点
4. `player stamina` 当前重新挂回共享 `stat-write` 提交层，但只允许对白名单里的玩家耐力 entry 生效
5. `mount stamina` 继续走 `AB00` 新入口，目标是直接锁耐力
6. `mount health` 继续走 `damage/incoming` 那条链，目标是直接锁血

也就是说：

- `stats` hook 只做白名单发现，不做数值补偿
- 共享 `stat-write` 当前只负责 `player stamina`
- `player spirit` 当前走独立 signed-delta 线路，不再在共享写回点上补偿
- `player stamina` 当前正式规则是：`rdi == tracked player stamina entry` 时才允许改写
- 如果 `entry` 不在已发现白名单里，绝对不能修改

### 玩家伤害

玩家伤害来自独立 hook，按倍率修改读取出的伤害值。

### Mount 状态

当前 mount 相关实现明确分成两部分：

- `mount stamina`：走 `AB00` 新入口，直接锁
- `mount health`：走 `damage/incoming`，直接锁

注意：

- `mount stamina` 可以继续留在 `AB00`
- `player stamina` 不能因为 `mount` 在该点可用，就继续套用同样策略

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

下面这条旧链路已经被证伪，必须判死，不要再把它当作当前有效结论使用：

```text
player actor + 0xA20 -> current dragon marker
marker + 0x8 -> owner / actor container
([marker + 0x8] + 0x68) -> dragon actor
marker + 0x18 -> stat root
root + 0x58 -> health entry
health entry + 0x480 -> stamina entry
```

需要明确：

- 不要再写“`player + 0xA20` 可以直接稳定拿到当前 dragon marker”。
- 不要再把这条链当作 dragon / mount 识别主线。
- 不要再基于这条链设计 hook、追踪逻辑或 damage 识别。
- 当前关于 dragon 的可靠定位，必须回到已经动态验证过的指针链本身，不能把这条旧链包装成既定事实。
- 旧的 `mount-pointer hook` 批量枚举入口同样仍然是高噪声来源，不能因为否定了 `player+0xA20` 就回退把它当成稳定单点。

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

当前工程状态补充：

- 这条共享 `stat-write` 逻辑当前只承载 `player stamina`
- `player health` / `player spirit` 不再走这里
- `player stamina` 在这里的唯一身份门控是：`rdi == tracked player stamina entry`

## Spirit 新入口

已确认并在 IDA 验证过唯一命中的 `spirit` 语义点 AOB：

```text
48 89 ?? 48 89 ?? E8 ?? ?? ?? ?? 84 C0 75 ?? 48 8B 5C 24 ?? 48 8B 74 24 ?? 48 83 C4 ?? 5F C3
```

当前 hook 落点：

- 唯一命中 `CrimsonDesert.exe+C6A72A5`
- 实际 hook offset 为 `+6`
- 最终拦截 call 位于 `CrimsonDesert.exe+C6A72AB`

对应调用链：

- `sub_1412E63E0 + 0x2D5` 调 `sub_1412EAA30`
- `sub_1412EAA30` 是 thunk，直接跳到 `sub_14C6A7280`
- `sub_14C6A7280 + 0x2B` 调 `sub_1412EB4C0`
- `sub_1412EB4C0` 是 thunk，最终跳到 `sub_14C6A9C20`

hook 点前后的关键寄存器语义：

```asm
mov r8, rbx
mov rdx, rdi
mov rcx, rax
mov rsi, rax
call sub_1412EB4C0
```

在这条线上：

- `rcx` = 已解析完成的 `spirit entry`
- `r8` = signed delta
- `rsi` = 同一个 `entry` 的备份

动态验证结论：

- `status == 18`
- `entry + 0x00` = `18`
- `entry + 0x08` = 当前精力
- `entry + 0x18` = 最大精力
- 负值是消耗
- 小正值（如 `+99/+100/+101`）是自然回复
- 较大正值（如 `+5000`）是手动回复

当前工程结论：

- `player spirit` 优先挂在这条 signed-delta 线路
- 这里比共享 `stat-write` 更清楚，因为直接拿到的是“本次变化量”而不是写回后的最终值
- 正式实现仍必须依赖 `stats` hook 发现出的玩家 `spirit entry` 白名单
- 只有 `entry` 已分类为 `PlayerSpirit` 且 `*(int32_t*)entry == 18` 时，才允许改 `delta`

## Stamina 新入口

已确认并在 IDA 验证过的 `AB00` 入口 AOB：

```text
0F B7 D7 49 8B CE E8 ?? ?? ?? ?? 48 8B F0 48 85 DB 74 ?? 33 C0 66 89 44 24 20 38 46 53
```

当前 hook 落点：

- 位于 `sub_1412EAB00`
- 落在 `call sub_1412E6090` 之后
- 也就是 `mov rsi, rax` 之后的观察点

当前已确认的语义边界：

- 这里拿到的 `rax` 确实是 `stamina entry`
- 这里拿到的 `rbx/a4` 不能再简单视为“原始 stamina 消耗值”
- `sub_1412EAB00` 之前会先调用 `sub_1412EACE0`
- `sub_1412EACE0` 会维护一张临时记录表，并返回聚合后的净变化量
- 因此 `AB00` 更像中间结算点，不是 player stamina 倍率的理想入口

当前工程结论：

- `mount stamina` 可继续挂在 `AB00`，因为 mount 只需要“直接锁”
- `player stamina` 不要继续在 `AB00` 上做倍率
- `player stamina` 当前正式实现回到 `stat-write`，`AB00` 只保留 mount 路线
- 如果后续继续研究上层语义点，也不要影响现有 `stat-write` 白名单门控

## Player Stamina 上层链

当前静态研究已确认：

- `sub_1412E76A0` 是 `sub_1412EAB00` 的主调用者
- `sub_1412E9930` 会在更上层构造事件结构，再调用 `sub_1412E76A0`
- 该结构里已经出现按 `1000` 缩放后的正负量

因此当前推荐研究顺序：

1. 先在 `sub_1412E9930` / `sub_1412E76A0` 确认 player stamina 原始业务输入
2. 再决定 player stamina 倍率挂点
3. 不要回到 `AB00` 上继续做“成对正负抵消”猜测

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

### 亲密度

当前亲密度逻辑不再挂在通用 record 提交写回层。

当前采用的有效语义点是：

```asm
mov     rax,[rdi+10]
mov     [rbp+18],rax
```

对应位置：

- `CrimsonDesert.exe+5B6E2C`

该点含义：

- `r12` = 旧亲密度
- `[rdi+10]` = 即将提交的新亲密度
- 应按正增量 `([rdi+10] - r12)` 做倍率
- 放大后写回的目标仍然是 `[rdi+10]`

明确规则：

- 只处理正增量
- 不要在 `CrimsonDesert.exe+E2DFC0B` 这类通用结构拷贝点直接做亲密度倍率主逻辑
- 不要再依赖旧的单一 return-address gate 作为亲密度主门控

---

## 配置约定

当前 INI 结构：

```ini
[General]
Enabled=1
LogEnabled=1
verbose=0
MaxLogLines=2000
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
- `spirit-delta hook`：在 `CrimsonDesert.exe+C6A72AB` 上处理玩家 `spirit` 的 signed delta
- `stat-write hook`：当前只处理 `player stamina`，且只在 `rdi == tracked player stamina entry` 时改写
- `stamina-ab00 hook`：当前主要负责 `mount stamina` 锁定与现场 mount 重建
- `damage hook`：处理玩家伤害倍率
- `damage/incoming`：当前也承载 `player health` 回复倍率与 `mount health` 锁定
- `item-gain hook`：处理物品获得倍率
- `affinity hook`：在亲密度预提交点放大正增量

说明：

- dragon / mount 不再使用独立 `mount-pointer hook`
- 不要在这里补写任何基于 `player + 0xA20` 的旧 dragon 链说明；该链已在上文判死

---

## 修改规则

以后继续改这个项目时，请遵守：

- 三属性逻辑优先挂在“语义明确且可对白名单 entry 稳定门控”的点，不要回退读后补偿
- 如果某个属性已经有更清晰的独立语义点，不要为了统一外形再塞回共享 `stat-write`
- 任何玩家属性修改都必须建立在已发现 entry 白名单之上
- 如果 entry 匹配不到，必须完全跳过，不允许猜测
- 玩家判定优先基于已捕获 `status marker`
- `player spirit` 优先继续沿用 `CrimsonDesert.exe+C6A72AB` 这条 signed-delta 线路，不要退回共享 `stat-write`
- `mount stamina` 允许继续留在 `AB00`，因为该功能目标是“直接锁”
- `player stamina` 不要继续在 `AB00` 上做倍率；该点目前只应视为 mount 可用点和 player 现场观察点
- `player stamina` 当前共享写回层的正式门控就是 `rdi == tracked player stamina entry`
- 不要再给 `player stamina` 额外叠加 `tracked_root` / `player_context` 之类的旧过滤
- 如果后续要迁回更上层语义点，先保留当前 `stat-write` 路线作为稳定回退
- 亲密度倍率优先挂在 `CrimsonDesert.exe+5B6E2C` 这个预提交新值点，基于 `r12` 与 `[rdi+10]` 的正增量改写
- 不要再把亲密度主逻辑挂回 `CrimsonDesert.exe+E2DFC0B` 的通用 record 结构写回点
- dragon / mount 相关旧链目前没有可直接写死到规范里的稳定主线；如果后续继续研究，必须以新的动态验证结果为准
- 不要把 `player + 0xA20`、`marker -> [marker+8] -> +0x68`、或旧的批量 actor attach 点重新写回为既定事实
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
