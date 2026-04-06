# Damage Research

## Background

Current player damage scaling is still a borrowed downstream hook.

It works by scaling a value that has already been produced by the game's damage pipeline.
That means:

- it is good enough for a simple damage multiplier
- it does not necessarily represent the real skill power source
- it may already be after some target/source routing, slot filtering, or other game logic
- it is not the ideal place if we want stable control over attack power, skill power, source, target, hit type, or per-skill tuning

The long-term goal should be to locate a more authoritative upstream damage function that:

- receives both source and target context
- carries actual skill / attack power inputs
- can be modified before the final damage number is committed

## Current Situation

The current runtime structure is split like this:

- `stats hook`: finds player stat entries
- `stat-write hook`: modifies player stat writes at the main stat commit point
- `damage hook`: scales an already-produced damage value using source/target heuristics

This means our stat system understanding is now stronger than our damage pipeline understanding.
For stats, we already know how to trace:

- actor
- marker
- stat root
- stat entries
- final write point

For damage, we still rely on a later-stage value adjustment.

## Why Stat-Write Matters For Damage Research

Even though the stat-write hook itself is not a damage function, it gives us a useful model:

1. The game often has a generic "final commit" layer.
2. The best place to mod behavior is usually not that generic commit layer.
3. The real control point is one or more layers above it, where:
   - semantic context still exists
   - source/target objects are both available
   - skill identifiers or damage categories are still distinguishable

That same reasoning likely applies to damage:

- the current damage hook is probably hitting a generic downstream damage-value path
- the better target is upstream, before the value is reduced to a plain number

## Research Goal

Find a function or a short chain of functions that can be described like:

- input:
  - attacker context
  - defender context
  - attack / skill / move category
  - base power or intermediate power term
- output:
  - pre-commit damage value

Ideal properties:

- stable source and target objects are visible at the same time
- caller/callee relationship is understandable
- skill-specific branching is still present
- downstream generic damage write has not happened yet

## Hypothesis

The real skill-power logic is probably not in the final damage write path.
It is more likely in one of these layers:

1. Attack event assembly
   - creates or fills the combat event object
   - binds source, target, hit metadata, slot, tags, or move category

2. Damage evaluation
   - combines source stats, move coefficients, target modifiers, state flags
   - produces an intermediate or final scalar

3. Damage dispatch / routing
   - pushes the evaluated value into a generic sink
   - this is likely closer to the current borrowed hook

The function we want is probably in layer 1 or 2, not layer 3.

## Known Weaknesses Of The Current Damage Hook

Current hook limitations:

- source/target identification is heuristic
- outgoing vs incoming classification is heuristic
- skill identity is not available
- one multiplier is applied after the game has already done most of its work
- if the game changes downstream damage packing, this hook may stay functional but semantically drift

This is acceptable for a utility multiplier, but not for a clean authoritative mod architecture.

## Suggested Static Analysis Route

Use the current damage hook only as a starting anchor.

### Step 1: Treat Current Damage Hook As A Sink

For the current damage value hook:

- identify exactly what register carries the scaled value
- identify what object pointer is used as event context
- identify the immediate caller chain above this hook site

Questions:

- where was the damage value computed immediately before it arrived here?
- is the event object assembled here or passed in from above?
- is there an earlier function that still has both source and target live?

### Step 2: Walk Upward From The Current Sink

For the current damage callback target:

- disassemble surrounding function
- identify the enclosing function boundary
- inspect callers
- group callers by behavior, not by raw address count

What to look for:

- functions that read multiple pointers from one combat event object
- functions that branch on attack type, slot, hit flags, or IDs
- functions that multiply, divide, clamp, or compare scalar values before the sink

### Step 3: Prefer Functions With Both Source And Target In Scope

A good candidate should show at least two distinct object relationships:

- attacker / instigator / source
- target / victim / receiver

Strong signs:

- two object roots or markers in the same function
- source-side stat reads and target-side stat reads in the same region
- branching based on hit class, guard state, stagger state, element, slot, or move id

### Step 4: Look For Pre-Sink Scalar Math

Good candidates often contain:

- integer or float multiply chains
- coefficient tables
- min/max clamps
- branchy modifier application
- reads from move-data or attack-data structures

If a function mostly just forwards pointers and copies fields, it is probably assembly/dispatch, not evaluation.

### Step 5: Preserve A Two-Layer Model

The likely end state is not one single perfect hook, but two layers:

1. semantic damage-evaluation hook
   - reads source, target, move metadata
   - adjusts power in a controlled way

2. downstream sink hook
   - optional fallback / telemetry only

That gives us:

- better correctness
- easier debugging
- less dependence on fragile post-hoc scaling

## Concrete Next Actions

After compacting context, resume from this sequence:

1. Open the current borrowed damage hook site in IDA.
2. Identify the exact enclosing function for both:
   - damage slot hook
   - damage value hook
3. Generate a bounded caller tree upward from the damage value site.
4. Rank candidate callers by:
   - both source and target in scope
   - scalar math density
   - branching on attack metadata
5. For the top candidates:
   - decompile
   - annotate live registers / stack locals
   - note where source and target enter the function
   - note where the damage scalar is formed

## Candidate Evaluation Checklist

When judging a candidate function, answer:

