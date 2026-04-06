# Stamina Research

## Purpose

This document records the current reverse-engineering status for stamina behavior in `Crimson Desert`.

The main goal is not to fully decompile the entire stamina system. The practical goal is to locate a stable upstream place where stamina consumption can be reduced without the game immediately recalculating and pulling the value back.

## Current Working Model

The current stamina pipeline is no longer treated as a single write path.

The useful mental model is:

```text
rule records
 -> sub_1412DCDA0
 -> sub_1412DCBC0
 -> sub_1412DD790
 -> sub_14C74F870
 -> entry fields (+08 / +20 / +40 / ...)
```

This matters because several lower-layer hooks were proven to have visible effect but were still overwritten later by upstream recalculation.

## Confirmed Entry Layout

The stamina entry layout is now much clearer:

- `+0x08` : committed current value
- `+0x10` : delta / rate accumulator
- `+0x18` : base component used in current reconstruction
- `+0x20` : pending offset / auxiliary current component
- `+0x28` : floor / lower clamp
- `+0x38` : base time
- `+0x40` : schedule / valid-until / next wake time
- `+0x48` : commit counter
- `+0x50` : maturity / threshold helper
- `+0x52` : latch byte
- `+0x53` : state / reentry / mode byte
- `+0x56` : last update time
- `+0x64` : secondary timing gate
- `+0x88` : another accumulated total

These names are still provisional, but the structural role is now consistent across static and dynamic traces.

## What `+08` And `+20` Actually Mean

One important correction from earlier assumptions:

- `+0x08` is the committed current stamina.
- `+0x20` is not the high-frequency visible stamina value.

`+0x20` is part of the reconstruction formula used by the commit layer:

```text
current = [entry+0x18] + [entry+0x20]
```

Dynamic watch results showed:

- `+0x20` is accessed frequently
- `+0x20` is sometimes written
- but in the tested samples it was often zero and was mostly being read into projection / commit math

So `+0x20` is a helper term, not the real high-frequency stamina UI source by itself.

## Confirmed Commit Layer

`sub_14C74F870` is now confirmed as the stamina commit layer.

Important logic:

```text
v5 = [a1+0x18] + [a1+0x20]
clamp v5 to max / floor
[a1+0x20] = v5 - [a1+0x18]
[a1+0x08] = v5
[a1+0x38] = time
[a1+0x50] = remainder
```

This explains why modifying this function does affect visible stamina, but only temporarily.

### Important Conclusion

If this layer is patched directly, the effect is real but downstream only.

The game later re-enters from upstream rule logic and recomputes the next projected value, so the visible stamina is eventually pulled back toward the original game result.

This is the same failure mode already observed in the existing ASI and in later CE tests.

## Confirmed Projection Helper

`sub_1412DD790` is a projection helper, not a writer.

It computes a projected current value based on:

- committed current
- timing fields
- rate / delta
- floor / cap
- schedule values

It does **not** directly write the entry.

This helper was useful for understanding the system, but patching its output alone is not enough if upstream rule aggregation keeps producing the same future delta.

## Confirmed Delta Accumulation Layer

`sub_1412DCBC0` is a key mid-layer, but not the true source.

Important logic:

```text
if state allows direct fast path:
    ++[entry+0x48]
    [entry+0x38] = time
else:
    projected = sub_1412DD790(...)
    sub_14C74F870(entry, time, projected, remainder)

[entry+0x10] += a4
sub_1412DD680(entry, time) -> sub_14C74E500
[entry+0x88] += a4
```

This explains an earlier confusing result:

- `0x1412DCC57` was very active
- but scaling the value there had weak gameplay effect

That is because this point only updates the accumulator term `+0x10`. It is important, but it is not the final visible stamina write.

## Confirmed Schedule Layer

`sub_14C74E500` updates the timing / schedule fields, especially `+0x40`.

It calls `sub_1412DD790` and uses the projected current to determine the next time gate.

This means:

- `+0x40` is a scheduling field
- not the visible stamina value
- and not the true upstream cost source either

## Dynamic Validation Summary

Several dynamic results are now confirmed:

### 1. `0x1412DCC57` Is Active But Not Final

When monitoring active play, this point hit frequently for stamina:

