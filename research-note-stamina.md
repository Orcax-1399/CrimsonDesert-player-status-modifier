# Stamina Research Note

## 2026-04-14 回退决定

最新 mod 尝试后需要明确：

- `player stamina` 试图拆成：
  - `C6A9CBF` 瞬时消耗 hook
  - `12E933B` 持续消耗 hook
  这条路线当前**不成立**
- 原因不是“静态点不存在”，而是：
  - 作为 CE 观察点可以成立
  - 但作为 mod 正式改点，目前没有得到稳定生效结果
- 因此当前工程决策改为：
  - `player stamina`
    - 回到 `stat-write` 提交层过滤
  - `mount stamina`
    - 继续留在 `AB00`

这里必须严格区分三种点：

- `stats hook`
  - `1412E610B`
  - 只负责发现 player stat entry
- `stat-write hook`
  - `C6B5520`
  - 是最终提交层
- `stamina-ab00 hook`
  - `1412EAB00`
  - 目前只给 mount 使用
  - 不是 `stat-write`

## 2026-04-14 双流收敛与本轮实现计划

下面这段“双流实现计划”保留作为研究记录，但当前不要再把它当成现行工程方案。

### 1. `player stamina` 需要按两类消耗分别处理

当前应明确分成两条线：

- 瞬时消耗
  - 典型样本：
    - 翻滚 `delta=-20000`
    - 空中冲刺 `delta=-30000`
  - 当前最稳定观察点：
    - `CrimsonDesert.exe+C6A9CBF`
    - `sub_14C6A9C20 + 0x9F`
  - 该点现场语义已经足够直接：
    - `rsi = stamina entry`
    - `rbx = signed delta`
  - 当前工程结论：
    - 这条线可作为 **player stamina 瞬时消耗** 的正式改点候选

- 持续消耗
  - 当前最稳定观察点：
    - `CrimsonDesert.exe+12E933B`
    - `sub_1412E9000 -> call sub_1412E76A0`
  - 当前已动态确认：
    - `status == 17`
    - `direct == 1`
    - `owner/root == tracked player root`
    - `packet < 0`
  - 飞行样本里，锁定玩家 owner 后目前只看到：
    - `-10000`
    - `-20000`
  - 当前工程结论：
    - 这条线可作为 **player stamina 持续消耗** 的正式改点候选

### 2. 当前不再采用的旧理解

- `23461BB = player stamina`
- `2345FEB = mount stamina`

这条旧判断已经证伪。

当前应改记为：

- `0x1423461BB`
- `0x142345FEB`

是同一条 player stamina 上层分支里的交替双腿，不是 player / mount 身份拆分。

因此：

- 不要再把这两个 return address 当作 player / mount 分类依据
- 这两支当前更适合作为观察点，不适合作为 mod 正式门控

### 3. `AB00` 对 player stamina 仍只视为观察层，不作为正式倍率主点

2026-04-14 的回看仍支持下面这个结论：

- `AB00` 侧会出现成对正负 / 聚合腿 / 生命周期噪声
- 只改负腿会导致“不 work”
- 抹平正腿会破坏回耐，甚至出现站地后快速掉空

因此本轮实现仍保持：

- `mount stamina`
  - 继续留在 `AB00`
- `player stamina`
  - 不在 `AB00` 做正式倍率

### 4. 本轮 mod 实施基线

本轮正式实现应按下面顺序推进：

1. 先把上述双流模型写进文档并建立 TODO
2. 在 mod 里新增两条 `player stamina` hook：
   - 瞬时消耗：
     - `C6A9CBF`
   - 持续消耗：
     - `12E933B`
3. 两条线都只处理 `player stamina consume`
4. `stamina heal` 现阶段仍不在这两条线上主动放大
5. `mount stamina` 继续保留在 `AB00` 锁定逻辑

### 5. 本轮实现门控规则

#### 瞬时消耗线

- `entry == tracked player stamina entry`
- `*(int32_t*)entry == 17`
- `delta < 0`

然后按 `Stamina.ConsumptionMultiplier` 缩放负值。

#### 持续消耗线

- `owner/root == tracked player stat root`
- `record.status == 17`
- `record.direct == 1`
- `packet < 0`

然后只改传给 `sub_1412E76A0` 的负 packet。

这里必须强调：

- 只改负 packet
- 不碰正 packet
- 不碰其他 owner

### 6. 本轮实现后的验证要求

实现完后至少要验证：

- 翻滚消耗倍率是否生效
- 空中冲刺消耗倍率是否生效
- 滑翔持续消耗倍率是否生效
- 加速飞行持续消耗倍率是否生效
- 落地自然回耐是否仍正常
- 走路 / 满耐待机是否不会异常掉耐

## 最新补充

- 2026-04-13 的最新动态验证与下个 session 执行基线，见：
  - `research-note-stamina-handoff-2026-04-13.md`
- 这一轮已经额外确认：
  - `1412E9000 direct` 是当前 player stamina 主线
  - `a1 == AB00.owner`
  - 对 player 样本来说，这个对象是 `root/context_root`，不是 `marker`
- 这一轮继续补完后的关键结论：
  - `player stamina` 的真实消耗态已经不需要再按“动作名”逐个枚举
  - 真实消耗共性是：
    - `player root/context_root`
    - `branch1_mode1`
    - 同一个 stamina entry
    - `AB00 delta < 0`
  - 但 `1412E9000/15161D7D0` 这层的 `packet` 不是最终写入值
  - 同一个 `statusWord=513 / packet=-8000`
    - 最终可落成 `delta=-8000 / -40000 / -100000`
  - 因此真正的数值展开发生在：
    - `sub_1412E76A0`
    - 不是 `1412E9000`，也不是最后的 `AB00`

