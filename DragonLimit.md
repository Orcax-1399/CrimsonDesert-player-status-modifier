# Dragon Limit Research

## 目标

研究 `CrimsonDesert.exe` 中黑星龙的两类限制：

- 村庄等区域无法召唤
- 进入禁飞区后被强制下龙

本笔记已经完全切换到当前最终口径。
早期旧链、错误主线、以及已被动态证伪的推测，统一不再保留。

---

## 当前最终结论

这份文档当前保留两类结果：

- 已可直接使用的补丁
- 已验证有效、但风险较高、只适合作为定位结论的补丁

当前从“功能可用性”角度看，已经足够覆盖主要目标：

- 村庄可以召唤
- 村庄可以继续骑龙 / 飞行
- 进村庄不会被强制下龙
- 屋顶可以放行

当前唯一仍明确需要继续收口的高风险点是：

- 屋顶补丁还停留在虚表回调后的结果消费层，后续如果继续做，只应把它往 keyed bypass 收紧

### 1. 村庄召唤：`0x140611A26 : 74 09 -> EB 09`

当前可用的最小 AA：

```asm
[ENABLE]
// Minimal validation only.
//
// Force 0x140611A24 path to continue into 0x140611A31.
//
// Original:
//   84 C0    test al,al
//   74 09    je short 0x140611A31
//
// Patch:
//   74 09 -> EB 09
//
// This is intentionally global for this site.

assert(CrimsonDesert.exe+611A24,84 C0 74 09 41 89 1C 24 E9 25 FF FF FF)

CrimsonDesert.exe+611A26:
  db EB 09

[DISABLE]

CrimsonDesert.exe+611A26:
  db 74 09
```

当前已确认：

- 村庄召唤已经可以正常生效
- 这不是纯理论验证，已经是当前可直接使用的补丁
- 语义不是“去 UI”，而是强制让这一路继续进入 `0x140611A31` 后面的完整规则链

当前已知仍未正式解决：

- 某些区域限制，例如 `eErrNoCallVehicleMercenaryRegion`
- 某些特殊区域，例如阿比斯

因此这条补丁当前的准确定位是：

- 已解决“村庄召唤”这个子问题
- 还没有全覆盖所有 `NoCallVehicleMercenary*` 限制
- 屋顶限制虽然已经定位到有效门，但当前只得到高风险定位补丁，尚不作为正式方案

### 1.1 屋顶 / 高处召唤：`0x140611D2F` 已定位，但当前只保留为高风险定位补丁

当前已确认：

- 在已经放行 `0x140611A26` 后，屋顶样本仍然会经过：
  - `0x140611A31`
  - `0x140611A78`
- 并且：
  - `rule[+0xE2] = 0`
  - `[actor+0x26A] = 0`
  - `[actor+0x26B] = 0`
- `0x140611BF5` 不触发
- 单独改 `0x140611CA6` 也无效

当前真正命中的屋顶结果门已经收敛到：

```text
0x140611D29  call qword ptr [rax+430]
0x140611D2F  mov ebx, [rax]
0x140611D31  test ebx, ebx
```

最小验证补丁为：

```asm
[ENABLE]
// Roof / high-place summon locating patch only.
// High risk: this patch clears the callback result after a virtual call.

assert(CrimsonDesert.exe+611D2F,8B 18 85 DB 74 59)

CrimsonDesert.exe+611D2F:
  db 31 DB 90 90

[DISABLE]

CrimsonDesert.exe+611D2F:
  db 8B 18 85 DB
```

当前 live 结果已确认：

- 打开这版后，屋顶可以召唤

但这条补丁必须明确降级为“高风险定位补丁”，原因是：

- 它发生在虚表回调 `call qword ptr [rax+430]` 之后
- 本质是在结果消费层把 `mov ebx,[rax]` 直接改成 `xor ebx,ebx`
- 这只是证明“屋顶限制的同步错误码来自这里”
- 不能直接证明这里就是合适的正式 patch 点

因此当前对这条屋顶补丁的正确定性是：

- 有效
- 很危险
- 只保留为定位结论
- 后续应继续往 `qword [rax+430]` 的宿主 / 入参语义上收口，再做 keyed bypass

当前阶段的实用结论则是：

- 屋顶功能已经可以被放行
- 但这条补丁因为发生在虚表回调后的结果消费层，暂时不建议直接当成最终成品

### 2. 飞行 / 禁飞区强制下龙：旧版 `0x140668112` 仅保留为主链证明

旧版 AA：