- `type = 17`
- `entry = stamina entry`
- `rbx` carried a meaningful per-event amount

But changing `rbx` there only produced weak or inconsistent effect.

Conclusion:

- active, yes
- useful for structure understanding, yes
- final control point, no

### 2. Commit Layer `0x14C74F870` Is Real But Downstream

Hooking the commit layer and modifying the projected current had immediate visible gameplay effect.

However:

- the effect matched the existing mod behavior
- after a while the game recalculated and pulled the stamina back toward normal

Conclusion:

- this is a real visible layer
- but it is still downstream of the actual rule source

### 3. Entry `+20` Is Not The Missing High-Frequency True Value

Watching `entry+0x20` showed:

- many reads
- some writes
- repeated use inside helper math
- often zero in current samples

Conclusion:

- `+20` participates in projected/committed current math
- but it is not the hidden high-frequency authority value the investigation was looking for

## The Important Upstream Correction

The most important new conclusion is this:

`sub_1412DCBC0` does not manufacture its own stamina delta.

It receives that delta from `sub_1412D9760`, and `sub_1412D9760` itself first calls:

```text
sub_1412DCDA0
```

The output of `sub_1412DCDA0` becomes the `a4` fed into `sub_1412DCBC0`.

That means the rule source is upstream of `DCBC0`.

## Confirmed Rule Aggregator

`sub_1412DCDA0` is now confirmed as a rule aggregator over a 48-byte record table.

Key behavior:

- the table starts at `a1 + 200`
- record count is at `a1 + 208`
- capacity is at `a1 + 212`
- each record is 48 bytes
- it scans records matching:

```text
record.word0  == selector.word0
record.qword1 == selector.qword1    (only in remove/update path)
record.word8  == selector.word8
```

- matching records are evaluated through:

```text
sub_1412DDC10(record)
```

- the total sum becomes the aggregated stamina delta

It also supports adding/removing/updating records in the same table, not just summing them.

### Confirmed Record Shape

From the disassembly, each rule record is copied and moved as a raw 48-byte block:

```text
offset +0x00 : word   key0
offset +0x08 : qword  key1 / payload field
offset +0x10 : word   key2
offset +0x18 : qword  mutable payload field
offset +0x20 : qword  payload field
offset +0x28 : qword  payload field
```

The exact semantic names are still unknown, but the structural facts are now clear:

- `+0x00` and `+0x10` are definitely selector keys
- `+0x08` also participates in exact identity matching during remove/update
- `+0x18` is explicitly overwritten across records that share the same `+0x00` and `+0x10`
- the whole 48-byte object is passed into `sub_1412DDC10 -> sub_14C7510A0`

This means the record is not just a tiny tag pair. It is a compact data-driven rule object.

### Confirmed Selector Shape

The selector object passed into `sub_1412DCDA0` is at least:

```text
selector +0x00 : word
selector +0x08 : qword
selector +0x10 : word
selector +0x18 : qword
selector +0x20 : qword
```

`sub_1412DCDA0` uses this selector in three different modes:

- sum matching records
- add a new record
- remove/update an existing record

The additive path only keys on:

```text
record.word0 == selector.word0
record.word8 == selector.word8
```

The remove/update path additionally checks:

```text
record.qword1 == selector.qword1
```

That makes the current best interpretation:

- `selector.word0` = main category / resource tag
- `selector.word8` = secondary slot / mode / sub-tag
- `selector.qword1` = instance-specific payload or owner-specific discriminator

The exact gameplay meaning is still unresolved, but the matching tiers are no longer ambiguous.

### Practical Meaning

This is the first clearly identified place where stamina delta is not just being applied, but **composed** from rule records.

That makes it much more promising than any previously tested lower layer.

## Confirmed Rule Evaluator

`sub_1412DDC10` is a thunk into:

```text
sub_14C7510A0
```

`sub_14C7510A0` is not returning a simple constant.

It evaluates record fields using range-like behavior centered around `1,000,000`.

Observed structure:

- if record field `a1[3]` is set, it builds one interval
- otherwise it checks `a1[5]`
- when both special fields are absent, it returns `a1[1]`
- otherwise it calls `sub_14070B9E0(...)` to evaluate a range / interpolation style result

