# Position Research

## Purpose

This document records the current reverse-engineering status for player position discovery in `Crimson Desert`.

The player-pointer AOB is currently unstable again after a game update, so this file preserves the useful findings collected so far. Once the player hook is repaired, this should make it straightforward to continue the position work without redoing the whole investigation.

## Current Status

- The old player-pointer work was good enough to recover the current player actor and marker.
- A later game update broke that player-pointer AOB again.
- Position work should resume only after the player actor can be tracked reliably again.
- The position-related runtime path appears to be shared by many actors. The right approach is not "find a player-only function", but "use a shared function with a player-only gate".

## Confirmed Player Runtime Identity

One confirmed runtime sample before the update:

```text
actor  = 0x000005E9941B0400
marker = 0x000005E994260A00
```

The exact addresses are session-specific and must not be treated as stable. What matters is the object relationship.

## Confirmed Object Chain

The most useful chain discovered so far is:

```text
actor
 -> +0x40
 -> +0x8
 -> +0xA0
 -> +0x280 / +0x284 / +0x288
```

A concrete runtime sample looked like this:

```text
actor + 0x40                -> 0x4E51A250380
[0x4E51A250380 + 0x8]       -> 0x4E51A0F0200
[0x4E51A0F0200 + 0xA0]      -> 0x4E51A1A0000
[0x4E51A1A0000 + 0x288]     -> one axis
[0x4E51A1A0000 + 0x284]     -> one axis
[0x4E51A1A0000 + 0x280]     -> one axis
```

This chain remains valuable even if the absolute addresses change every launch.

## Important Interpretation

`[actor + 0x40] + 0x8` is the most important intermediate object discovered so far.

It is referred to as `player_core` below:

```text
player_core = [[actor + 0x40] + 0x8]
```

This object is important because:

- It is reachable from the tracked player actor.
- It was also observed directly in the position-related call chain.
- At one confirmed breakpoint, `R14 == player_core`.
- At the outer caller, `R9 == R14 == player_core`.

That gives a very strong player-only gate for any future hook:

```text
current hit belongs to player iff runtime_object == [[tracked_actor + 0x40] + 0x8]
```

## What Was Found To Be Cache

The `+0xA0 -> +0x280/+0x284/+0x288` path does contain valid-looking coordinates, but direct edits there did not move the character.

Observed behavior:

- editing these values had no effect on real position
- the values snapped back
- they were periodically rewritten by a bulk-copy routine

This strongly suggests that:

- `[player_core + 0xA0]` is not the authoritative position object
- it is a cache, mirror, render-side transform block, or another downstream copy

## Confirmed Copy Routine

The following routine was identified as the bulk writer that updates the cache block:

```asm
CrimsonDesert.exe+192F5E0 - 4C 8B 41 08              - mov r8,[rcx+08]
CrimsonDesert.exe+192F5E4 - 48 8D 82 80000000        - lea rax,[rdx+80]
CrimsonDesert.exe+192F5EB - 0F10 02                  - movups xmm0,[rdx]
CrimsonDesert.exe+192F5EE - 41 0F11 00               - movups [r8],xmm0
CrimsonDesert.exe+192F5F2 - 49 8D 88 80000000        - lea rcx,[r8+80]
CrimsonDesert.exe+192F5F9 - 0F10 4A 10               - movups xmm1,[rdx+10]
CrimsonDesert.exe+192F5FD - 41 0F11 48 10            - movups [r8+10],xmm1
...
```

Meaning:

- `r8` is the destination block
- `rdx` is the source block
- the function copies a larger transform/position-related structure, not just one coordinate

One observed `xmm0` sample:

```text
XMM0 = -10103.35, 755.97, 890.79, 0.00
```

This proves the data flowing through this path includes a real `xyzw` vector, but this function is still only a copier, not the original position producer.

## Confirmed Position-Related Call Chain

The useful chain recovered so far:

```text
CrimsonDesert.exe+2366DF9 -> call 21F0900
CrimsonDesert.exe+21F1776 -> call 1C71780
CrimsonDesert.exe+1C7186B -> call 192F5E0
CrimsonDesert.exe+192F5EE -> movups [r8],xmm0
```

Useful partial context:

```asm
CrimsonDesert.exe+2366DEE - 4D 8B CE              - mov r9,r14
CrimsonDesert.exe+2366DF1 - 4C 8D 45 C8           - lea r8,[rbp-38]
CrimsonDesert.exe+2366DF5 - 48 8D 55 C0           - lea rdx,[rbp-40]
CrimsonDesert.exe+2366DF9 - E8 029BE8FF           - call CrimsonDesert.exe+21F0900
```

and:

```asm
CrimsonDesert.exe+21F1770 - 49 8B D5              - mov rdx,r13
CrimsonDesert.exe+21F1773 - 49 8B CE              - mov rcx,r14
CrimsonDesert.exe+21F1776 - E8 0500A8FF           - call CrimsonDesert.exe+1C71780
```

and:

```asm
CrimsonDesert.exe+1C7178B - 48 8B DA              - mov rbx,rdx
...
CrimsonDesert.exe+1C71864 - 48 8B D3              - mov rdx,rbx
CrimsonDesert.exe+1C71867 - 48 8B 4D 38           - mov rcx,[rbp+38]
CrimsonDesert.exe+1C7186B - E8 70DDCBFF           - call CrimsonDesert.exe+192F5E0
```

This is enough to conclude:

- `21F0900` is already much closer to position ownership than the raw cache copy
- the path is shared, not player-exclusive
- player gating should be done with `player_core`, not with function uniqueness

## Key Register Relationship

At the outer caller:

```text
R9  = player_core
R14 = player_core
```

At the `2366DF9 -> call 21F0900` breakpoint:

```text
R14 == [[actor + 0x40] + 0x8]
```

This is one of the strongest findings in the whole research.

It means `21F0900` can be reused later as a player-only discovery point even though it is a shared function.

## Position-Like Data Inside player_core

The memory region behind `player_core` appears to contain multiple repeated copies of similar `xyz` values.

Observed characteristics:

- many repeated triplets with similar coordinate values
- highly regular spacing
- some values appear duplicated several times
- some copies likely represent previous/current/target/render or other state variants

This suggests `player_core` is a position-state container, not just a single transform.

Current best hypothesis:

- one or more groups inside `player_core` are authoritative or near-authoritative
- `[player_core + 0xA0] -> +0x280/+0x284/+0x288` is likely one downstream cache block
- the real source value is probably another repeated group inside `player_core`

## What Not To Do Next

Do not resume from these assumptions:

- do not assume `+0xA0 + 0x280/+284/+288` is the real position
- do not assume the position function must be player-exclusive
- do not continue chasing absolute runtime addresses from old sessions
- do not interpret caller `rsp+offset` and callee `rsp+offset` as the same stack slot

## Recommended Next Step After Player AOB Is Fixed

Once the player actor can be tracked again, resume from the stable object chain instead of from the copy routine.

Recommended process:

1. Recover `tracked_actor`.
2. Recompute `player_core = [[tracked_actor + 0x40] + 0x8]`.
3. Enumerate all repeated `xyz`-like groups inside `player_core`.
4. Label each candidate group by offset.
5. For each group:
   - observe whether it changes first
   - test whether direct edits snap back
   - check whether it is rewritten by the `192F5E0` copy path
6. Keep only candidates that are not just downstream copy targets.

## If A Hook Is Needed Later

If position work eventually moves back to hooks, the safest design is:

- use the repaired player actor hook only to maintain `tracked_actor`
- derive `player_core` from `tracked_actor`
- hook a shared position-related function such as the `21F0900` chain
- gate all logic by:

```text
runtime_object == [[tracked_actor + 0x40] + 0x8]
```

This avoids the mistake of trying to find a "player-only function" in a shared actor update system.

## Newer Findings After Player Hook Repair

After the player hook was repaired again, the actor/object relationship was revalidated with fresh runtime addresses.

A confirmed sample:

```text
actor       = 0x000005CB701B0400
player_core = [[actor + 0x40] + 0x8] = 0x000005CB700F0200
cache_block = [player_core + 0xA0]    = 0x000005CB701A0000
```

The old `+0xA0 -> +0x280/+0x284/+0x288` path still changes with player movement, but it is no longer the main breakthrough.

The more important discovery is a true writable position structure that affects real movement and fall damage.

## Confirmed Authoritative Position Structure

One confirmed runtime sample:

```text
position_owner  = 0x000005CADE311800
position_struct = [position_owner + 0x248] = 0x000005CADF622300
position_vec    = [position_struct + 0x90]
```

Observed field layout:

```text
[position_struct + 0x90] = x
[position_struct + 0x94] = y
[position_struct + 0x98] = z
[position_struct + 0x9C] = w / padding / auxiliary
```

This was verified by editing the live values:

- changing one axis caused the controlled character to move physically
- changing the vertical axis caused the character to launch/fall
- fall damage could occur, confirming this is not just a render-side cache

This makes `position_struct + 0x90` the strongest authoritative-position candidate found so far.

## Physics Move Controller Relationship

The same `position_owner` object also exposed:

```text
[position_owner + 0x148] -> pa::engineScript::CharacterPhysicsMoveControllerScript
```

This strongly suggests the authoritative position is owned by the physics/movement control layer rather than the earlier downstream cache blocks.

## Write Pattern For True Position

Several write instructions were observed hitting the authoritative vector:

```asm
1436ADB8C - 41 0F11 45 00          - movups [r13+00],xmm0
142750253 - 41 0F11 A3 90000000    - movups [r11+00000090],xmm4
14274E0F2 - 0F11 99 90000000       - movups [rcx+00000090],xmm3
14274D650 - 44 0F11 89 90000000    - movups [rcx+00000090],xmm9
14274A5F0 - 41 0F11 A8 90000000    - movups [r8+00000090],xmm5
```

These confirm the true position is handled as a 16-byte SIMD vector field at `+0x90`.

## Important Owner Chain

One especially useful path:

```asm
CrimsonDesert.exe+274E0D5 - mov rcx,[rbx+00000248]
...
CrimsonDesert.exe+274E0F2 - movups [rcx+00000090],xmm3
```

This means:

```text
position_owner  = rbx
position_struct = [rbx + 0x248]
position_vec    = [position_struct + 0x90]
```

One confirmed runtime sample:

```text
rbx = 0x000005CADE311800
[rbx + 0x248] = 0x000005CADF622300
```

The exact bridge from `player_core` to `position_owner` is still incomplete, but the authoritative position side is now much better understood than before.

## New Low-Noise Position Hook Candidate

A later discovery produced a much cleaner update point:

```asm
CrimsonDesert.exe+36ADB87 - 41 0F 58 45 00       - addps xmm0,[r13+00]
CrimsonDesert.exe+36ADB8C - 41 0F 11 45 00       - movups [r13+00],xmm0
```

Observed behavior:

- `r13` points directly at the controlled character's position vector
- the hit switches when the controlled character changes
- it did not produce the same broad multi-actor noise as earlier generic hooks

This makes `CrimsonDesert.exe+36ADB8C` the best current position-hook candidate.

## AOB For The New Position Hook Candidate

Recommended primary pattern:

```text
49 3B F7 0F 8C ?? ?? ?? ?? 0F 28 C6 F3 45 0F 5C C8 41 0F 58 45 00 41 0F 11 45 00 48 8B BB F8 00 00 00 48 63 83 00 01 00 00
```

Results:

- unique match at `CrimsonDesert.exe+36ADB76`
- hook offset `+0x16`
- final hook address `CrimsonDesert.exe+36ADB8C`

Shorter unique fallback:

```text
0F 28 C6 F3 45 0F 5C C8 41 0F 58 45 00 41 0F 11 45 00
```

Results:

- unique match at `CrimsonDesert.exe+36ADB7F`
- hook offset `+0x0D`
- final hook address `CrimsonDesert.exe+36ADB8C`

Longer context fallback:

```text
41 0F 58 45 00 41 0F 11 45 00 48 8B BB F8 00 00 00 48 63 83 00 01 00 00 48 8D 34 C7 48 3B FE 74 20
```

Results:

- unique match at `CrimsonDesert.exe+36ADB87`
- hook offset `+0x05`
- final hook address `CrimsonDesert.exe+36ADB8C`

## Short Summary

The most important findings preserved here are:

- the player position system is centered around `player_core = [[actor + 0x40] + 0x8]`
- the previously edited `+0xA0 -> +0x280/+284/+288` block is a position cache, not the final authoritative source
- `R14` and `R9` in the outer `21F0900` call matched `player_core`
- the correct future direction is to bridge `player_core` to `position_owner`, where the real authoritative position now appears to live at `[position_owner + 0x248] + 0x90`
- `CrimsonDesert.exe+36ADB8C` is currently the cleanest hook candidate for controlled-character position updates
