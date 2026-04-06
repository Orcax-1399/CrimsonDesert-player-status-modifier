# player-status-modifier

`player-status-modifier` is an ASI mod for `Crimson Desert` built as a native x64 DLL and loaded through an ASI loader.

## Implementation Notes

This project uses [safetyhook](https://github.com/cursey/safetyhook) for mid-function hooks.

Current runtime behavior is split across several hook paths:

- player pointer capture
- player stat entry discovery
- source stat write interception
- player damage scaling
- item gain scaling
- equipment maintenance consumption control
- weapon durability consumption control
- optional position height control
- optional position horizontal movement scaling
- automatic config hot reload

For health, stamina, and spirit, the mod no longer relies only on read-side compensation. It first discovers the player's real stat entries, then adjusts values at the main write path when the write target matches a discovered player entry.

## Current Features

The mod currently supports the following configurable features through `player-status-modifier.ini`:

- Health consumption multiplier
- Health heal multiplier
- Stamina consumption multiplier
- Stamina heal multiplier
- Spirit consumption multiplier
- Spirit heal multiplier
- Outgoing damage multiplier
- Incoming damage multiplier
- Item gain multiplier
- Equipment maintenance / durability consumption chance
- Mount health / stamina lock
- Position height control
- Position horizontal movement scaling

`Stamina.HealMultiplier` also affects natural stamina regeneration. The default is kept at `1.0` so reduced stamina consumption does not also speed up passive recovery unless you want that behavior.

`Position Control(Height)` is disabled by default. When enabled, the mod installs an additional controlled-character position hook and starts a dedicated key-listener thread. Pressing the configured key accumulates a small vertical offset that is applied at the position update point.

`Position Control(Horizontal)` is also disabled by default. When enabled, the same controlled-character position hook scales the X/Z movement delta while the configured key is held, without scaling the absolute world position itself.

The mod also watches `player-status-modifier.ini` in the background and reloads changes automatically. Most multipliers update on the next write immediately, while position-control key changes are applied by reconfiguring the listener threads in place. Hook installation itself is still not toggled during runtime.

The repository now also includes `player-status-modifier.default.ini` as a clean baseline. Use it as a reference or restore point if your live config drifts too far during testing.

Damage behavior:

- `OutgoingDamage` scales negative health deltas caused by the player or the currently resolved mount / dragon actor
- `IncomingDamage` scales negative health deltas applied back to the player's resolved target owner
- legacy `[Damage] Multiplier=...` is still accepted as a fallback for `OutgoingDamage.Multiplier`

Default config:

```ini
[General]
Enabled=1
LogEnabled=1
InitDelayMs=3000
StaleComponentMs=60000
RelockIdleMs=10000

[OutgoingDamage]
Enabled=1
Multiplier=2.0

[IncomingDamage]
Enabled=0
Multiplier=1.0

[Items]
GainMultiplier=2.0

[Durability]
ConsumptionChance=100.0

[Mount]
Enabled=0
LockHealth=1
LockStamina=1
LockValue=9999999

[Position Control(Height)]
Enable=0
Key=117
Amplitude=0.1

[Position Control(Horizontal)]
Enable=0
Key=118
Multiplier=1.5

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

Durability fields:

- `ConsumptionChance` is clamped to `0..100`
- `100` means maintenance and durability always consume normally
- `0` means maintenance and durability never consume
- values between `0` and `100` apply a per-write chance gate to both maintenance and durability loss paths

Damage fields:

- `OutgoingDamage.Enable=1` enables outgoing player / mount / dragon damage scaling
- `OutgoingDamage.Multiplier` scales outgoing negative health deltas
- `IncomingDamage.Enable=1` enables incoming damage scaling against the resolved player target
- `IncomingDamage.Multiplier` scales incoming negative health deltas; `1.0` means unchanged

Mount fields:

- `Enabled=1` enables mount stat locking after the async resolver validates the current mount / dragon marker
- `LockHealth=1` locks the resolved mount health entry to `LockValue`
- `LockStamina=1` locks the resolved mount stamina entry to `LockValue`
- `LockValue` is clamped to the stat max during write interception

Position height control fields:

- `Enable=1` turns on the height-control hook and key listener
- `Key` is a Windows virtual-key code, default `117` (`VK_F6`)
- `Amplitude` is the amount added to the height axis per successful key-listener poll

Position horizontal control fields:

- `Enable=1` turns on horizontal movement scaling and its dedicated key listener
- `Key` is a Windows virtual-key code, default `118` (`VK_F7`)
- `Multiplier` scales only the per-update X/Z movement delta; `1.0` means no change

## Build

Requirements:

- Visual Studio 2022
- CMake

Build example:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output:

- `build/Release/player-status-modifier.asi`

## Credits

- [FLiNG Trainer - Crimson Desert Trainer](https://flingtrainer.com/trainer/crimson-desert-trainer/) for reverse-engineering reference and opcode validation
- [safetyhook](https://github.com/cursey/safetyhook) for the hooking framework