- Does it see both source and target?
- Does it still know what move / attack / slot this is?
- Does it perform real damage math?
- Is it upstream of the current sink?
- Can we modify power here without breaking unrelated damage routing?

If the answer is "yes" to most of these, it is a better long-term hook than the current borrowed mod logic.

## Practical Research Note

Do not confuse:

- the final damage write
- the final damage event dispatch
- the actual damage power evaluation

Those are often separate layers.

The current mod most likely hooks layer 3.
The next target should be layer 2.

## Deliverable For The Next Session

The next research session should aim to produce:

- one shortlisted upstream damage-evaluation function
- one fallback downstream sink function
- a written register/argument map for both
- a recommendation on which one should become the future authoritative damage mod point

## Current Static Conclusion

Static analysis in IDA narrowed the practical damage chain to:

- `sub_142181F90`
- `sub_14237E590`
- `sub_1412D9760`
- `sub_1412DCBC0`

The previously borrowed damage hook site is **not** the preferred long-term target.

### What The Current Borrowed Hook Really Is

The current hook anchored by:

- damage slot access
- damage value access

lands inside:

- `sub_14C707540`

This function is not a clean skill-damage evaluator.
It behaves like a grouped status-value combiner:

- maps a `statusId` through `statusgroupinfo`
- reads indexed values from internal tables
- adds incoming/context values
- clamps against a max entry
- writes the final combined result

This makes it useful as:

- a downstream fallback
- a telemetry anchor
- a caller-chain starting point

But it is **not** a good authoritative mod point for future damage work.

## Why The Existing Mod Damage Scaling Is Unstable

The current repo implementation in `TryScalePlayerDamage(...)` is still heuristics-driven.

It depends on:

- a late-stage already-produced number
- recursive participant relationship guesses
- cached previously-seen participants
- outgoing/incoming inference instead of explicit source/target semantics

That means:

- it may keep working while semantically drifting
- it has no direct HP/status identity guarantee
- it has no reliable source/target ownership model
- it has no skill ID / move category / hit-type awareness

So the current scaling path should be treated as:

- temporary utility behavior
- fallback compatibility path

and not the long-term architecture.

## New Primary Research Targets

The next session should focus **only** on these two functions:

1. `sub_14237E590`
2. `sub_1412D9760`

## Semantic Map

To reduce future context rebuild cost, use this simplified semantic model first.

This is not yet a final type-accurate reconstruction.
It is a working map for reasoning about the chain.

### High-Level Role Of Each Function

- `sub_142181F90`
  - likely deserializes or unpacks an incoming combat/effect event payload
  - forwards parsed fields into the real processing function

- `sub_14237E590`
  - likely the high-level combat/status effect handler for one event
  - resolves which status is being affected
  - branches on event mode / flags
  - computes or selects the delta path
  - decides whether to route into direct HP/status application helpers

- `sub_1412D9760`
  - likely the status-change execution function
  - receives a resolved status ID plus a delta/context package
  - performs lower-level status update preparation
  - passes the change into the actual application/commit helpers

- `sub_1412DCDA0`
  - likely prepares aggregation / bookkeeping for the change
  - appears to build or update per-status historical or queued records

- `sub_1412DCBC0`
  - likely commits a concrete status delta into the target status entry
  - updates counters / accumulated values / current write-time state

- `sub_14C707540`
  - grouped status-value combiner
  - not the semantic damage source
  - keep only as downstream fallback / telemetry anchor

### Practical Semantic Summary

If the current interpretation is roughly correct, then the chain should be thought of as:

1. `sub_142181F90`
   - parse one incoming gameplay/combat packet or event payload

2. `sub_14237E590`
   - decide which status is being changed
   - identify whether this is HP-like handling
   - determine the change amount and route
   - hold the best chance of still knowing source/instigator context

3. `sub_1412D9760`
   - execute the chosen status delta path
   - hand off to lower-level change preparation and commit helpers

4. `sub_1412DCBC0`
   - apply the delta to the target status machinery

This is the main reason `sub_14237E590` is the preferred future hook target:

- it is still semantic
- it appears upstream of the final status application
- it likely has better source/target context than the lower layers

And this is why `sub_1412D9760` remains the best backup candidate:

- it is close to the actual HP delta application
- it is lower-risk if semantic reconstruction of the upper layer is incomplete

### Working Interpretation Of Important Values

These are not yet final names, but they are the current best working assumptions:

- resolved `statusId`
  - should match the repo's known IDs
  - `0 = Health`
  - `17 = Stamina`
  - `18 = Spirit`

- target status owner/context
  - likely the object eventually passed into `sub_1412D9760(...)`
  - likely the object whose status entry gets modified downstream

- source / instigator candidate
  - likely the external context object passed from `sub_14237E590` into `sub_1412D9760(...)`
  - this is the main thing to confirm next session

- HP delta candidate
  - likely the signed value prepared in `sub_14237E590` before the call into `sub_1412D9760(...)`
  - modifying this value is the likely route for authoritative source-based damage scaling

### What Future Readers Should Not Assume

Do not assume:

- `sub_14C707540` is the damage function
- every HP change is combat damage
- every path into `sub_1412D9760` is hostile damage
- the current repo's downstream multiplier proves source/target ownership

Do assume:

- status ID gating is stronger than downstream value heuristics
- HP-only gating should be the first semantic filter
- source identification should happen as high as possible, ideally in `sub_14237E590`

