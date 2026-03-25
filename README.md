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

For health, stamina, and spirit, the mod no longer relies only on read-side compensation. It first discovers the player's real stat entries, then adjusts values at the main write path when the write target matches a discovered player entry.

## Current Features

The mod currently supports the following configurable features through `player-status-modifier.ini`:

- Health consumption multiplier
- Health heal multiplier
- Stamina consumption multiplier
- Stamina heal multiplier
- Spirit consumption multiplier
- Spirit heal multiplier
- Player damage multiplier
- Item gain multiplier

`Stamina.HealMultiplier` also affects natural stamina regeneration. The default is kept at `1.0` so reduced stamina consumption does not also speed up passive recovery unless you want that behavior.

Default config:

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
