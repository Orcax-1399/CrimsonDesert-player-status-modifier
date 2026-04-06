# ActorResolve Design For Player / Dragon / Mount

## Purpose

This document defines the object reconstruction model that should replace the current ad-hoc dragon / mount identification logic in the mod.

The key idea is:

- player, dragon, and mount should all be represented by one reconstructed runtime object
- that object should be buildable from multiple known entry points
- once resolved, it should expose every address needed by stat and damage logic

This document is intentionally implementation-oriented so it can survive compact and be used directly in the next code pass.

## The Goal Object

The resolver should build one complete object, conceptually:

```cpp
struct ActorResolve {
    uintptr_t actor;
    uintptr_t marker;
    uintptr_t root;
    uintptr_t health_entry;
    uintptr_t stamina_entry;
    uintptr_t spirit_entry;

    uintptr_t damage_source;
    uintptr_t damage_target;

    bool valid;
};
```

The exact field names can change, but the resolved object must at least contain:

- `actor`
- `marker`
- `root`
- `health_entry`
- `stamina_entry`
- `spirit_entry`
- damage-related identity fields needed by the current damage hook

If any implementation only resolves `marker/root/health/stamina` but cannot support damage-side identity, it is still incomplete.

## Core Shared Structure

The following relations were verified live and are the foundation of the resolver:

```text
actor + 0x20 -> marker
marker + 0x18 -> root
root + 0x00 -> marker
root + 0x58 -> health entry
health entry + 0x480 -> stamina entry
health entry + 0x510 -> spirit entry
[[marker + 0x8] + 0x68] -> actor
```

This means the actor can be reconstructed in both directions:

- forward from `actor`
- backward from `marker`
- backward from `root`
- backward from `health entry`
- backward from `stamina entry`
- backward from `spirit entry`

## Required Resolver Entry Points

The next code pass should expose a small set of constructors / helper paths that all converge to the same final object.

### 1. Resolve From Actor

```text
actor
 -> [actor + 0x20] = marker
 -> [marker + 0x18] = root
 -> [root + 0x58] = health_entry
 -> [health_entry + 0x480] = stamina_entry
 -> [health_entry + 0x510] = spirit_entry
```

This is the most direct path when a reliable actor already exists.

### 2. Resolve From Marker

```text
marker
 -> [marker + 0x18] = root
 -> [root + 0x58] = health_entry
 -> [health_entry + 0x480] = stamina_entry
 -> [health_entry + 0x510] = spirit_entry
 -> [[marker + 0x8] + 0x68] = actor
```

This is currently the most important path because marker is more stable than actor in practice.

### 3. Resolve From Root

```text
root
 -> [root + 0x00] = marker
 -> ResolveFromMarker(marker)
```

This is one of the cleanest reverse paths because `root + 0 == marker` is a strong validation point.

### 4. Resolve From HealthEntry

The exact code path should resolve root first, then marker, then actor.

Conceptually:

```text
health_entry
 -> reverse to root
 -> [root + 0x00] = marker
 -> [[marker + 0x8] + 0x68] = actor
 -> [health_entry + 0x480] = stamina_entry
 -> [health_entry + 0x510] = spirit_entry
```

This matters because some runtime hooks and live memory observations discover stat entry first, not actor first.

### 5. Resolve From StaminaEntry

```text
stamina_entry
 -> health_entry = stamina_entry - 0x480
 -> ResolveFromHealthEntry(health_entry)
```

### 6. Resolve From SpiritEntry

```text
spirit_entry
 -> health_entry = spirit_entry - 0x510
 -> ResolveFromHealthEntry(health_entry)
```

### 7. Resolve From Player-Owned Mount Slot

This is the path that should replace the current dragon / mount identification logic in the mod.

Flow:

1. start from the player object
2. read one or more current mount pointer chains owned by the player
3. if the chain result is null, stop safely
4. if the chain result is non-null, normalize it into a `marker` or a known object that can derive `marker`
5. call `ResolveFromMarker`
6. reject the result if it resolves back to player

This should become the runtime source of truth for current dragon / mount tracking.

## Why Marker Matters More Than Actor

The live debugging results consistently showed:

- actor addresses drift heavily between sessions
- actor pointer scans are weak and unstable
- marker, root, and entry relations are much more reproducible

So the design should treat:

- `actor` as useful derived identity
- `marker` as the preferred stable reconstruction anchor

This is the exact opposite of the earlier noisy hook-based approach.

## Strong Validation Rules

Every resolver path should converge to the same validation gate.

Minimum required checks:

1. input pointer is non-null
2. `marker` is non-null
3. `root = [marker + 0x18]`
4. `root` is non-null
5. `[root + 0x00] == marker`
6. `health_entry = [root + 0x58]`
7. `health_entry` is non-null
8. `stamina_entry = [health_entry + 0x480]`
9. `spirit_entry = [health_entry + 0x510]`
10. `stamina_entry` is non-null
11. `spirit_entry` is non-null
12. `[stamina_entry + 0x00] == 17`
13. `[spirit_entry + 0x00] == 18`

Optional but recommended:

1. `actor = [[marker + 0x8] + 0x68]`
2. if actor exists, `[actor + 0x20] == marker`

If any critical step fails, the resolve result should be invalid and unusable.

## Safe Pointer Chain Helper

The implementation needs one dedicated helper for pointer-chain reads.

This is mandatory because the player-owned dragon chain is null before the player mounts, and a blind access will crash.

