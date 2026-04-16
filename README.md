# player-status-modifier

`player-status-modifier` is an ASI mod for `Crimson Desert` built as a native x64 DLL and loaded through an ASI loader.

## Implementation Notes

This project uses [safetyhook](https://github.com/cursey/safetyhook) for mid-function hooks.

Current runtime behavior is split across several hook paths:

- player pointer capture
- player stat entry discovery
- spirit signed-delta interception
- mount stamina AB00 interception
- dragon village summon bypass
- dragon forced-dismount result rewrite
- dragon roof summon experimental bypass
- player damage scaling
- item gain scaling
- affinity gain scaling
- equipment maintenance consumption control
- weapon durability consumption control
- optional position height control
- optional position horizontal movement scaling
- automatic config hot reload

For player stats, the runtime is now split by semantics instead of forcing everything through one shared write path:

- `stats` only discovers the player's real stat entries
- `spirit` adjusts signed delta at `CrimsonDesert.exe+C6A72AB`
- `player health` recovery scaling currently rides the incoming-damage semantic path
- `player stamina` scales again at the shared `stat-write` commit point, but only when `rdi == tracked player stamina entry`
- `mount stamina` still uses `AB00` because that feature only needs a direct lock

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
- Affinity gain multiplier
- Equipment maintenance / durability consumption chance
- Mount health / stamina lock
- Dragon village summon bypass
- Dragon forced-dismount bypass
- Dragon roof summon experimental bypass
- Position height control
- Position horizontal movement scaling

`Stamina.HealMultiplier` also affects natural stamina regeneration. The default is kept at `1.0` so reduced stamina consumption does not also speed up passive recovery unless you want that behavior.

`Position Control(Height)` is disabled by default. When enabled, the mod installs an additional controlled-character position hook and starts a dedicated key-listener thread. Pressing the configured key accumulates a small vertical offset that is applied at the position update point.

`Position Control(Horizontal)` is also disabled by default. When enabled, the same controlled-character position hook scales the X/Z movement delta while the configured key is held, without scaling the absolute world position itself.

The mod also watches `player-status-modifier.ini` in the background and reloads changes automatically. Most multipliers update on the next write immediately, while position-control key changes are applied by reconfiguring the listener threads in place. Hook installation itself is still not toggled during runtime.

Hook installation policy:

- player-pointer capture always stays installed because the rest of the runtime depends on it
- shared player stat discovery stays centered on `stats`, while the runtime only installs the semantic hooks it actually needs (`spirit-delta`, `stamina-ab00`)
- `spirit-delta` is installed only when `Spirit` is not neutral (`1.0 / 1.0`)
- `stamina-ab00` is installed only when `Stamina` is not neutral or mount stamina lock is enabled
- damage, items, affinity, durability, dragon-limit, and position-control hooks are only installed when their corresponding feature is actually enabled in the config
- if an INI edit changes the required hook loadout, the watcher logs a restart warning; toggling hooks on or off still requires a game restart

`Spirit` now uses a dedicated signed-delta semantic site rather than the legacy shared `stat-write` path. See [research-note-spirit.md](/D:/Workspace/cpp/CrimsonDesertASI/stamina-spirit/research-note-spirit.md) for the validated route, register semantics, and AOB selection rationale.

The repository now also includes `player-status-modifier.default.ini` as a clean baseline. Use it as a reference or restore point if your live config drifts too far during testing.

Dragon limit behavior:

- all three dragon-limit hooks stay passive until the runtime has captured the current player actor and player status marker
- `village_summon` bypasses the village fast-exit gate and forces the request to continue into the full summon-rule path
- `cancel_restrict_flying` only rewrites the forced-dismount result signature (`RDI=0x247`, `AL=0`, `[RBX]=6`) and leaves manual dismount behavior intact
- `roof_summon_experimental` is intentionally disabled by default because it still clears a late callback result and should be treated as experimental

Damage behavior:

- `OutgoingDamage` scales negative health deltas caused by the player or the currently resolved mount / dragon actor
- `IncomingDamage` scales negative health deltas applied back to the player's resolved target owner
- legacy `[Damage] Multiplier=...` is still accepted as a fallback for `OutgoingDamage.Multiplier`

Default config:

```ini
[General]
Enabled=1
LogEnabled=1
verbose=0
MaxLogLines=2000
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

[Affinity]
Multiplier=1.0

[Durability]
ConsumptionChance=100.0

[Mount]
Enabled=0
LockHealth=1
LockStamina=1
LockValue=9999999

[DragonLimit]
roof_summon_experimental=0
village_summon=1
cancel_restrict_flying=1

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

Affinity fields:

- `Multiplier` scales positive affinity deltas before the game commits the updated value
- the hook rewrites the pending `record + 0x10` value using `old_value + floor((new_value - old_value) * multiplier)`
- non-positive deltas are left untouched
- the final value is clamped to the game-side cap (`100`)
- `1.0` keeps affinity unchanged and skips installing the affinity hook entirely

Mount fields:

- `Enabled=1` enables mount stat locking after the async resolver validates the current mount / dragon marker
- `LockHealth=1` locks the resolved mount health entry to `LockValue`
- `LockStamina=1` locks the resolved mount stamina entry to `LockValue`
- `LockValue` is clamped to the stat max during write interception

DragonLimit fields:

- `roof_summon_experimental=1` enables the current roof / high-place summon experimental bypass; this is still a high-risk result-layer patch and is disabled by default
- `village_summon=1` enables the village summon bypass by forcing the validated request into the downstream summon-rule chain
- `cancel_restrict_flying=1` prevents the validated forced-dismount result from ejecting the player from the dragon while keeping manual dismount available
- none of these dragon-limit edits become active until the mod has already captured the current player actor and player status marker at runtime

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