## 2026-04-13 追加静态收敛

### 1. `1412E76A0` 才是 player stamina 的关键变换层

当前链路应写成：

- `sub_15161D7D0`
- `sub_1412E9000`
- `sub_1412E76A0`
- `sub_1412EAB00`

其中语义分工已经能收紧到：

- `1412E9000`
  - 读取 `Skill` metadata / descriptor
  - 提供：
    - `statusWord`
    - `packet`
    - `direct`
    - `pos_assoc`
    - `neg_assoc`
- `1412E76A0`
  - 结合运行时状态，把 descriptor record 变成“本次真实变化量”
- `1412EAB00`
  - 只是接收已经算好的 `delta`
  - 再提交到具体 stat 对象

### 2. `1412E76A0` 里真正决定传给 `AB00` 的不是原始 `packet`

关键静态调用顺序：

```text
sub_1412E5B50(...)
sub_1412EACE0(a1, &delta_out, a2, &effect_record)
sub_1412EAB00(a1, status, time_key, aggregate_value, delta_out)
```

更准确地说：

- `a5` 只是原始 `packet`
- `1412E76A0` 会先构造一个 48-byte `effect_record`
- 再调用 `sub_1412EACE0`
- `sub_1412EACE0` 返回的差值 `delta_out`
  - 才是后面真正传给 `sub_1412EAB00` 的那个数

这就是为什么动态里会出现：

- 同一个 `packet=-8000`
- 最终却变成：
  - `delta=-8000`
  - `delta=-40000`
  - `delta=-100000`

### 3. `sub_1412EACE0` 的本质是“active effects 聚合差值”

当前可确定：

- `a1 + 200`
  - 是一组 48-byte active effect records
- `sub_1412EACE0`
  - 先算插入/删除前总贡献
  - 再更新当前 record
  - 再算插入/删除后总贡献
  - 返回：
    - `after_sum - before_sum`

也就是说：

- `delta_out` 本质上不是“当前 packet 本身”
- 而是“当前 effect 导致的聚合总量变化差值”

### 4. 单条 record 的贡献值来自 `sub_1412EBB50 -> sub_14C6BC160`

`sub_1412EBB50` 只是 thunk，真实实现是：

- `sub_14C6BC160`

这层不会直接返回 `record.packet`，而是会组合使用：

- `record + 0x08`
- `record + 0x18`
- `record + 0x28`

并进一步调用：

- `sub_14070FA50`

当前足够明确的工程结论：

- `packet` 是模板输入
- 最终有效贡献值要再经过一层区间/比例映射
- 所以不能在 `1412E9000` 只按原始 `packet` 做 player stamina 最终倍率

### 5. 当前最合理的新改点已经上移

对 `player stamina` 来说，当前最合理的候选改点不是：

- `1412E9000`
- `AB00`

而是：

- `1412E76A0`
  - `sub_1412EACE0` 返回之后
  - `sub_1412EAB00` 调用之前

理由：

- 这里拿到的是已经聚合完成的真实 `delta_out`
- 还没进入最后的通用 stat 提交层
- 可以自然地只处理：
  - `delta_out < 0`
- 同时避免再去枚举：
  - `503 / 513 / 610 / 626 / 715 ...`

### 6. 对后续实现的直接影响

当前不建议：

- 在 `1412E9000` 只按 `packet` 改 player stamina
- 在 `AB00` 做 player stamina 的通用正负倍率

当前建议：

- `player stamina`
  - 继续研究并落点在 `1412E76A0` 的聚合差值产出侧
- `mount stamina`
  - 仍留在 `AB00`

### 7. `1412E76A0` 内部已确认两段唯一窗口

本轮 IDA 静态再次确认，`sub_1412E76A0` 里确实有两段固定模式：

- 主窗口 A：
  - `0x1412E7930`
  - `lea rdx, [rsp+50]`
  - `call sub_1412EACE0`
  - `mov r9, [rsp+50]`
  - `call sub_1412EAB00`
  - call 后续返回点：
    - `0x1412E7961`
- 次窗口 B：
  - `0x1412E7A90`
  - `lea rdx, [rbp-60]`
  - `call sub_1412EACE0`
  - `mov r9, [rbp-60]`
  - `call sub_1412EAB00`
  - call 后续返回点：
    - `0x1412E7AB7`

两段都已用 `find_bytes` 验证为唯一命中。

当前可直接记住的工程结论：

- `mov r9, [stack/local]`
  - 这里装入的就是 `delta_out`
- 紧接着：
  - `mov r8, r13`
  - `movzx edx, bx`
  - `mov rcx, r15`
  - `call sub_1412EAB00`
- 也就是说在 call 前最后一刻，参数语义已经足够清楚：
  - `rcx = a1/root-context`
  - `edx = statusWord`
  - `r8  = a4/time_key`
  - `r9  = delta_out`

结合已有动态：

- 当前 player stamina 样本命中的 `AB00 caller`
  - 一直是 `0x1412E7961`
- 因而 player 主线优先窗口是：
  - `0x1412E7930 -> 0x1412E795C`
- `0x1412E7A90` 那条次窗口先保留作旁支/交叉验证，不要把两段混成一个未区分的 hook 点