### Why `sub_14237E590` Matters

This function currently looks like the best high-level candidate for a future authoritative damage hook.

It appears to:

- receive parsed event/effect inputs from `sub_142181F90`
- resolve the target status ID
- branch based on event flags
- special-case HP-like handling
- prepare the delta that will be applied downstream
- call `sub_1412D9760(...)` with a richer semantic context than the current borrowed hook has

Most importantly, it appears to carry:

- target status identity
- event flags / mode flags
- an external context object that may represent source / instigator information
- the delta value before final status application

### Why `sub_1412D9760` Matters

This function looks like the status-change execution layer.

It appears to:

- read current status values
- prepare auxiliary delta / history structures
- call `sub_1412DCDA0(...)`
- call `sub_1412DCBC0(...)`
- submit the actual status change

This makes it a strong second-choice hook point:

- lower level than `sub_14237E590`
- less semantic context
- but likely more stable for directly modifying the final HP delta

## HP Identity Working Assumption

The repo already documents the status IDs:

- `0 = Health`
- `17 = Stamina`
- `18 = Spirit`

Current static evidence strongly suggests the new chain still operates on that same status ID model.

So the working assumption for the next session should be:

- if the resolved status ID is `0`, we are on the HP path

This is much stronger than the current downstream damage heuristic because it gives a direct semantic gate:

- HP-only processing
- no stamina/spirit accidental overlap

## Questions To Answer Next Session

For `sub_14237E590`:

- which argument or local represents `source` / instigator?
- which object is the target status owner?
- which local is the final HP delta before application?
- which flags distinguish damage-like behavior from other HP-affecting events?

For `sub_1412D9760`:

- which argument is the status ID?
- which argument is the delta to be applied?
- where does HP-only processing diverge from other statuses?
- is this function suitable as a stable write-time delta hook if `sub_14237E590` is too broad?

## Next Session Scope Lock

Do not broaden the next session back to generic downstream damage sinks.

Only study:

- `sub_14237E590`
- `sub_1412D9760`

Use all other functions only as supporting helpers when necessary, including:

- `sub_142181F90`
- `sub_1412DCBC0`
- `sub_1412DCDA0`
- `sub_1412D7C10`
- `sub_1412D7DB0`

But the primary deliverable should remain:

- source/target identification at `sub_14237E590`
- HP delta path confirmation through `sub_1412D9760`
- recommendation on which of the two should become the future authoritative damage mod point

## Damage-Scaling Focused Static Addendum

This addendum intentionally ignores generic status-system questions that do not
help player damage scaling.

Only the following questions matter:

- where HP damage delta is formed
- where HP damage delta is committed
- where source context is still attached
- which branch is likely damage-like instead of heal/cost/other HP change

## Refined Reading Of `sub_14237E590`

`sub_14237E590` still looks like the best semantic entry for authoritative
damage scaling.

### Stable Parameter Mapping

Current best static mapping:

- `a2`
  - event / effect context wrapper
  - `v85 = *a2` is forwarded downstream as the external context object
  - this is still the strongest `source / instigator candidate`

- `a3`
  - required event payload identifier / handle
  - if zero, the function exits early with an error path
  - not useful as the damage scalar

- `a4`
  - incoming status token or attribute selector
  - first resolved through `sub_1402DAFD0(...)`
  - if lookup fails, function falls back to string matching

- `a5`
  - mode selector
  - branches the function into three distinct status-change paths
  - this is one of the most important filters for separating damage-like HP
    changes from other HP writes

- `a6`
  - signed integer magnitude input
  - immediately converted to `1000 * *a6`
  - acts like a percent / magnitude term that participates in delta formation

- `a7`
  - alternate formula flag
  - when set, the function derives a delta from current/max-like values instead
    of using the simpler clamp path

- `a8`
  - post-apply follow-up flag
  - only matters on the `*a5 == 1` path after `sub_1412D8DC0(...)`
  - currently lower value for direct damage scaling

### HP Identity Resolution

The function resolves `v36` as the effective status ID.

Resolution order:

1. map `*a4` through `sub_1402DAFD0(...)`
2. if that fails or yields `0xFFFF`, fall back to string matching
3. if still unresolved, exit without applying a status delta

This is important because it strongly supports the working assumption that the
resolved ID still belongs to the same status model used by the repo:

- `0 = Health`
- `17 = Stamina`
- `18 = Spirit`

For damage scaling, that means the first hard gate should remain:

- `statusId == 0`

### Current / Cap Reads Before Delta Formation

Before any apply helper is called, the function does two reads against the
target-side status owner at `*(_QWORD **)(v43 + 24)`:

- `sub_1412D7C10(...)`
  - reads the current value for `v36`

- `sub_1412D7DB0(...)`
  - reads another value for `v36`
  - current evidence still fits a max/cap/related limit role

This matters because the damage-like branch can form delta from these two reads
instead of directly trusting the incoming magnitude.

### Most Important Branch For Damage Work

The most promising branch is:

- `*a5 == 2`

Inside that branch:

- the target status owner is `*(_QWORD **)(v43 + 24)`
- a special per-status lookup may produce `v57`
- final candidate delta is stored in `v60`
- the function then calls:

`sub_1412D9760(*(_QWORD **)(v43 + 24), 1u, v36, v84, v60, (__int64)v85, 0x10u, 0xAu, 0xFFFF, &v75);`

This is currently the strongest HP-damage candidate path because:

- it uses the execution-layer helper instead of the more generic sibling path
- it carries both resolved `statusId` and external context candidate `v85`
- it computes a signed delta immediately before the call

### Working Meaning Of `v60`

`v60` is now the best static candidate for the final HP damage delta prepared
by `sub_14237E590`.

Observed behavior:

- if `*a7 == 0`
  - `v60` comes from the `sub_1412D7DB0(...)` result, clamped by `1000 * *a6`

- if `*a7 != 0`
  - `v60` is derived from a more complex formula that mixes:
    - a per-status table/result (`v57`)
    - the capped `a6`-derived percent term
    - the `sub_1412D7DB0(...)` result

For our purposes, the exact game formula is less important than this fact:

- `v60` is the last clearly isolated signed delta value before the call into
  `sub_1412D9760(...)`

That makes it the best upstream hook mutation target discovered so far.

### Current Meaning Of `v85`

`v85 = *a2` is still the best static `source / instigator candidate`.

Evidence:

- it originates from the high-level event context, not the target status owner
- it is passed unchanged into `sub_1412D9760(...)` as the sixth argument
- the downstream execution layer only uses it for event/report metadata, not for
  the actual arithmetic

Current conservative interpretation:

- `v85` is not proven to be the raw attacker actor pointer
- but it is very likely an event-context object that still preserves attacker
  identity or attacker-side routing metadata

This is the main remaining uncertainty that may still require minimal dynamic
confirmation later.

## Refined Reading Of `sub_1412D9760`

`sub_1412D9760` is now better understood as the status-delta execution layer,
not the semantic damage calculator.

### Parameter Mapping

Current best mapping:

- `a1`
  - target status owner / target status component

- `a2`
  - apply mode / bookkeeping mode
  - influences `sub_1412DCDA0(...)`
  - seen as both `0` and `1` in callers

- `a3`
  - resolved status ID
  - this is now strongly confirmed

- `a4`
  - timestamp / apply tick / world-time-like marker
  - forwarded into commit helpers and status-side bookkeeping

- `a5`
  - signed delta being applied
  - this is the core write-time value for HP damage scaling

- `a6`
  - optional external event context
  - not used for delta math
  - only read later as `*(a6 + 96)` for event/report payload construction

- `a7`
  - event/category byte used by a later virtual callback

- `a8`
  - secondary event/category byte copied into per-status event state

- `a9`
  - optional secondary status ID for reference reads

- `a10`
  - optional status ID pointer used for auxiliary readback

### Strongest Confirmed Fact About This Function

The commit helper call is:

`sub_1412DCBC0(a1, a3, a4, prepared_delta, &event_info);`

And inside `sub_1412DCBC0(...)`:

- `a2` selects the status entry
- `a4` is added directly into the status entry totals
- `*(_QWORD *)(v9 + 16) += a4`
- `*(_QWORD *)(v9 + 136) += a4`

So for practical modding purposes:

- `sub_1412D9760.a5` is the effective signed delta that reaches the status entry

This is the most important execution-layer conclusion of the current session.

### Why `a6` Is Probably Not The Delta Source

`a6` is only consumed after the commit succeeds:

- if non-null, the function reads `*(_DWORD *)(a6 + 96)`
- that value is then attached to an event/callback payload

So `a6` behaves like:

- source-side event metadata
- instigator-side reporting context
- or some packaged combat context

It does **not** behave like the arithmetic input that determines HP loss.

That makes `sub_1412D9760` a viable backup hook point for delta scaling, but a
weaker point for reliable attacker identification.

### HP-Specific Special Handling

The function has extra logic when `a3` matches two globally selected status IDs.
This looks like a built-in paired vital-status path rather than generic status
handling.

For our current goal, the practical takeaway is:

- `sub_1412D9760` is not HP-only
- but it does contain special handling for a small vital-status set
- we should still gate strictly on `statusId == 0` if hooking here

## Hook Decision Implication

If the goal is **only** damage scaling, the current ranking is:

1. `sub_14237E590`
   - best chance to see:
     - HP identity
     - delta before commit
     - source/event context
     - branch mode that may separate damage from other HP changes

2. `sub_1412D9760`
   - best backup if we only need a stable write-time signed delta
   - weaker source semantics
   - stronger confidence that `a5` is the actual applied delta

3. `sub_14C707540`
   - keep only as downstream fallback / telemetry anchor

## Narrow Remaining Unknowns

Static analysis reduced the remaining unknowns to two small questions:

- whether `v85` / `sub_1412D9760.a6` is truly attacker/instigator identity, or
  only a combat event wrapper carrying that identity indirectly
- whether `*a5 == 2` in `sub_14237E590` is strictly the hostile-damage branch, or
  only the main HP-delta execution branch that may still include non-hostile HP
  writes

These are now narrow enough that, if static analysis stalls later, dynamic
verification should only target those two points.

## Additional Static Round: Branch Semantics Tightening

Another static pass refined how `sub_14237E590` should be read for
damage-scaling work.

### Fallback Name Table Is Not Generic

The string fallback table used when direct status resolution fails is:

- `Hp`
- `Fatal`
- `KnockOut`

This is important because it means the function is not reasoning about a broad
generic status set at that point.

Instead, it is already narrowed to a small vital-status cluster centered on:

- health
- lethal / fatal state
- knockout / downed state

So even before final branch interpretation is perfect, this function is clearly
much closer to life-and-damage semantics than to a general-purpose status write.

### Revised Reading Of `*a5` Mode Branches

The `*a5` mode byte now looks like the main dispatcher inside
`sub_14237E590`.

Current branch map:

- `*a5 == 2`
  - computes a signed delta in `v60`
  - calls `sub_1412D9760(...)` directly
  - strongest direct execution-layer delta path

- `*a5 == 1`
  - first routes through `sub_1412D8DC0(...)`
  - only if `*a8` is set and the post-step total stays positive does it continue
    into `sub_1412D8340(...)`
  - looks less like a pure hostile-damage path and more like a conditional
    status-processing route

- `*a5 == 0`
  - computes a signed delta in `v68`
  - calls `sub_1412D8340(...)` directly
  - cannot be dismissed as “generic only”, because for HP it enters a dedicated
    helper

### Why `sub_1412D8340` Matters More Than Previously Thought

`sub_1412D8340(...)` special-cases the globally selected primary vital status.
Current evidence strongly suggests that this is HP:

- if `a2` matches the global status at offset `+152`
  - it calls `sub_1412D6640(...)`
- otherwise
  - it falls back to `sub_1412D84A0(...)`

That changes the interpretation materially:

- for HP, `sub_1412D8340` is not just a generic sibling helper
- it is a dispatcher into an HP-specific path

### Why `sub_1412D6640` Looks Damage-Relevant

Even though it was not promoted to a primary research target, reading
`sub_1412D6640(...)` gives useful evidence about the parent branch semantics.

For HP-like status it receives:

- timestamp / apply marker
- signed delta
- external context pointer
- event/category bytes

Important observations:

- the forwarded external context is the same source candidate previously tracked
  from `sub_14237E590`
- negative deltas receive explicit handling
- the function reads `*(source_ctx + 96)` when source context exists
- it performs event/report dispatch that clearly depends on source-presence and
  damage-like negative application

So `sub_1412D6640(...)` behaves much more like:

- HP-specific change application with source-aware damage reporting

and much less like:

- a blind generic status setter

### Practical Consequence For Branch Ranking

This means the previous interpretation needs one correction.

It is still true that:

- `*a5 == 2` is the clearest path if the goal is “find the last signed delta
  before commit into `sub_1412D9760`”

But it is no longer safe to assume that:

- `*a5 == 2` is automatically the most damage-semantic branch

Because:

- `*a5 == 0` routes straight into `sub_1412D8340(...)`
- HP inside `sub_1412D8340(...)` routes into `sub_1412D6640(...)`
- `sub_1412D6640(...)` has stronger negative-delta and source-context behavior
  than the plain `sub_1412D9760(...)` execution layer

### Updated Working Interpretation

The current best working interpretation is:

- `sub_14237E590`
  - still the main semantic dispatcher we should hook from above

- `*a5 == 2`
  - strongest direct-to-commit delta path
  - best if we want write-time scaling with minimal extra reconstruction

- `*a5 == 0`
  - strongest HP-specific helper path
  - may actually be the more damage-semantic route because it reaches the
    dedicated HP helper `sub_1412D6640(...)`

- `*a5 == 1`
  - currently the weakest candidate for clean hostile-damage semantics
  - looks more conditional / transitional than primary attack damage

### Updated Hooking Implication

For damage scaling only, the decision table is now slightly more nuanced:

1. `sub_14237E590`
   - still the preferred top-level hook search point
   - best place to distinguish branch mode and inspect source candidate

2. `sub_1412D9760`
   - still the best backup if we only need a stable signed delta commit point
   - especially relevant for the `*a5 == 2` path

3. `sub_1412D6640`
   - not promoted to primary scope yet
   - but now clearly important as supporting evidence
   - if future static or dynamic work proves `*a5 == 0` is the real hostile
     damage branch, this helper may become a better HP-damage hook than
     `sub_1412D9760`

### What This Changes For The Next Session

The scope should still stay narrow, but the question is now sharper:

- is `*a5 == 0` the real source-aware HP damage branch?
- or is `*a5 == 2` still the more authoritative attack-delta branch despite
  weaker visible source semantics downstream?

That is now the main unresolved branch-selection problem for damage scaling.

## Additional Static Round: Evidence Leaning Toward `*a5 == 0`

Another pass produced stronger evidence that the `*a5 == 0` route is more
damage-like than the `*a5 == 2` route when the goal is HP damage scaling.

### Serialized Payload Layout Is Stable

`sub_142181F90` deserializes the payload in a fixed order before calling
`sub_14237E590(...)`:

- `a3` <- 4-byte field
- `a4` <- 4-byte field
- `a5` <- 1-byte field
- `a6` <- 4-byte field
- `a7` <- 1-byte field
- `a8` <- 1-byte field

This matters because `a5` is not a derived local classification.
It is an actual protocol/event-mode byte coming from the serialized gameplay
payload.

So branch analysis on `*a5` is meaningful at the gameplay-event level.

### What `sub_1412D6640` Actually Looks Like

