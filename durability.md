# Durability Notes

This document describes the current durability-related behavior implemented in `player-status-modifier`.

## Overview

The game currently exposes three separate loss paths that are all covered by the same durability feature:

- equipment maintenance consumption
- weapon durability consumption
- AbyssGear durability consumption

These are not the same field and not the same function, but they are intentionally grouped under one shared config option in the mod.

## Config

Config section:

```ini
[Durability]
ConsumptionChance=100.0
```

Current behavior:

- `100.0` means durability-related consumption always applies
- `0.0` means durability-related consumption never applies
- values between `0.0` and `100.0` are treated as a per-write chance gate
- values above `100.0` are clamped to `100.0`
- values below `0.0` are clamped to `0.0`

At the moment, this single option affects all of:

- maintenance consumption
- durability consumption
- AbyssGear durability consumption

If needed later, this can be split into two independent config fields.

## Path 1: Maintenance

Confirmed write path:

```asm
66 89 7B 50    mov [rbx+50],di
```

Observed behavior:

- `rbx+50` is the maintenance-related value being written back
- the hook is installed at the final write-back path
- only negative updates are filtered
- non-loss updates are left unchanged

Current implementation:

- scanner: `ScanForDurabilityWriteAccess`
- hook callback: `DurabilityCallback`
- runtime logic: `TryAdjustDurabilityWrite`

## Path 2: Durability

Confirmed update path:

```asm
0F B7 C7          movzx eax,di
66 41 03 C5       add ax,r13w
66 89 45 38       mov [rbp+38],ax
```

Observed behavior:

- `rbp+38` is the durability field updated by this function
- `r13w` carries the current durability delta
- negative delta means consumption
- the game continues into its own sign and clamp handling after the add

Current implementation:

- scanner: `ScanForDurabilityDeltaAccess`
- hook callback: `DurabilityDeltaCallback`
- runtime logic: `TryAdjustDurabilityDelta`

The mod hooks before the write-back and zeros only negative delta when the chance check says the loss should be skipped. This preserves the original lower-bound and follow-up game logic.

## Path 3: AbyssGear Durability

Confirmed update path:

```asm
0F B7 73 02       movzx esi,word ptr [rbx+02]
66 41 3B F5       cmp si,r13w
42 8D 04 2E       lea eax,[rsi+r13]
66 0F 4D F8       cmovge di,ax
66 89 7B 02       mov [rbx+02],di
...
66 89 7B 02       mov [rbx+02],di
```

Observed behavior:

- `rbx+02` is the AbyssGear durability field updated by this function
- `r13w` carries the current durability delta
- negative delta means consumption
- the game performs two writes on the same field inside the same flow

Current implementation:

- scanner: `ScanForAbyssDurabilityDeltaAccess`
- hook callback: `AbyssDurabilityDeltaCallback`
- runtime logic: `TryAdjustDurabilityDelta`

The mod hooks before `lea eax,[rsi+r13]` and zeros only negative delta when the chance check says the loss should be skipped. This lets both later writes reuse the game's original update and clamp path.

## Shared Runtime Rule

The durability feature does not increase values and does not try to invent a custom clamp. It only decides whether an already-detected loss should be allowed through.

Practical result:

- if the game is trying to consume durability, the mod may cancel that loss
- if the game is repairing or otherwise increasing durability-related values, the mod does nothing

## Source Files

Main files involved:

- `src/config.h`
- `src/config.cpp`
- `src/scanner.h`
- `src/scanner.cpp`
- `src/hooks.cpp`
- `src/mod_logic.h`
- `src/mod_logic.cpp`

## Notes

- The three paths are intentionally grouped under one user-facing setting for now.
- The maintenance path, the durability path, and the AbyssGear durability path were confirmed separately during runtime testing.
- If a future game update changes one path but not the others, they can be maintained independently because they already use separate scanners and separate hook callbacks.