```asm
[ENABLE]

assert(CrimsonDesert.exe+668112,45 84 E4 74 08 48 8B CE E8 81 60 61 01)
alloc(newmem,$200,CrimsonDesert.exe+668112)

label(code)
label(return)

newmem:
code:
  test r12b,r12b
  je short @f

  mov rax,[rsp+98]
  mov rdx,CrimsonDesert.exe+6B71A4
  cmp rax,rdx
  je short @f

  mov rcx,rsi
  call CrimsonDesert.exe+1C7E1A0

@@:
  jmp return

CrimsonDesert.exe+668112:
  jmp newmem
  nop 8

return:
  registersymbol(newmem)

[DISABLE]

CrimsonDesert.exe+668112:
  db 45 84 E4 74 08 48 8B CE E8 81 60 61 01

unregistersymbol(newmem)
dealloc(newmem)
```

当前已确认：

- 禁飞区不会再把人从龙上扔下来
- banner 也不会再弹出
- 入口覆盖长度、回跳、NOP 填充都已经现场核对过，汇编结构正确

这条补丁当前的准确语义是：

- `0x140668112 -> 0x14066811A -> 0x141C7E1A0` 是 forced dismount 主链的一部分
- 它不是单纯 UI sink
- 当前补丁只跳过 `RSP+0x98 == CrimsonDesert.exe+6B71A4` 这一路上的那次 `call 0x141C7E1A0`

但现在也已经确认这版不能作为最终正式方案，原因是：

- `0x141C7E1A0` 不是“只做强制下龙”的专用调用
- 它属于更通用的骑乘状态推进链
- 直接在 `0x140668112` 跳过这次调用后，虽然能阻止 forced dismount，但也会导致玩家自己无法手动解除骑乘

因此这条旧补丁现在只保留两层意义：

- 证明 `0x140668112 -> 0x141C7E1A0` 确实处在 forced dismount 主链上
- 作为上游行为门的研究结论保留

而不再作为最终推荐使用的飞行补丁。

### 3. 飞行 / 禁飞区强制下龙：当前正式方案改为 `0x141D634DB` 结果层补丁

当前新的最小可用 AA：

```asm
[ENABLE]

assert(CrimsonDesert.exe+1D634DB,41 88 47 04 41 89 3F)
alloc(newmem,$100,CrimsonDesert.exe+1D634DB)

label(code)
label(return)
label(skip_fix)

newmem:
code:
  cmp edi,247
  jne short skip_fix
  test al,al
  jne short skip_fix
  cmp dword ptr [rbx],6
  jne short skip_fix
  mov al,1

skip_fix:
  mov [r15+04],al
  mov [r15],edi
  jmp return

CrimsonDesert.exe+1D634DB:
  jmp newmem
  nop 2

return:
  registersymbol(newmem)

[DISABLE]

CrimsonDesert.exe+1D634DB:
  db 41 88 47 04 41 89 3F

unregistersymbol(newmem)
dealloc(newmem)
```

当前 live 结果已确认：

- 只改这一处 `AL` 字段，就已经足够阻止 forced dismount
- 玩家手动解除骑乘仍然正常，不再像 `668112` 那版一样被误伤
- 禁飞区仍会弹通知，但不会真的把人从龙上扔下来

这条补丁当前的准确语义是：

- `0x141D634DB / 0x141D634DF` 这一层已经是 forced dismount 的结果包写入层
- 当前 forced dismount 的稳定结果特征为：
  - `RDI = 0x247`
  - `AL = 0`
  - `[RBX] = 6`
- 把这组 forced-only 结果里的 `AL` 改成 `1`，即可只改行为结果，不影响手动下机

因此当前飞行侧的正式结论应改成：

- `668112` 是上游行为门，保留研究价值，但不适合正式 patch
- `1D634DB` 是当前更正确的最终 patch 点
- 当前剩下没解决的，只是对应的异步通知链

### 4. 阿比斯 / “骑乘位置被障碍物阻挡” 的当前结论

当前 live 与静态已经足够说明：

- 这次阿比斯样本不走：
  - `0x1407AD5A0`
  - `eErrNoCallVehicleMercenaryMovableNavigation`
  - `eErrNoCallVehicleMercenaryRideLimit`
- 也不走前面那些已经对村庄 / 屋顶生效的同步门

当前更可信的静态结论是：

- 阿比斯这类“骑乘位置被障碍物阻挡”样本，已经落进：
  - `sub_1403F5730`
- 该函数静态上明确对应：
  - `pa::ClientNavigationActorComponent::createCalculateSpawnMercenaryPositionTask`

也就是说：

- 阿比斯这条更像“异步计算召唤落点 / 生成位置”的任务链
- 不再是普通的同步 `NoCallVehicleMercenary*` gate

当前对阿比斯的处理结论是：

- 先不继续深挖
- 只保留这条静态认识
- 当前功能已经足够，不再为了它继续扩大补丁面

---

## 当前可用 AA 汇总

### 召唤：村庄可召唤