### 8. 最新动态收敛：`direct=1` 与 `direct=0` 已分成两类

通过 `76A0 window A` 的 `delta_out probe` 与 `effect-record probe`，当前已经能把样本分成两类：

- `direct=1`
  - `desc_packet == record_packet == delta_out`
  - 典型：
    - `metaId=10016`
    - `metaId=10008`
    - `metaId=10005`
  - 这类就是“原始消耗值直接下去”

- `direct=0`
  - `desc_packet` 仍然是负向模板值
  - 但进入 `76A0` / `ACE0` 前后，会出现：
    - `record_packet` 变成正的有效幅度
    - `delta_out` 再变回负的提交值
  - 已对上的典型样本：
    - `metaId=10023`
      - `-10000 -> +20000 -> -20000`
    - `metaId=10072`
      - `-20000 -> +40000 -> -40000`
    - `metaId=10156`
      - `-40000 -> +100000 -> -100000`

当前工程结论：

- `direct=1`
  - 如果未来要做倍率，这类最容易处理
- `direct=0`
  - 不能再把 `delta_out` 当作第一手语义值
  - 也不能只看 `desc_packet`
  - 真正的“放大/改写”发生在 `ACE0` 之前或 `76A0` 入参构造阶段

### 9. 最新动态收敛：`lower/upper` 目前不是主放大来源

最新 `effect-record probe` 里，对这些 `direct=0` 放大样本，当前看到的是：

- `lower = 0`
- `upper = 0`
- `sentinel = 65535`

也就是说：

- 这批样本里，`sub_14C6BC160` 那套基于 `lower/upper` 的区间映射
  - 目前不是主要放大来源

结合日志表现：

- 真正更像关键输入的是：
  - `76A0` 入口的 `a5`
  - 或更上游 `1412E9000` 构出来的 work item / descriptor 中间值

因此当前推荐继续上移研究点：

- 不要把正式实现停在：
  - `delta_out`
  - 或 `ACE0` 聚合结果
- 下一步更合理的是继续追：
  - `1412E9000 -> call sub_1412E76A0`
  - 以及 `76A0` 入口的 `a5 -> effect_record.packet`

### 10. 最新 CE 脚本补充

本轮新增并已验证可用的只读脚本：

- `ce/sample_stamina_effect_record_76A0_window_a.lua`
  - 作用：
    - 关联上游 `meta packet`
    - 读取 `76A0` 入口 `a5`
    - 读取 `ACE0` 前的 48-byte `effect record`
    - 读取窗口 A 的最终 `delta_out`
  - 当前推荐用途：
    - 专门验证 `direct=0` 样本到底是在：
      - `1412E9000`
      - `76A0 a5`
      - `effect_record.packet`
      这三层里的哪一层先发生放大

## 目标

- 目标要收紧：
  - 只需要找到一个落点
  - 这个落点既能识别出“当前是 player 或 mount 相关流量”
  - 又能稳定修改 stamina consumption
- 不要求把整条通用 stat 链完全还原干净。
- 不要求把 player / mount 从最上游彻底拆成两套独立链。
- player / mount 可以挂在同一个点位处理。
- 明确要求：不要再依赖通用 `stat-write` 路径。

当前判断标准：

- 只要某个点同时满足下面两条，就已经足够：
  - 能在该点或该点上游稳定识别“这是 player / mount 目标集合”，不必再细分谁是 player、谁是 mount
  - 能在该点直接影响 stamina 消耗，而不是通用写回后的大范围噪声补偿

因此当前研究重点不再是“哪里都要搞清楚”，而是优先判断：

- 哪个点最早带有足够可靠的 `player / mount` 目标识别语义
- 哪个点离 stamina consumption 足够近，改动副作用最小

## 已确认应避开的点

- `stats hook`：`0x1412E610B`
  - 对应 `sub_1412E6090+0x7B`
- `stat-write hook`：`0x14C6B5520`
  - 对应 `sub_14C6B5470+0xB0`
  - 即通用写回 `mov [rdi+08], rbx`

结论：

- 这两处都太通用，尤其 `stat-write` 热点噪声太大，不再作为主线。

## 2026-04-14：`stat-write` 侧重新观察

虽然 `stat-write` 不再作为 player stamina 的正式主线，但 2026-04-14 的回看说明：

- 它仍然很适合用来观察“最终提交包”长什么样
- 但不适合把某一次抓到的 writer 栈，直接当作所有 consume / recover 业务的统一模板

### 断点

- `CrimsonDesert.exe+C6B5520`
  - `48 89 5F 08`
  - `mov [rdi+08], rbx`

也就是通用 current write：

- `entry = rdi`
- `final value = rbx`

### 一次 writer 栈观察

这次在 `CrimsonDesert.exe+C6B5520` 附近看到过下面一串值：

```text
CrimsonDesert.exe+C6A9CBF - 000493E0,FFFFB1E0,000493E0,542EE080,...
13FFD0037            - FFFFB1E0,00000011,FFFF0000,000493E0,...
FFFFFFFFFFFFB1E0     - 00000011,FFFF0000,000493E0,00000000,...
00000011             - FFFF0000,000493E0,00000000,412E66BA,...
FFFFFFFFFFFF0000     - 000493E0,00000000,412E66BA,00000011,...
000493E0             - 00000000,412E66BA,00000011,FFFF0011,...
00000000             - 412E66BA,00000011,FFFF0011,FFFFB1E0,...
```

其中至少能直观看出这些关键值：