### Confirmed Evaluator Field Usage

`sub_14C7510A0` uses the record as a small structured object:

```text
record +0x08 : base value
record +0x18 : upper-style shaping field
record +0x28 : lower-style shaping field
```

Behavior:

- if `record +0x18 != 0`
  - evaluate between `record.base` and `(1000000 - record+0x18)`
- else if `record +0x28 != 0`
  - evaluate between `(record+0x28 + 1000000)` and `record.base`
- else
  - return `record.base`

This is a very strong sign that the stamina rule system is data-driven around a normalized `1,000,000` domain rather than raw hardcoded percentages in code.

### Practical Meaning

This strongly suggests:

- the stamina system is not driven by a single hardcoded number
- it is driven by data records with shaping / transition logic
- that explains why some direct patches feel partial or temporary

## Confirmed Synthetic Record Constructor

`sub_1412DCCA0` is now confirmed as a synthetic 48-byte record constructor.

It initializes a record like this:

```text
record +0x00 = *a3
record +0x08 = a4
record +0x10 = *a6
record +0x18 = 0
record +0x20 = 0 or projected_value
record +0x28 = 0 or projected_value
```

More precisely:

- it first fills the whole 48-byte object with `-1/0` defaults
- then writes:
  - `record.word0  = *a3`
  - `record.qword1 = a4`
  - `record.word8  = *a6`
- if `a4 < 0` and `*a6 != 0xFFFF`
  - it queries `sub_1412D7C10(...)`
  - stores a clamped projected value into `record + 0x18`
- else if `a4 > 0` and `*a5 != 0xFFFF`
  - it queries `sub_1412D7C10(...)`
  - stores a clamped projected value into `record + 0x28`

This is important because it proves that not all stamina records come from a persistent table.

Some are synthesized on the fly and then evaluated by the same `DDC10 -> 14C7510A0` rule path.

## Current Best Explanation For The Failure Mode

The current best explanation for the repeated "works briefly, then gets pulled back" behavior is:

```text
lower-layer patch
 -> visible current changes
 -> upstream rule chain runs again
 -> DCDA0/DDC10/14C7510A0 recomputes delta
 -> DCBC0 applies new delta
 -> DD790 projects again
 -> 14C74F870 commits again
 -> visible stamina returns toward vanilla behavior
```

So the real target is not the final commit function.

The real target is the rule record aggregation / evaluation path.

## Current Most Promising Targets

The best current upstream targets are:

- `sub_1412D9760`
- `sub_1412DCDA0`
- `sub_1412DDC10`
- `sub_14C7510A0`

These are now more promising than:

- `sub_14C74F870`
- `sub_1412DD790`
- `sub_1412DCBC0`
- entry field watches

## Practical Next Step

The next reverse-engineering step should focus on identifying:

1. what each 48-byte rule record field means
2. what `record.word0` and `record.word8` correspond to in gameplay terms
3. how `sub_14C7510A0` transforms one record into a concrete stamina delta
4. which caller/path feeds the rule records used during:
   - running
   - gliding
   - air maneuver
   - recovery
5. where the 48-byte records are constructed before they are handed to `sub_1412DCDA0`

## Current Upstream Callers Worth Following

The current most useful upstream callers are:

- `sub_1412DB0C0`
  - builds and evaluates synthetic records through `sub_1412DCCA0`
- `sub_151861F50`
  - calls `sub_1412DB0C0(..., rules_at+200, a4=0)`
- `sub_151882820`
  - calls `sub_1412DB0C0(..., rules_at+168, a4=caller_flag)`
  - and later calls `sub_1412DB0C0(..., rules_at+200, a4=caller_flag)`

This suggests there are at least two upstream rule bundles being fed into the same stamina rule engine:

- one bundle starting at `object + 168`
- one bundle starting at `object + 200`

That is currently the best static lead for separating different stamina behaviors by gameplay context.

## Confirmed Skill Object Link

`sub_1402D5D40(unsigned __int16 *id)` is now confirmed to return a `Skill`-type data object.

Important clues:

- when the cache entry is missing, it performs a lookup using the string `"Skill"`
- the returned pointer is later used by the stamina-rule callers