For HP-like status, `sub_1412D8340(...)` dispatches into `sub_1412D6640(...)`.

Static evidence from `sub_1412D6640(...)` now supports the following reading:

- it is highly specialized for HP / vital-state handling
- it treats negative deltas as a first-class case
- it reads source/event context from the external context object when present
- it emits multiple game-event notifications tied to the HP change
- it contains explicit logic around fatal / downed / follow-up state transitions

This is notably different from the more generic execution-layer feeling of
`sub_1412D9760(...)`.

### Negative Delta Is The Center Of Gravity In `sub_1412D6640`

Several strong signs point toward a hostile-damage interpretation:

- negative `a3` is handled specially
- when the delta is negative and source context exists, the function follows an
  extra source-aware reporting path
- it reads `*(source_ctx + 96)` or equivalent source-linked identifiers for
  event/report dispatch
- it drives additional HP-adjacent event flow after applying the negative change

That pattern matches “damage application with attacker/source metadata” much
better than it matches a generic setter or a passive stat correction helper.

### Independent Caller Evidence: `sub_1412C5660`

The strongest new clue is the other caller of `sub_1412D6640(...)`:

- `sub_1412C5660`

What this caller does:

- resolves the global primary vital status (`+152`, very likely HP)
- reads current HP through `sub_1412D7C10(...)`
- negates it
- calls:

`sub_1412D6640(target_status_owner, timestamp, -current_hp, external_ctx, 0, 0, 1, a4, a5, 0, &tmp);`

This call shape looks like:

- consume all remaining HP
- force an HP-loss / fatal-style application
- run the HP-specific event machinery

That is exactly the kind of evidence that pushes `sub_1412D6640(...)` toward
“real damage / lethal HP-change helper” rather than “generic status utility”.

### Updated Interpretation Of The Two Main Branches

At this point, the most defensible branch reading is:

- `*a5 == 0`
  - routes into `sub_1412D8340(...)`
  - HP inside that helper routes into `sub_1412D6640(...)`
  - strongest source-aware and negative-delta-aware HP path
  - currently the better candidate for real hostile damage semantics

- `*a5 == 2`
  - routes directly into `sub_1412D9760(...)`
  - still a clean signed-delta execution path
  - probably better for “stable commit-level delta modification”
  - but now looks less semantically rich than the HP-specific helper path

### Why `*a5 == 2` Still Matters

This does not make `*a5 == 2` useless.

It still has two real advantages:

- the signed delta is isolated very clearly just before commit
- the downstream execution layer is easier to reason about for direct scaling

So `*a5 == 2` remains the best backup if we optimize for:

- simple implementation
- stable write-time scaling
- minimal semantic reconstruction

But it is no longer the best guess for the most authoritative damage branch.

### Updated Working Ranking

For the specific goal of scaling player-caused HP damage:

1. `sub_14237E590`, focusing first on the `*a5 == 0` branch
   - best semantic entry point currently known

2. `sub_1412D6640`
   - strongest HP-specific and source-aware helper
   - now the leading supporting candidate if we need to hook below
     `sub_14237E590`

3. `sub_1412D9760`
   - still the best generic execution-layer fallback
   - especially useful if we only need a reliable signed delta commit path

### Current Best Branch-Selection Hypothesis

The current best hypothesis is now:

- `*a5 == 0` is more likely to represent the real hostile HP-damage path
- `*a5 == 2` is more likely to represent a cleaner but less semantic direct
  status-delta execution path

This is still a hypothesis, but it is much stronger than before.

### Remaining Narrow Uncertainty

Only one branch-selection ambiguity still really matters:

- whether `*a5 == 0` is the main ordinary combat-damage route
- or whether it is a narrower HP-special route used only by a subset of
  damage/fatal events, while `*a5 == 2` covers the more common direct damage case

If future static work cannot settle that, dynamic validation should be aimed at
that exact question and nothing broader.

## Dynamic Addendum: Latest `sub_1412D6640` Evidence

Recent CE sampling shifted the dynamic center of gravity away from
`sub_14237E590`.

### What The Filtered Sampler Actually Saw

The dispatch-window sampler was set to observe:

- `sub_14237E590`
- the direct `sub_1412D8340` call site inside it
- `sub_1412D9760`
- `sub_1412D6640`

But real runtime samples did **not** show the expected upper-layer sequence.
Instead, the useful combat-looking samples appeared as direct `orphan-d6640`
hits.

That means:

- no current dynamic evidence proves that `sub_14237E590` is the observable
  main combat entry in the tested scenario
- the strongest live signal currently comes from `sub_1412D6640`

### The Most Important Runtime Sample So Far

The clearest negative-delta sample seen so far was:

- `ret = 0x1412C582E`
- `delta = -50000`
- `target = 0x2AEC7B64B00`
- `sourceCtx = 0x2AE720F0200`
- `a7 = 1`
- `a8 = 8`
- `a9 = 10`

This is important for two reasons:

1. It is a negative HP-like application entering `sub_1412D6640(...)`.
2. Its caller is **not** the usual `sub_1412D8340 -> sub_1412D6640` return site
   at `0x1412D8407`.

Static analysis already identified `0x1412C582E` as the post-call site inside
`sub_1412C5660(...)`, which performs:

- read current HP
- negate the value
- call `sub_1412D6640(...)`