- `0x542EE080`
  - stamina entry
- `0x00000011`
  - stat type `17`
- `0xFFFFB1E0`
  - signed `-20000`
- `0x000493E0`
  - `300000`
- `0xFFFF0011`
  - 很像打包过的 `sentinel/status` 组合值

动态现象：

- 把这段“writer 侧已整理好的提交包”搬过去，耐力会真实扣减

这说明到 `stat-write` 这一层时，系统手里已经不只是 `AB00` 那种中间差分，而是更接近：

- target entry
- stat type
- signed delta
- 已经整理好的最终写回参数

### 关键更正

上面这次抓到的 writer 栈，后来重新确认后应视为：

- 一次 **耐力回复侧** 的 writer 观察

而不是：

- 通用的“耐力消耗一定走这条栈”

必须明确：

- 不能把这串 writer 栈直接当成 consume 的统一模板
- 当前已知：**消耗不一定走这串相同的栈内容**
- 因此“把这串包放过去会扣耐”只说明 writer 侧已经是最终提交层，不说明 consume 和 recover 在 writer 前完全同栈

### 当前解释

这轮观察更支持下面这个模型：

- `AB00` / `76A0` 看到的是 active-effect 生命周期相关的中间差分
- `stat-write` 看到的是已经被整理好的最终提交包

所以：

- 在 `AB00` 改，容易被后续配对腿 / 聚合 / 状态维护抵消
- 在 `stat-write` 改，往往稳定，因为已经接近最终 current write

### 当前未完成项

真正还缺的是：

- 重新抓一轮 **consume 侧** 在 `CrimsonDesert.exe+C6B5520` 附近的 writer 栈

不要再沿用上面这组 recovery 侧样本，去解释 consume。

## 当前主线

目前更合适的上游链是：

`sub_142345E60 -> sub_14234C500 -> sub_15161D7D0 -> sub_1412E9000 -> sub_1412E76A0 -> sub_1412EAB00 -> sub_1412E6090`

其中：

- `sub_14234C500` 只是 thunk
  - 内容：直接跳到 `sub_15161D7D0`
- `sub_15161D7D0` 是把某个状态描述送进 `sub_1412E9000` 的桥接层
- `sub_1412E9000` 是通用 stat descriptor 分发层
- `sub_1412E6090` 返回真实 `entry*`

## 已对上的栈结论

### mount stamina

已对上的关键返回地址：

- `0x142345FEB`
- `0x15161D837`
- `0x1412E9340`
- `0x1412E7961`
- `0x1412EAB8D`

### player stamina

已对上的关键返回地址：

- `0x1423461BB`
- `0x15161D837`
- `0x1412E9340`
- `0x1412E7961`
- `0x1412EAB8D`

### 结论

- 玩家和 mount 的 stamina 后半段栈大面积共用。
- 它们不是在 `stat-write` 才混在一起，而是在更上游就共享通用 stat 分发链。
- 真正的分叉已经上移到 `sub_142345E60` 里不同 call site。

## `sub_142345E60` 的意义

`sub_142345E60` 目前是最值得继续往上游研究的包装层。

已知有两个和当前 stamina 主线直接相关的 call site：

- `0x142345FE6 -> call sub_14234C500`
  - 返回地址：`0x142345FEB`
- `0x1423461B6 -> call sub_14234C500`
  - 返回地址：`0x1423461BB`

结论：

- 这两个点都已经远离 `stat-write`。
- 它们都进入同一个 `sub_15161D7D0`，但代表 `sub_142345E60` 内部两条不同分支。
- 后续如果需要动态验证，优先考虑这两个 call site，而不是去打通用 stat hook。

## actor / property 锚点

目前最早、最像 actor 语义点的位置在 `sub_142345E60`。

这里循环处理一个 24-byte 槽，核心对象是第三个 qword，也就是类似 `v18[2]` 的对象指针。

当前已确认该对象上至少有这些有用字段：

- `+0x103 & 2`
  - 有效性门控
- `+0x98`
  - 会进一步映射成 `word` 状态码
- `+0xE0`
  - 对象类别字节，静态里明确参与分支
- `+0x100 & 8`
  - 第二处 call site 前的额外门控

结论：

- 如果要确定“这次 stat 流量属于哪个 actor / property 对象”，优先沿 `sub_142345E60` 这一层研究。

## status metadata 锚点

`sub_15161D7D0` 会先对传入的 `word*` 做：

- `sub_1402D7FA0(word_ptr)`

这返回的是一块 metadata 结构。当前已确认其中至少这些字段直接参与 stat 分发：

- `+0xA8`
  - 第一组 descriptor
- `+0xC8`
  - 第二组 descriptor
- `+0xC0`
  - 分支门控
- `+0xD0`
  - 分支门控
- `+0xD8`
  - 乘 `1000` 后送进下游 schedule / timer
- `+0x00`
  - 也会被用于事件 / 消息参数

辅助结论：

- `sub_1402D7FA0` 是 `Skill` 表
- `sub_140357A90` 是 `Status` 表

### `sub_15161D7D0` 的更细分工

参数语义目前已能收紧到：

- `a3`
  - 就是传入的 `word* status`
- `a4`
  - call-site selector
  - 在 `sub_142345E60` 里两处 call site 分别传 `0 / 1`
- `a5`
  - 是否允许进入 descriptor 分发
- `a6`
  - 只在 `a4 != 0` 的后半段事件逻辑里继续参与门控

函数内部分工：