This is important because it means the stamina rule bundles currently being followed are not generic actor state blobs.

They appear to live on a skill-definition / skill-runtime data object.

### Practical Meaning

The current best interpretation is:

- `sub_1402D5D40(skill_id)` returns the data object for one skill / action definition
- `object + 168` and `object + 200` are two rule bundles attached to that skill object
- the same stamina rule engine is then used to evaluate those bundles into concrete stamina deltas

This is a much better fit for the observed gameplay than earlier guesses:

- running
- gliding
- air maneuver
- recovery

may each correspond to different skill/action records that feed the same evaluator.

## Current Caller Context Interpretation

### `sub_151861F50`

This caller:

- iterates two slots
- resolves an ID through data at `v10 + 152`
- maps that ID through `qword_145CEF3D8 + 96`
- then calls:

```text
sub_1412DB0C0(..., skill_object + 200, 0)
```

Current best interpretation:

- this path likely uses the `+200` rule bundle as a default / passive / state-driven evaluation set

### `sub_151882820`

This caller is more interesting:

- it first calls:

```text
sub_1412DB0C0(..., skill_object + 168, a4)
```

- later, under another object/context, it calls:

```text
sub_1412DB0C0(..., skill_object + 200, a4)
```

Current best interpretation:

- `+168` and `+200` are not duplicates
- they are two distinct rule bundles for the same skill object
- one may represent a direct action cost bundle
- the other may represent a follow-up / conditional / state-propagation bundle

This is not proven yet, but the static structure strongly supports it.

## Broader Skill-Driven Stamina Flow

Further static tracing shows that the stamina rule system is wired into higher-level skill/action processing rather than only generic stat maintenance.

### `sub_1412DB9F0`

This function is still one of the most important upstream stamina producers.

It:

- derives a skill/action context from the incoming selector
- computes a base amount through:
  - `sub_141D49740`
  - sometimes `sub_141D4D9B0`
- applies percentage/mode shaping through:
  - `sub_1412DD0E0`
- and then routes the result into:
  - `sub_1412D84A0`
  - or `sub_1412D9760`

Most importantly, it contains repeated calls like:

```text
sub_1412D9760(..., a7=0x13, a8=0x0A, ...)
```

with both positive and negative `a5` payloads.

This means the stamina rule engine is being driven directly by higher-level skill/action resolution, not just by a passive stat updater.

### `sub_14237E590`

This higher-level action path also feeds the same stamina engine.

It eventually calls:

```text
sub_1412D9760(...)
sub_1412D8340(...)
sub_1412D8DC0(...)
```

depending on flags and mode.

This is important because it confirms that the stamina system is reused by action/gameplay code paths that are probably much closer to:

- movement actions
- action execution
- conditional cost/refund behavior

than the lower stat-entry layers.

## Updated Current Best Interpretation

At this point the best model is:

```text
Skill / action object
 -> one or more rule bundles (+168 / +200)
 -> optional synthetic rule record construction (DCCA0)
 -> rule aggregation (DCDA0)
 -> rule evaluation (DDC10 -> 14C7510A0)
 -> mid application (DCBC0 / D9760)
 -> projection helper (DD790)
 -> commit layer (14C74F870)
 -> visible stamina fields
```

This is now the most stable high-level explanation for all previous observations:

- lower-layer patches visibly work
- but later get overwritten
- because the skill/action layer keeps recomputing the same rule result

## Current Best Static Direction

The best remaining static direction is no longer the commit layer.

It is now:

1. isolate which skill IDs correspond to:
   - running
   - gliding
   - air maneuver
   - recovery
2. map which of those skills use:
   - bundle `+168`
   - bundle `+200`
3. determine whether one bundle is the true cost source and the other is only a derived/secondary rule layer

Only after that should a new patch point be chosen.

## What Not To Repeat

The following directions are now understood well enough and should not be the main focus anymore:

- do not keep patching `0x1412DCC57` as if it were the final source
- do not keep treating `+0x20` as the hidden real-time stamina value
- do not keep improving `0x14C74F870` as if it were the final fix
- do not keep focusing on `0x1412DD790` alone

They are useful for context, but not the correct long-term control point.