```text
地址：CrimsonDesert.exe+611A26
原始：74 09
修改：EB 09
```

### 飞行：旧版主链证明补丁

```text
入口：CrimsonDesert.exe+668112
关键过滤：只跳过 [rsp+98] == CrimsonDesert.exe+6B71A4 这一路
被跳过调用：CrimsonDesert.exe+1C7E1A0
```

后果：

- 可以阻止 forced dismount
- 但也会导致玩家自己无法手动解除骑乘

### 飞行：当前正式行为补丁

```text
入口：CrimsonDesert.exe+1D634DB
forced-only 特征：RDI=0x247, AL=0, [RBX]=6
修正方式：只把 AL 改成 1
```

当前效果：

- 不会被强制下龙
- 手动解除骑乘正常
- 通知仍会弹出

### 召唤：屋顶高风险定位补丁

```text
入口：CrimsonDesert.exe+611D2F
改法：mov ebx,[rax] -> xor ebx,ebx
效果：屋顶可召唤
```

警告：

- 这是虚表回调之后的结果消费层补丁
- 风险很高
- 当前只保留为定位结果，不建议直接作为正式成品方案

### 召唤：阿比斯当前只保留静态结论

```text
当前定位：不属于前面的同步 quick-fail gate
更像：createCalculateSpawnMercenaryPositionTask 异步落点计算链
当前处理：不继续扩大补丁面
```

---

## 当前静态 / 动态认识

### 1. 召唤侧

当前已经能稳定坐实：

- `0x140611A24` 前的 `test al, al`
- `0x140611A26` 的那道跳转

就是当前召唤成功 / 失败的最后关键门之一。

成功样本已经确认会经过：

- `0x140611A31`
- `0x140611A78`

也就是说，成功样本会继续进入完整规则解析链。

失败样本原本会在这里被截住：

```text
0x140611A1E  call qword ptr [rax+298]
0x140611A24  test al, al
0x140611A26  je short 0x140611A31
0x140611A28  mov [r12], ebx
0x140611A2C  jmp 0x140611956
```

把 `0x140611A26` 改成 `EB 09` 后，失败样本被强制推进到 `A31` 后面的完整链，因此村庄召唤被放行。

并且召唤侧现在还能再细分出第二类限制：

- 屋顶 / 高处限制不是卡在 `A31 / A78`
- 也不是卡在：
  - `rule[+E2]`
  - `[actor+26A] / [actor+26B]`
  - `611BF5`
  - 单独的 `611CA6`
- 当前已定位到它真正命中的同步错误门是：
  - `0x140611D29 -> 0x140611D2F -> 0x140611D31`

也就是说，当前召唤侧最少已经分成两类：

- 村庄：前面的 quick-fail gate
- 屋顶：后面的 callback result gate

### 2. 飞行侧

当前已经能稳定坐实：

- `0x140668112` 是 forced dismount 主链上的上游行为门
- `0x141D634DB` 是当前更有价值的结果层 patch 点

forced dismount 结果样本：

```text
RDI = 0x247
AL  = 0
[RBX] = 6
```

也就是结果包：

```text
[out+0x0] = 0x247
[out+0x4] = 0
[out+0x8] = 6
```

这说明当前飞行问题已经不是“单纯 UI 弹了什么”，而是主行为链真的走到了“自动解除骑乘”。

并且现在还可以进一步收紧为：

- 上游 `668112` 那版会误伤手动下机，因此只能保留为研究结论
- 真正适合作为正式 patch 的，是 `1D634DB` 这一层的 forced-only 结果改写

### 3. UI 和最终行为不是同一回事

当前已经明确：

- `sub_1404A0CA0`
- `sub_1403541D0`

都更像 UI 路由 / prompt sink，不是最终动作根点。

召唤侧已经实测到：

- 即使 `0x140611A26 : 74 09 -> EB 09` 放行后，龙已经正常召出
- 失败 UI 仍然可能异步单独弹出

这说明：

- 召唤是否成功
- 是否还会异步派发提示

是两条不同层级的链。

飞行侧同样成立：

- UI 相关链能解释“为什么弹 banner”
- 但不能直接当作“谁执行了强制下龙”

---

## 当前已判死 / 已降级的旧理解

下面这些结论已经不应再当主线：