- `a5 != 0`
  - 先取 `sub_1402D7FA0(a3)` 的 metadata
  - 把 `metadata + 0xA8` 送进 `sub_1412E9000`
- 若 `metadata + 0xD0 != 0`
  - 再取第二个 actor scope
  - 把 `metadata + 0xC8` 送进 `sub_1412E9000`
- `a4 != 0 && a6 != 0 && metadata + 0xC0 != 0`
  - 会发 `2038` 事件消息
- `a4 != 0`
  - 还会把 `metadata + 0xD8` 乘 `1000` 后送进 schedule / timer 逻辑

当前理解：

- `+0xA8` 是第一组 descriptor
- `+0xC8` 是第二组 descriptor
- `+0xD0` 决定第二组 descriptor 是否启用
- `+0xC0` 决定额外事件逻辑是否启用
- `sub_142345E60` 传下来的 `0 / 1` call-site selector，不只是“同一个函数的两个入口”，还直接改变 `sub_15161D7D0` 后半段是否走消息 / schedule 分支

## `sub_1412E9000` 里的 descriptor 记录

`sub_1412E9000` 会把 descriptor 拷成 0x18 字节记录后再遍历。

当前已确认该 0x18 记录里最重要的字段是：

- `record + 0x02`
  - `status_id`
- `record + 0x04`
  - 是否走直接路径的 flag
- `record + 0x08`
  - qword 参数 / 数值
- `record + 0x10`
  - 正增量侧的关联 `status_id`
- `record + 0x12`
  - 负增量侧的关联 `status_id`

关键分发逻辑：

- `record + 0x02` 会作为主要 `status_id` 往下传
- 如果 `record + 0x04 != 0`
  - 走 `0x1412E933B -> sub_1412E76A0`
- 否则可能走
  - `0x1412E9392 -> sub_1412EABE0`
  - `0x1412E940B -> sub_1412E6280`

当前 stamina 现场栈命中的是直接路径：

- `0x1412E933B -> call sub_1412E76A0`
- 返回落在 `0x1412E9340`

### `sub_1412E9000` 新确认的布局与门控

当前可以更具体地写成：

- `record + 0x02`
  - 主 `status_id`
  - 也就是传给 `sub_1412E76A0 / sub_1412EABE0 / sub_1412E6280` 的那个主键
- `record + 0x04`
  - direct flag
  - `!= 0` 时强制走 `sub_1412E76A0`
- `record + 0x08`
  - `int64` 参数
  - 在直路径里会作为 `a5`
- `record + 0x10`
  - 当 `record + 0x08 > 0` 时，作为“正增量关联查询”的 `status_id`
- `record + 0x12`
  - 当 `record + 0x08 < 0` 时，作为“负增量关联查询”的 `status_id`

补充门控：

- 该函数先从一组 type-111 对象里收集一批 `word` 白名单
- 遍历 descriptor 时：
  - 如果 `record + 0x02` 不在这批白名单里，仍会处理
  - 如果 `record + 0x02` 命中白名单，但 `record + 0x04 == 0`，会被跳过
  - 如果 `record + 0x04 != 0`，即使命中白名单也会继续处理

也就是说：

- 这里不是简单的“命中白名单才处理”
- 更准确地说，是“命中白名单且不是 direct record 时才抑制”

另外一条很重要的细节：

- `sub_1412E9000` 里传给 `sub_1412E76A0` 的末参 `a10`
  - 实际指向 `record + 0x12`
- 它再配合单独传下去的 `a9 = *(record + 0x10)`
  - 共同决定正 / 负方向的关联 stat 查询
- 所以前一版把 `record + 0x10` 记成“唯一 secondary status_id”已经不够准确
- 现在应改成：
  - `+0x10` = 正向关联
  - `+0x12` = 负向关联

## 真实 stat entry 锚点

真实 `entry*` 不需要等到 `stat-write` 才出现。

当前最直接的拿法是 `sub_1412E6090` 返回后：

- `sub_1412EAB00`
  - `0x1412EAB39 -> call sub_1412E6090`
  - 返回后 `rsi = entry*`
- `sub_1412E76A0`
  - `0x1412E7989`
  - `0x1412E799C`
  - `0x1412E79A8`
  - `0x1412E7AC2`
  - `0x1412E7AD1`
  - 调完后 `rax = entry*`

entry 布局已确认：

- `entry + 0x00` : `int32 type`
- `entry + 0x08` : `int64 current`
- `entry + 0x18` : `int64 max`

已知类型：

- `0` = Health
- `17` = Stamina
- `18` = Spirit

结论：

- 如果目标是“拿真实 stat entry 而不走通用 write 回点”，就应该盯 `sub_1412E6090` 返回后的点。

## `sub_1412E5B50` 的价值

`sub_1412E5B50(a1, out, status_id, arg)` 的作用不是主写回，而是：

- 依据 `status_id` 取得对应 entry 或相关值
- 在部分路径里会直接调用 `sub_1412E6090(a1, status_id)`
- 然后调用 `sub_1412EB6D0(...)`

结论：

- 它是一个很有价值的辅助查询点
- 但不是最终最直接的 entry 锚点
- 真正明确的 `entry*` 仍然是在 `sub_1412E6090` 返回后

### `sub_1412EAB00` / `sub_1412E6090` 的更细锚点

`sub_1412E6090(a1, status_id)` 目前已能明确写成：