So the latest dynamic evidence is consistent with:

- `sub_1412D6640(...)` being a real vital/HP execution helper
- negative delta paths carrying more combat/lethal semantics than the generic
  background positive traffic

### Background Traffic Pattern

The current log also shows frequent positive-delta traffic such as:

- `ret = 0x1412D8407`
- `delta = 2000`, `a8 = 14`, `a9 = 10`, `outPtr = 0x1412D8101`
- `delta = 200`, `a8 = 8`, `a9 = 173`, `outPtr = 0x100000000`

These repeating positive signatures appear across multiple targets and are
currently best interpreted as non-combat or passive vital traffic, for example:

- regeneration
- accessory / orb triggered recovery
- background vital maintenance

This matters because a raw `sub_1412D6640` hook must not assume:

- every call is outgoing player damage
- every positive HP delta is relevant to damage scaling

### Current Dynamic Conclusion

The practical dynamic ranking is now:

1. `sub_1412D6640`
   - best live observation point
   - most useful current target for separating combat-looking negative deltas
     from passive positive traffic

2. `sub_1412D8340`
   - useful only as a caller discriminator for the common HP helper route

3. `sub_14237E590`
   - still interesting statically
   - but currently **not** supported as the main dynamic investigation path

### What Needs To Be Verified Next

The next dynamic pass should stay narrow and answer only these questions:

- whether `sourceCtx` can reliably distinguish:
  - player -> enemy
  - enemy -> player
- whether negative `sub_1412D6640(...)` samples split into stable caller buckets
  that match:
  - ordinary combat damage
  - lethal / consume-all-HP transitions
- whether the repeating positive signatures should simply be ignored for the
  damage-scaling mod architecture

### Updated Research Direction

For the next session, dynamic work should **not** keep chasing
`sub_14237E590` first.

Instead:

- treat `sub_1412D6640` as the active dynamic mainline
- use caller / return-address bucketing and `delta < 0` gating
- only go back upward if a stable combat-negative bucket has been isolated

## Dynamic Addendum: `sub_1412D8340` Split The Damage Branches Cleanly

After narrowing the sampler from `sub_1412D6640(...)` to
`sub_1412D8340(...)`, the runtime picture became much clearer.

### Why `sub_1412D8340` Was The Better Probe

`sub_1412D6640(...)` was too late in the HP pipeline:

- many unrelated HP/vital events collapse into it
- all `sub_1412D8340 -> sub_1412D6640` traffic shares the same return address
- combat damage and passive recovery are hard to separate there

Sampling one layer higher at `sub_1412D8340(...)` preserved the caller split.

### HP-Only Caller Buckets

For `statusId == 0`, the current dynamic samples consistently split into three
stable buckets:

1. `ret = 0x141A50BB0`
   - negative deltas
   - `a8 = 8`
   - `a9 = 10`
   - `a10 = 0`
   - `outPtr = 0x131B29CD0`
   - this is the strongest currently-known real combat-damage bucket

2. `ret = 0x141A53DA4`
   - repeated small positive deltas such as `+200`
   - `a8 = 8`
   - `a9 = 173`
   - `a10 = 0`
   - current best interpretation: passive recovery / background HP maintenance

3. `ret = 0x14213B8C1`
   - repeated positive and zero traffic such as `+2000`, `+400`, `0`
   - `a8 = 14`
   - `a9 = 10`
   - `a10 = 1`
   - current best interpretation: non-combat recovery / grouped HP processing

### Current Combat-Damage Bucket

The best current combat-damage signature is now:

```text
ret   == 0x141A50BB0
delta < 0
a8    == 8
a9    == 10
a10   == 0
```

This bucket is seen for both:

- enemy -> player
- player -> enemy

So `ret = 0x141A50BB0` should be treated as:

- the shared real battle-damage submission branch

and **not** as an incoming-only or outgoing-only branch.

## Dynamic Addendum: Source Context Chain Is Now Structurally Confirmed

The earlier question was whether `sourceCtx` was only a loose event wrapper or
whether it could be tied back to a concrete actor identity.

That relation is now directly confirmed in live memory.

### Confirmed Pointer Chain

For the player-side runtime sample:

- player actor:
  - `0x2AE721B0400`
- player marker:
  - `0x2AE72260A00`
- player-side combat source context:
  - `0x2AE720F0200`

Direct memory reads show:

```text
*(0x2AE720F0200 + 0x68) == 0x2AE721B0400
*(0x2AE721B0400 + 0x20) == 0x2AE72260A00
```

So the current working chain is:

```text
sourceCtx
  +0x68 -> actor
actor
  +0x20 -> marker
```

This is strong enough to use in future hook logic.

### Meaning Of `sourceCtx`

`sourceCtx` should no longer be treated as:

- raw actor pointer
- raw marker pointer

Instead it is better described as:

- source-side combat/status context object

and its actor owner can be recovered by:

```text
attacker_actor = *(sourceCtx + 0x68)
```

## Dynamic Addendum: Player Target Owner Is Reachable From Marker

Another live-memory relation is now confirmed on the player side.

Reading the player marker shows:

```text
*(player_marker + 0x08) == player-side sourceCtx
*(player_marker + 0x18) == player target owner
```

For the current session:

```text
player_marker           = 0x2AE72260A00
*(marker + 0x08)        = 0x2AE720F0200
*(marker + 0x18)        = 0x2AE720E03C0
```

This `0x2AE720E03C0` value is **not** currently interpreted as:

- the raw HP numeric entry

It is better treated as:

- player HP/vital target owner
- player damage target context
- the `a1`-like target-side owner passed into `sub_1412D8340(...)`

### Why It Should Not Be Called The HP Entry

Current evidence suggests `sub_1412D8340(...)` still receives:

- target owner / target status component
- explicit `statusId`

If the `target` argument were already the concrete HP entry, the additional
`statusId == 0` argument would be semantically redundant.

So the safer wording is:

- `player_target_owner = *(player_marker + 0x18)`

and not:

- `player_hp_entry`

## Practical Modding Implication

With the above chains, incoming and outgoing combat damage can now be described
with much lower ambiguity.

### Incoming Damage To Player

The practical current rule is:

```text
ret   == 0x141A50BB0
delta < 0
target == *(player_marker + 0x18)
```

This is currently the strongest runtime rule for:

- enemy -> player damage

and is more robust than trying to enumerate all enemy-side `sourceCtx` values.

### Outgoing Damage From Player

The practical current rule is:

```text
ret == 0x141A50BB0
delta < 0
*(sourceCtx + 0x68) == player_actor
```

This is currently the strongest runtime rule for:

- player -> enemy damage

### Why Enemy Hits May Show Multiple `sourceCtx` Values

Multiple enemy-side `sourceCtx` values are expected.

They should not be interpreted as a contradiction.

The reason is:

- `sourceCtx` is a source-side context object
- different attacking enemies or attacking entities can have different
  `sourceCtx` objects
- each `sourceCtx` can still be resolved back to its actor by:
  - `*(sourceCtx + 0x68)`

So multiple enemy-side addresses simply mean:

- multiple source-side owners / actors are participating

### Current Scope Recommendation

For the specific mod goal of damage scaling, the current best practical scope is
now narrow enough:

1. Hook the branch that corresponds to the `ret = 0x141A50BB0` battle-damage
   bucket.
2. Gate outgoing player damage with:
   - `*(sourceCtx + 0x68) == player_actor`
3. Gate incoming player damage reduction with:
   - `target == *(player_marker + 0x18)`

This is now much stronger than the old heuristic downstream damage logic and is
likely already sufficient for a stable player damage-scaling implementation,
even before every upper-layer semantic detail is fully named.

## Session Closeout: Stable Current-State Summary

This section records the current practical state at the end of the session so
the next session can resume without rebuilding context from scratch.

### Current Runtime Structure That Matters

The current best low-risk damage-scaling model is:

```text
combat damage bucket:
  sub_1412D8340(...)
  statusId == 0
  delta < 0
  caller return == 0x141A50BB0
```

Within that bucket:

- incoming damage to player is best identified by:
  - `target == player_target_owner`

- outgoing damage from player is best identified by:
  - `*(sourceCtx + 0x68) == player_actor`

### Confirmed Pointer Relations

The currently confirmed pointer relations are:

```text
sourceCtx + 0x68 -> actor
actor     + 0x20 -> marker
marker    + 0x08 -> sourceCtx-like player-side context
marker    + 0x18 -> player_target_owner
```

These relations are strong enough to support future implementation work.

### Important Naming Clarification

Do **not** call `player_target_owner`:

- HP entry
- health value entry

Current evidence supports calling it:

- target owner
- vital owner
- player damage target context

It is the target-side owner object used in the HP damage path, not the final
numeric stat entry itself.

### CE AA Stability Lessons

This session exposed an important Auto Assembler pitfall:

- `nop 11` in CE AA is interpreted as hexadecimal length
- so it means `0x11` bytes, not decimal 11 bytes

For the `sub_1412D8340` entry trampoline, the correct padding after a 5-byte
`jmp` over a 16-byte overwritten prologue is:

```text
nop B
```

and **not**:

```text
nop 11
```

Using `nop 11` overran the intended overwrite window and corrupted the function
prologue.

### Current CE Validation Assets

Current CE files of interest:

- [sample_damage_d8340.lua](/D:/Workspace/cpp/CrimsonDesertASI/stamina-spirit/ce/sample_damage_d8340.lua)
  - HP-only branch sampler at `sub_1412D8340`

- [scale_damage_x16_gate_1412D8340.aa](/D:/Workspace/cpp/CrimsonDesertASI/stamina-spirit/ce/scale_damage_x16_gate_1412D8340.aa)
  - session-local validation patch
  - current state: machine-code injection shape is correct

- [hook_stub_1412D8340.aa](/D:/Workspace/cpp/CrimsonDesertASI/stamina-spirit/ce/hook_stub_1412D8340.aa)
  - minimal trampoline verifier for the same entry

### Scope Recommendation For The Next Session

The next session should not reopen the broad question of generic damage
research.

Instead it should continue from the narrowed practical state:

1. Preserve the confirmed `sub_1412D8340` battle bucket model.
2. Keep the player identification rules:
   - incoming:
     - `target == player_target_owner`
   - outgoing:
     - `*(sourceCtx + 0x68) == player_actor`
3. Investigate the next requested target:
   - whether dragon damage can be amplified using the same bucket and
     source-side actor/context logic.