- 不要再把旧的 `player + 0xA20` 那条 dragon 链当成既定事实
- 不要再回到旧的 `mount-pointer` 批量枚举入口当稳定单点
- `sub_1404868E0 / 0x1404871BE / 0x1404871D5 / 0x1404871E5` 这条老链已经判死，不再作为召唤或飞行主线
- `sub_1404A0CA0` / `sub_1403541D0` 是 UI 路由，不是最终行为 gate
- `sub_140668B90` 更像飞行资源 / 状态更新，不是 forced dismount 根判定
- `sub_141C7AB90` 更像 slot 刷新 / 同步，不是禁飞主入口
- `sub_14066B990` 虽然和飞行状态强相关，但更像状态协调器，不是当前最小 patch 首选点
- `0x14088F4xx / 0x14088F5xx / 0x1406B7C25 / 0x1406B7C2A / 0x1406B7E6C` 这类高频状态链不能直接当最终根因点
- `0x141D6C2AB / 0x1412FBFE7 / 0x141A3471A` 这些中段包装 / 汇总点不再作为下一轮主断点
- `0x140668112` 不能再作为最终正式补丁点，因为会连手动下机一起拦掉
- `0x140611D2F` 虽然对屋顶有效，但因为发生在虚表回调结果消费层，不能直接当正式成品补丁

一句话概括：

- 旧文档里凡是把高频热链、UI 链、或者旧 dragon 指针链写成“最终根因”的地方，现在都应该视为失效

---

## 关键样本结论

### 1. 村庄召唤

当前最重要的结论不是“整条系统完全搞清楚了”，而是：

- 最后关键门已经定位到 `0x140611A24 / 0x140611A26`
- 把失败样本推进到 `A31 / A78` 后，村庄召唤就能成功
- 屋顶样本则进一步定位到了 `0x140611D29 / 0x140611D2F` 这道更后的结果门
- 阿比斯样本则已经说明它更偏向异步落点计算链，而不是前面这些同步 gate

因此：

- 这条链已经足够支持实际 mod
- 没必要再回头盯旧的高噪声路径
- 但屋顶这条当前还只有“危险但有效”的定位补丁，没有正式 keyed 方案

### 2. 强制下龙

当前最重要的结论是：

- `0x140668112` 这条行为门证明了 forced dismount 主链确实经过这里
- 但正式 patch 应该落在 `0x141D634DB` 的结果层
- 当前最有效的 forced-only 特征是：
  - `RDI = 0x247`
  - `AL = 0`
  - `[RBX] = 6`

因此：

- 飞行侧当前也已经从“分析状态”进入“有正式可用 patch”的状态

---

## 仍未解决的问题

### 1. 召唤侧

当前仍未覆盖：

- `eErrNoCallVehicleMercenaryRegion`
- 以及类似阿比斯这种区域限制

也就是说：

- 村庄召唤已经解决
- 屋顶已经定位但还没形成低风险正式方案
- 阿比斯当前只保留静态结论，不继续扩大补丁面
- “所有不能召唤的原因”还没有一次性全部放开，但当前功能已经足够使用

### 2. 飞行侧

当前已解决的是：

- 禁飞区强制下龙

但还没有系统化整理的，是：

- 其它可能共享 forced dismount 机制的限制类型
- 是否存在不同禁飞原因对应不同 caller / 不同过滤条件
- 当前对应的异步通知链还没单独压掉

---

## 后续只应该继续做什么

如果后面继续推进，主线只剩下面这些：

### 1. 召唤侧

不要再回头研究村庄这条已经解决的 `611A26` 问题。

应该直接去拆：

- 区域限制
- 阿比斯这类特殊区域限制

也就是继续细分不同 `NoCallVehicleMercenary*` 原因，而不是再重复盯 `611A24 / A31 / A78`。

补充：

- 屋顶这条当前已经不用再重复做“是不是这里”的定位实验
- 后续如果回头处理屋顶，目标应该是把 `611D2F` 这类高风险结果层补丁往上提，做成 keyed bypass
- 阿比斯这条当前不再继续扩展，只保留 task 链结论

### 2. 飞行侧

不要再回头研究旧 UI 链和高频热链。

应该直接沿着：

- `0x141D634DB`
- `0x140668112`

这两个当前最稳的行为锚点，去区分是否还有别的 forced dismount 分支需要单独过滤。

但优先级要明确成：

- 正式 patch 继续围绕 `0x141D634DB` 这种结果层思路
- `0x140668112` 只保留为上游行为门分析点，不再直接拿来做成品 patch

---

## 最后结论

当前已经有两条可直接使用的结果：

1. 村庄召唤：`0x140611A26 : 74 09 -> EB 09`
2. 禁飞区飞行：`0x141D634DB` 结果层只把 forced-only 的 `AL=0` 改成 `1`

另外还有一条已经定位、但当前只保留为高风险结论的结果：

3. 屋顶召唤：`0x140611D2F : mov ebx,[rax] -> xor ebx,ebx`

当前研究方向已经不需要再围绕旧 dragon 链、旧 UI 链、或高频热路径扩散。

后续工作只应围绕：

- 屋顶那条虚表回调结果门如何继续做 keyed 收紧
- 是否有必要以后再回头处理阿比斯那条异步落点计算链

继续。