- 先从 `*a1 + 0x30` 取当前 actor 的 status table id
- 再经 `sub_1404A56C0` 拿到映射表
- 最终返回：
  - `a1[11] + 0x90 * index`

因此：

- 单个 stat entry 大小是 `0x90`
- `sub_1412E6090` 本质上就是：
  - `status_id -> entry index -> entry*`

`sub_1412EAB00` 对 entry 的直接写前处理也比之前更清楚：

- `v9 = sub_1412E6090(a1, a2)`
- 如果
  - `*(char *)(v9 + 0x53) > 0`
  - 或 `current <= threshold` 且 `arg <= cap`
  - 走快速路径：
    - `++*(qword *)(v9 + 0x48)`
    - `*(qword *)(v9 + 0x38) = a3`
- 否则：
  - 走 `sub_1412EB6D0 / sub_1412EB830`
- 然后统一做：
  - `*(qword *)(v9 + 0x10) += a4`
  - `sub_1412EB5C0(v9, a3)`
  - `*(qword *)(v9 + 0x88) += a4`
  - `sub_1412EA450(a1, a2, a5)`

这说明：

- `sub_1412EAB00` 已经是很贴近真实 entry 的提交层
- 而且它直接改的就是 `sub_1412E6090` 返回的那块 `0x90` entry
- 如果后续要做更早、更干净的 stamina 专点 gate：
  - `sub_1412E6090` 返回后
  - 或 `sub_1412EAB00` 内部 entry 已落到 `rsi/v9` 后
  - 都比通用 `stat-write` 更可控

### 新的最佳修改候选点

当前最像“真正可落地”的修改点，不再是泛泛地说 `sub_1412E76A0` 或 `sub_1412EAB00`，而是下面两个更具体的位置：

1. `sub_1412E76A0`
   - `0x1412E795C -> call sub_1412EAB00`
   - `0x1412E7AB2 -> call sub_1412EAB00`

这两处 call 前，当前寄存器语义可以明确写成：

- `rcx = a1`
  - 当前 status owner / stat container
- `edx = bx`
  - 当前主 `status_id`
- `r8 = r13`
  - 时间戳 / 调度时间一类参数
- `r9 = sub_1412EACE0(...)` 产出的净 delta
  - 这是本次将要提交给 `sub_1412EAB00` 的 signed 改变量

其中：

- `sub_1412EACE0` 的 out 不是普通辅助值
- 它最终写的是：
  - `*out = 新聚合值 - 旧聚合值`
- 所以 `r9` 是一个真正的 signed net delta

2. `sub_1412EAB00`
   - `0x1412EAB39 -> call sub_1412E6090`
   - `0x1412EAB3E -> mov rsi, rax`

这里是当前最值得优先考虑的单点：

- `rsi = entry*`
  - 已经拿到真实 stat entry
- `rbx = signed delta`
  - 就是函数入口的 `a4`
- `edi = status_id`
- `r14 = owner / context`

这意味着：

- 如果挂在 `0x1412EAB3E` 附近
- 就能同时拿到：
  - 真实 `entry*`
  - signed delta
- 然后直接做：
  - `*(int32_t*)entry == 17`
    - 只处理 stamina
  - `delta < 0`
    - 只处理消耗
- 这比先完全还原一整套 stamina `status_id` 编码更直接

当前判断：

- 真正最强的修改候选点，是 `sub_1412EAB00` 里 `sub_1412E6090` 返回后
- 真正最强的目标 gate 候选点，仍然是 `sub_142345E60` 那两个特定 call site 前
- 这两层组合起来，已经非常接近“player/mount 目标集合 gate + stamina consumption 改写”的最短方案

### 正负号判断的新依据

关于 `delta` 的正负号，目前虽然还没做最终动态钉死，但静态上已经非常强：

1. 在 `sub_1412E76A0` 里：
   - `arg_20` 的正负直接决定走哪一侧关联查询
   - `a5 < 0`
     - 优先使用 `a10 -> record + 0x12`
   - `a5 > 0`
     - 优先使用 `a9 -> record + 0x10`

2. `sub_1412EACE0` 输出的是：
   - `新聚合值 - 旧聚合值`
   - 所以传给 `sub_1412EAB00` 的本来就是 signed net delta

3. `sub_1412EAB00` 内部直接做：
   - `entry + 0x10 += delta`
   - `entry + 0x88 += delta`
   - 没有再做绝对值或方向翻转

4. 对照当前已经比较清楚的 health / damage 链：
   - damage hook 现场也是直接拿 signed delta
   - 负值明确表示伤害 / 扣减
   - 当前 runtime 也只处理负向血量变化

因此当前最稳妥的工作假设应是：

- `delta < 0`
  - 就是消耗 / 扣减 / damage-like 方向
- `delta > 0`
  - 就是回复 / 返还 / refund-like 方向

对 stamina 来说，这意味着：

- 如果目标只是“改耐力消耗”
- 最优先应该只缩放 `delta < 0` 的路径
- 正向恢复先不要碰

### 2026-04-13 动态验证：`0x1412EAB3E`

使用脚本：

- [sample_stamina_ab00_entry_delta.lua](D:\Workspace\cpp\CrimsonDesertASI\stamina-spirit\ce\sample_stamina_ab00_entry_delta.lua)

验证点：

- `CrimsonDesert.exe+12EAB3E`
  - 即 `sub_1412EAB00` 里 `sub_1412E6090` 返回后

本次实验日志确认了下面几件事：

1. 这里拿到的确实是真实 stamina entry