Conceptually:

```cpp
bool TryReadPtr(uintptr_t address, uintptr_t* out);
bool TryReadChain(uintptr_t base, std::span<const uintptr_t> offsets, uintptr_t* out);
```

Expected behavior:

1. if `base == 0`, fail
2. before each dereference, verify the current pointer is non-null
3. if a read fails, fail cleanly
4. never dereference a null intermediate node
5. return `false` instead of crashing

The mount-specific chain helper can then be built on top:

```cpp
bool TryResolveMountCandidateFromPlayer(uintptr_t player_actor, uintptr_t* out_candidate);
```

This helper should:

- safely walk the known player-owned pointer chains
- tolerate the unmounted state
- return null / false when the chain is absent
- never treat absence as an error condition

## Confirmed Reverse Paths

The reverse relations currently worth preserving are:

### Marker To Actor

```text
[[marker + 0x8] + 0x68] -> actor
```

This is the main confirmed reverse actor recovery path from marker.

Forward cross-check:

```text
[actor + 0x20] == marker
```

### Root To Marker

```text
[root + 0x00] -> marker
```

This is the strongest structural confirmation found so far.

### HealthEntry To Other Entries

```text
health_entry + 0x480 -> stamina_entry
health_entry + 0x510 -> spirit_entry
```

This lets stat logic rebuild the whole stat set once health entry is known.

### Entry-Side Reconstruction

The important live conclusion is:

- entry can be used as a reconstruction entry point
- once root is recovered, the rest of the object follows

The code should therefore support rebuild from:

- `root`
- `health_entry`
- `stamina_entry`
- `spirit_entry`

instead of only from actor-like callback inputs.

## What The Resolved Object Must Provide

A fully resolved actor object should be enough for all of the following:

1. identify whether a stat entry belongs to that actor
2. get current health entry
3. get current stamina entry
4. get current spirit entry
5. compare player and mount identities safely
6. feed damage-side source / target checks
7. support later dragon-specific damage scaling if needed

In other words, the resolved object is not just for stat writes. It is the shared identity model for the mod.

## Damage-Side Requirement

The damage hook should eventually consume the same reconstructed identity model.

That means the resolved object must be able to expose whatever the current damage logic needs for:

- source actor recognition
- target actor recognition
- player-versus-dragon disambiguation

This is important because the old dragon detection path and the new damage path must not diverge into two incompatible identity systems.

## Player And Mount Relationship Rules

The runtime should enforce these rules:

1. player resolve is the primary anchor
2. mount resolve is derived from player-owned current mount chains
3. if mount resolve collapses back to player identity, skip mount logic
4. if player is not mounted, mount resolve must clear safely
5. unknown or partially resolved objects must not be used for write scaling

Possible identity comparisons:

- `actor`
- `marker`
- `root`

`marker` is likely the best primary equality check once validated.

## Confirmed Player Sample

Verified live sample:

```text
actor   = 0x000002B7FA1B0400
marker  = 0x000002B7FA260A00
root    = 0x000002B7FA0E03C0
health  = 0x000002B7FAC33200
stamina = 0x000002B7FAC33680
```

Observed values:

```text
health + 0x08 = 797450
health + 0x18 = 1050000

stamina + 0x00 = 17
stamina + 0x08 = 360000
stamina + 0x18 = 360000
```

This is the baseline validation sample for the player side.

## Confirmed Dragon Samples

### Sample A

```text
marker  = 0x00000472B1F42D00
root    = 0x00000472B1FFB540
health  = 0x000004725FD5E600
stamina = 0x000004725FD5EA80
actor   = 0x00000472B14D5C00
```

Observed values:

```text
health + 0x08 = 1973000
health + 0x18 = 2500000

stamina + 0x00 = 17
stamina + 0x08 = 300000
stamina + 0x18 = 300000
```

### Sample B

```text
marker  = 0x000004EB3626FA00
root    = 0x000004EB3A928340
health  = 0x000004EA034BAA00
stamina = 0x000004EA034BAE80
```

Observed values:

```text
health + 0x08 = 1972600
health + 0x18 = 2500000

stamina + 0x00 = 17
stamina + 0x08 = 300000
stamina + 0x18 = 300000
```

### Sample C

```text
marker  = 0x00000266FA4D6400
root    = 0x00000266FA4FCD00
health  = 0x00000266FA686400
stamina = 0x00000266FA686880
```

Observed values:

```text
health + 0x08 = 2500000
stamina + 0x08 = 300000
```

This further confirms the same structure on a legal full-health dragon.

## What Should Be Replaced In The Mod

The current dragon / mount detection logic in the mod should be replaced with:

1. resolve player into `ActorResolve`
2. resolve current mount candidate from player-owned safe pointer chain helper
3. rebuild mount into `ActorResolve`
4. validate mount object fully
5. skip if it resolves back to player
6. use that resolved mount object for:
   - health scaling
   - stamina scaling
   - spirit scaling
   - damage source / target logic

The old noisy hook-based dragon detection path should stop being the primary runtime source.

## Bottom Line

The correct abstraction is not “dragon marker tracking”.

The correct abstraction is a fully reconstructed `ActorResolve` object that can be built from:

- actor
- marker
- root
- health entry
- stamina entry
- spirit entry
- player-owned mount pointer chain

Once this abstraction exists, the current dragon logic in the mod can be replaced cleanly instead of patched incrementally.
