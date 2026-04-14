# Stamina Dual Flow TODO (2026-04-14)

## Superseded

- 2026-04-14 最新结论：
  - 这份“双流直接 hook”方案已暂停
  - `player stamina` 改回 `stat-write`
  - `player stamina` 在 mod 里的最终门控收敛为：
    - `rdi == tracked player stamina entry`
    - 玩家未初始化时完全不工作
  - `AB00` 继续只给 `mount stamina`

## Goal

- 在 mod 里同时接入 `player stamina` 的两类消耗：
  - 瞬时消耗
  - 持续消耗
- 保持 `mount stamina` 继续走 `AB00` 锁定

## Checklist

- [x] 把双流模型和门控规则写回 `research-note-stamina.md`
- [x] 在 scanner 里新增：
  - `C6A9CBF` 瞬时消耗 hook 扫描
  - `12E933B` 持续消耗 hook 扫描
- [x] 在 hook 层新增：
  - `stamina-short` callback / install / remove
  - `stamina-continuous` callback / install / remove
- [x] 在 runtime 层新增：
  - `player stamina` 瞬时消耗缩放
  - `player stamina` 持续消耗负 packet 缩放
- [x] 保持现有 `mount stamina @ AB00` 逻辑不回退
- [x] 编译或至少完成静态检查，确认没有接口遗漏
- [x] 回填本文件，记录完成状态与剩余风险

## Current assumptions

- 瞬时消耗主线：
  - `CrimsonDesert.exe+C6A9CBF`
  - `rsi = stamina entry`
  - `rbx = signed delta`
- 持续消耗主线：
  - `CrimsonDesert.exe+12E933B`
  - `rcx/rsi = owner root`
  - `status == 17`
  - `direct == 1`
  - 只处理 `packet < 0`

## Validation targets

- [ ] 翻滚
- [ ] 空中冲刺
- [ ] 滑翔
- [ ] 加速飞行
- [ ] 落地回耐
- [ ] 走路 / 满耐待机

## Status

- 2026-04-14:
  - 双流直接 hook 方案已回退
  - `player stamina` 当前正式实现为：
    - `stat-write` 提交层缩放
    - 仅对白名单 `tracked player stamina entry` 生效
  - `mount stamina` 继续留在 `AB00`
  - 文档、TODO、hook、runtime 已回填到当前决策
  - `Release` 已成功编译：
    - `build\\Release\\player-status-modifier.asi`
  - 当前剩余风险：
    - `player stamina` 仍是通用提交层，不是更高语义点
    - 但现阶段已确认只需 `entry` 白名单即可稳定过滤