- 日志反复命中：
  - `type=17`
  - `status=17`
- 说明这里不只是“看起来像 stamina”
  - 而是真的已经落到 stamina entry

2. `rbx`/`a4` 确实是 signed delta

日志里同一 entry 明确出现了成对的：

- `delta=-100000` / `delta=100000`
- `delta=-80000` / `delta=80000`
- `delta=-8000` / `delta=8000`
- `delta=-10000` / `delta=10000`

这进一步支持：

- `delta < 0`
  - 消耗 / 扣减
- `delta > 0`
  - 回复 / 返还

3. 当前点位会同时命中 player 与 mount

实验里至少出现了两组 stamina entry：

- player stamina entry
  - `entry = 0x2BB882ED680`
  - `owner = 0x2BB880E03C0`
- mount stamina entry
  - `entry = 0x2BBDFB30E80`
  - `owner = 0x2BBAC7C76C0`

这和当前目标是吻合的：

- 不要求把 player / mount 拆开
- 同一点位同时覆盖二者是可接受方案

4. 需要注意 entry 基址与 current 字段地址不要混淆

用户现场给出的：

- `0x2BB882ED688`

对应的不是 entry 基址，而是：

- `player stamina entry + 0x08`
  - 也就是 current 字段地址

真正用于 hook / 白名单 / 类型判断的 entry 基址应是：

- `0x2BB882ED680`

这三者当前应明确区分：

- `entry = 0x2BB882ED680`
  - 这是完整 stat entry 基址
- `entry + 0x08 = 0x2BB882ED688`
  - 这是 current value 字段地址
- 日志里的 `cur=320000 / 318575 / ...`
  - 是对 `readQword(entry + 0x08)` 的读取结果

因此：

- 说“玩家目标是 `0x2BB882ED688`”
  - 如果语义是“玩家当前耐力值地址”
  - 这是正确的
- 说“hook 要匹配的 stamina entry 是 `0x2BB882ED688`”
  - 这就不准确
  - hook / entry type 判断仍应使用 `0x2BB882ED680`

### 2026-04-13 静态收敛：`sub_1412E9930` 才是 player stamina 更好的观测层

这一步需要明确和之前的 `AB00` 结论切开：

- `sub_1412EAB00`
  - 适合 `mount stamina` 这种“直接锁”
  - 不适合继续拿来做 `player stamina` 倍率
- `player stamina`
  - 更应该回到 `sub_1412E9930 -> sub_1412E76A0 -> sub_1412EAB00` 这层
  - 先看协议展开前的原始业务量，再决定最终 hook 点

当前已经静态确认：

1. `sub_1412EAB00` 看到的 `rbx/a4`
   - 不是单纯的原始业务消耗值
   - 它是经过 `sub_1412EACE0` 聚合后的 net delta

2. `sub_1412E9930` 里 `0x1412E9EBB` 前后
   - `rdi` 仍然是协议展开前的原始业务量
   - 还没有被乘 `1000` 并下发到 `sub_1412E76A0`

3. `sub_1412E9930` 里的 mode 分组已经能稳定写成：
   - `{0,4}`
     - 先发一次正向 `+rdi * 1000`
     - 若 `v73 != 0` 再发一次负向缩放值
   - `{1,5}`
     - 同样是“先正后负”的双阶段
     - 但第一次调用给 `sub_1412E76A0` 的 `a2/dl` 不同
   - `{2}`
     - 只发一次负向缩放值
   - `{3}`
     - 也只发一次负向缩放值
     - 但给 `sub_1412E76A0` 的 `a2/dl` 和 mode 2 不同

4. `sub_1412E76A0` 的四个关键 callsite：
   - `0x1412EA004`
     - return `0x1412EA009`
   - `0x1412EA051`
     - return `0x1412EA056`
   - `0x1412EA11F`
     - return `0x1412EA124`
   - `0x1412EA1B2`
     - return `0x1412EA1B7`

5. `0x1412E9EBB` 的候选 AOB 已在 IDA 验证唯一：

```text
0F B6 ?? ?? 48 85 FF 0F 8E ?? ?? ?? ?? 41 F6 ?? FB
```

当前工程结论：

- `mount stamina`
  - 继续保留在 `AB00`
- `player stamina`
  - 不要回到 `AB00` 上继续猜正负成对
  - 下一步应优先动态验证 `0x1412E9EBB` 和 `sub_1412E76A0` 这两层的 live log

### 2026-04-13 新增 CE live probe

已新增脚本：

- [sample_stamina_9930_live.lua](D:\Workspace\cpp\CrimsonDesertASI\stamina-spirit\ce\sample_stamina_9930_live.lua)

用途：

- 在 `CrimsonDesert.exe+12E9EBB` 记录：
  - `status_id`
  - `mode`
  - `mode group`
  - `rdi` 原始业务量
  - `r14` actor/status context
- 在 `sub_1412E76A0` 入口记录：
  - 4 个 `9930` 下游 callsite 的实际命中
  - `a2`
  - `a3`
  - `a4`
  - `a5`
  - `a5` 的正负号与 `/1000` 后的可读值

目标：

- 直接验证 `player sprint / recovery / mount movement` 三类场景
- 判断 `0x1412E9EBB` 的 `rdi` 是否与后续 `a5` 一一对应
- 为下一步真正的 `player stamina` 新 hook 选点提供动态证据

### 这次实验后的结论

到这一步，可以把判断进一步收紧为：

- `0x1412EAB3E` 已经足够证明是一个可用修改入口
- 因为这里同时具备：
  - 真实 `entry*`
  - 已确认的 stamina 类型 `17`
  - signed delta
- 并且动态上已经看到：
  - player 与 mount 都会走到这里
  - 负值与正值成对出现，符合“消耗 / 回复”语义

因此：

- 如果下一步要真正下手改 stamina consumption
- 完全可以优先从 `sub_1412EAB00` 这一点做
- 最保守的第一版策略应该是：
  - 只处理 `*(int32_t*)entry == 17`
  - 只处理 `delta < 0`
  - 暂时不要动 `delta > 0`

## 当前最值得保留的核心判断

- 不再以 `stat-write` 为主线。
- 不需要把整条链都研究到“完全语义化”。
- 真正要找的是：
  - 一个能把 `player / mount` 目标集合识别出来的点
  - 一个能稳定改 stamina consumption 的点
- 当前看下来，最有价值的两层仍然是：
  - `sub_142345E60`
    - 更像 `player / mount` 目标识别层
  - `sub_1412E6090` 返回后到 `sub_1412EAB00 / sub_1412E76A0`
    - 更像真实 stamina entry / consumption 提交层
- 如果后续能把这两层用一条更短的 gate 连起来，就已经满足目标
- 不需要为了 player 和 mount 各自单独挂点
- 如果二者能在同一条 consumption 链上被一起 gate 住，就是合格方案

## `sub_142345E60` 新增静态结论

这层现在至少可以确认到下面这些事实：

- 它确实在遍历两路 lane：`i = 0 / 1`
- 每路都对应一个 `0x18` 的槽
- `slot + 0x10`
  - 是核心对象指针
- 但 `slot` 前半段不能再简单记成两个“纯 qword 黑盒”
  - 因为 `sub_14193EB10(slot, out)` 会把 `slot` 当成 `word*`
  - 直接消费 `slot[0] / slot[1] / slot[2]`
  - 也就是前 6 字节至少是三段 index / selector
- 同时 `sub_142345E60` 仍会粗略检查：
  - `*(qword *)(slot + 0x00) != 0`
  - `*(qword *)(slot + 0x08) != 0`
  - `*(qword *)(slot + 0x10) != 0`

当前更保守、也更准确的写法应是：

- `slot + 0x00 .. +0x05`
  - 至少包含三段 `word` 索引
- `slot + 0x10`
  - 核心对象指针
- `slot + 0x06 .. +0x0F`
  - 仍未完全定性
  - 但属于 `sub_142345E60` 的粗有效性检查范围

当前真正有价值的是第三个 qword 指向的对象：

- `obj + 0x98`
  - 映射成 `word status`
- `obj + 0xE0`
  - 类别 / role byte
- `obj + 0x100 & 8`
  - 第二处 call site 前的硬门控
- `obj + 0x103 & 2`
  - 有效性门控

还可以确认两组 24-byte 槽不是对称地“无脑都 call”：

- 第一处：
  - `0x142345FE6 -> call sub_14234C500`
  - 传 `a4 = 0`
  - 主要取自 `a5` 这组槽
- 第二处：
  - `0x1423461B6 -> call sub_14234C500`
  - 传 `a4 = 1`
  - 主要取自 `a4` 这组槽
  - 且会额外受 `obj + 0x100 & 8` 抑制

附加观察：

- 函数在进入 loop 前，会先从 `*a5` 的第一个槽对象取一次 `+0xE0`
- 也会从 `*a4` 的第一个槽对象取一次 `+0xE0`
- 这两个 role byte 随后作为整轮 loop 的 lane gate 使用
- 说明它不是到 `stat-write` 才区分 player / mount / 其它对象
- 分流早就在 `sub_142345E60` 完成了
- `sub_14193EB10(slot, out)` 还说明：
  - `slot[0] / slot[1] / slot[2]` 参与一段分层索引查找
  - 会产出一个 4-qword 的 `out`
  - `sub_142345E60` 随后会用 `out[0] + 0x1C` 的 bit `0x4000 / 0x8000`
    - 回写 `a1[74] / a1[76]` 这两路 gate

当前更稳妥的表述应该是：

- `sub_142345E60` 已经拿到了足够早的 actor/property 候选对象
- 两处 stamina 相关 call site 的分叉，也已经在这里完成
- 后面进入 `sub_15161D7D0 -> sub_1412E9000 -> sub_1412E76A0 / sub_1412EAB00` 时，已经是在共享通用 stat 分发链上跑

## 下个 session 建议继续做的事

1. 优先确认 `sub_142345E60` 里哪一个字段最适合做 `player / mount` 目标集合 gate：
   - 不求把整个 24-byte 槽完全解释完
   - 只求找到一个足够稳定的 actor / property 识别锚点
   - 不要求继续把 player 和 mount 再彼此拆开

2. 优先确认 stamina consumption 最贴近的可改点：
   - 首选 `sub_1412E6090` 返回后
   - 次选 `sub_1412EAB00 / sub_1412E76A0` 内部
   - 目标是直接影响消耗，不回退到通用 `stat-write`

3. 如果需要动态验证，只围绕“目标识别 + 消耗”两件事打点：
   - `0x142345FE6 / 0x1423461B6`
     - 看能否稳定识别出 `player / mount` 目标集合
   - `0x1412EAB39 / 0x1412E7989`
     - 看命中的是否就是 stamina consumption 相关 entry
   - 不再为泛化语义去打大范围 hook
