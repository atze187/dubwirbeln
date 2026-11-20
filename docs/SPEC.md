# dubwirbeln CORE v1.0 - Master Specification

**Status:** Frozen (v1.0 architecture)
**Motto:** LOAD. PAINT. TRIGGER.
**License:** AGPLv3 (required by JUCE's free license; open source, no PRO tier)

Conventions:
- `OPEN-n` marks a decision that is still undecided. Each is listed in Part 15.
- Items marked **DECIDED** were settled in the architecture review.
- Source documents: `ROLLCRAFT_CORE_v1_0_MASTER_SPEC_3319.txt` plus the review answers (TODO-001..003 definitions).

---

## Part 1 - Product Vision

dubwirbeln CORE is a monophonic drum-roll instrument.

It is **not**: a sampler workstation, a step sequencer, a break manipulator, a MIDI roll editor.

Core principles:
- 1 Preset = 1 Roll
- 1 Bank = collection of Rolls
- 1 Instance = 1 Voice

Workflow: Load Samples -> Draw Gesture -> Generate Roll -> Adjust Parameters -> Perform

## Part 2 - Platform and Scope (DECIDED)

- Formats: **VST3** for v1. CLAP later. AU will not be supported.
- OS: Windows, macOS.
- Framework: JUCE (AGPLv3). VST3 SDK is MIT.
- NKS (browser, tags, audio previews, metadata) is **out of scope for v1** (future). A reserved empty metadata field may exist in the preset schema.
- No PRO features. Everything is open source.
- Validation (per global project rules): Steinberg VST3 validator and pluginval (strictness 5), via `build.ps1 validate`, and in CI on every platform before packaging. `tools/vst3validator` provides the SDK validator build.
- Sample import formats: WAV, AIF, MP3, FLAC.
  - Recommendation: embed samples in presets and banks as WAV or FLAC (decoded), not MP3, because MP3 encoder padding shifts `Start`/`End`.

## Part 3 - User Interface

Areas: Bank & Preset Browser (tree), Roll Painter, Trigger Preview, Samples Section, Playback Section, Status Bar.

Toolbar: Undo | Redo | Variate | Clear

Terminology (DECIDED):
- **Trigger Preview** = graphical roll display in the UI.
- **Audio Preview** = rendered audio file for browser use (future / NKS).

Status bar provides: gesture feedback, undo/redo feedback, tooltips, MIDI learn information, system messages.

## Part 4 - Roll Painter

The user draws gestures, not triggers.

Pipeline: Gesture -> Recognition -> Roll Type -> Roll Engine -> Audio

### Roll Types

| Type  | Subdivision progression                |
|-------|----------------------------------------|
| BUILD | 1/8 -> 1/16 -> 1/32 -> 1/64            |
| DROP  | 1/64 -> 1/32 -> 1/16 -> 1/8            |
| RUSH  | 1/8 -> 1/8 -> 1/8 -> 1/32 -> 1/64      |
| BURST | 1/64 -> 1/64 -> 1/64 -> 1/64           |
| WAVE  | 1/16 -> 1/32 -> 1/16 -> 1/32 (see Intensity for high intensity) |

## Part 5 - Gesture Engine

Gestures (DECIDED correction of source typo):
- BUILD: `/`
- DROP: `\`
- RUSH: `_____/`
- BURST: `. * o` (dots)
- WAVE: `~~~~` or `/\/`

Recognition:
- Gestures are normalized before analysis.
- BUILD / DROP: 0 direction changes, or at most 1 very small direction change (< 10 % amplitude).
- WAVE: at least 2 direction changes, each with amplitude > 10 %.
- Boundary behavior at exactly 10 % is to be defined in implementation (suggest: <= 10 % counts as BUILD/DROP).

Confidence (DECIDED feedback behavior):
- **> 80 %**: apply silently.
- **60-80 %**: apply the best guess immediately. The status bar shows e.g. `BUILD 72 % - alternative: WAVE 61 %`; the alternative is clickable and switches the roll type.
- **< 60 %**: reject. Status bar message asks the user to redraw. The current roll stays unchanged.

**Gesture scope (DECIDED, closes OPEN-8):** a gesture sets **only the Roll Type**. Intensity and Blend stay unchanged and are set with their controls. Gesture length, steepness and speed do not influence any parameter.

## Part 6 - Roll Engine

Parameters: Roll Type, Intensity, Blend.

### Determinism (DECIDED)

dubwirbeln CORE is deterministic. The same preset produces the same triggers, velocities and sample distribution.

Exceptions:
- None for Humanize: it is seeded (`humanize.seed`, Part 7), so it is reproducible as well.
- Variate generates new data once; the result is stored (see Part 10).
- Velocity Follow makes the roll depend on the played velocity. This is intended.

### Time model and windowing (DECIDED)

- The roll is always generated over a **4-bar internal reference**: all phases, triggers and Sample DNA are defined on the full 4 bars.
- A shorter playback window trims the reference. **BUILD, RUSH, WAVE and BURST trim the early portion** (keep the end). Example: BUILD at 4 bars -> 1 bar yields only the final escalation phase.
- **DROP trims the end** (keeps the start, DECIDED): a 1-bar DROP window keeps the first bar, i.e. the dense 1/64 start, and drops the slow tail.
- Window lengths for v1: **4, 2, 1, 1/2 bar**. Windows longer than 4 bars are not supported in v1 (future decision: lead-in, stretch, or repeat).
- Because generation is always over the full reference, DNA distribution does not depend on window length.

### Density

Density DNA is defined by roll type and controlled by Intensity (TODO-001). Triplets are excluded from v1.

### Intensity (TODO-001, DEFINED, with gaps)

Intensity describes the escalation degree of a roll. It affects the maximum grid level and how long the highest grid level is held. It does **not** affect Roll Type, Sample DNA, Velocity Curve, Blend or Playback Mode.

| Intensity | BUILD                          | DROP                            | WAVE            |
|-----------|--------------------------------|---------------------------------|-----------------|
| 0-24 %    | 1/8 -> 1/16                    | 1/16 -> 1/8                     | 1/16 <-> 1/32   |
| 25-49 %   | 1/8 -> 1/16 -> 1/32            | 1/32 -> 1/16 -> 1/8             | 1/16 <-> 1/32   |
| 50-100 %  | 1/8 -> 1/16 -> 1/32 -> 1/64    | 1/64 -> 1/32 -> 1/16 -> 1/8     | 1/16 <-> 1/64   |

WAVE (DECIDED): the DNA stays 1/16 <-> 1/32 at low intensity and 1/16 <-> 1/64 at high intensity. Intensity defines the amplitude of the oscillation.

Hold time: the higher the intensity, the longer the highest grid level is held (e.g. BUILD at 50 %: 1/64 only briefly; at 100 %: considerably longer).

**RUSH (DECIDED, closes OPEN-2):** phases 3x 1/8, 1/32, 1/64. Intensity controls how far the roll escalates, as for BUILD:
- 0-49 %: escalates only up to 1/32 (3x 1/8 -> 1/32).
- 50-100 %: escalates up to 1/64 (3x 1/8 -> 1/32 -> 1/64), and the 1/64 phase is held longer as intensity rises.

**BURST (DECIDED, closes OPEN-2):** always 1/64, no escalation. Intensity controls only the **duration** of the burst (see Hold time below). It does not change the grid. A BURST starts immediately on Note-On, with no silence before it; its length is `min(window, share * 4 bars)`.

**Hold time (DECIDED, closes OPEN-5):** the share of the 4-bar reference that the highest reached grid level occupies is a linear function of Intensity:

| Intensity | Share of the highest level |
|-----------|----------------------------|
| 50 %      | 10 %                       |
| 75 %      | 25 %                       |
| 100 %     | 40 %                       |

Formula (continuous, DECIDED):
- `I >= 50 %`: `share = 10 % + 0.6 * (I - 50)`.
- `I < 50 %`: `share = 2 % + 0.16 * I` (continuous at 50 %; the highest reached level is 1/16 or 1/32 there).

Applies to BUILD, DROP and the RUSH peak phase (1/64 at I >= 50 %, 1/32 below). The remaining reference time is split equally among the other phases (RUSH's three 1/8 phases count as three phases). Intensity is continuous within a zone; the zones only decide which grid levels are reachable.

**WAVE (DECIDED):** no hold time. Intensity selects only the amplitude of the oscillation through the zone (1/16 <-> 1/32 below 50 %, 1/16 <-> 1/64 from 50 %).

BURST (duration): linear from 25 % of the reference at Intensity 0 % to 100 % at Intensity 100 %: `share = 25 % + 0.75 * I`.

**DROP trimming (DECIDED, closes OPEN-11):** DROP trims the end of the reference, not the start (see time model above). Example: at Intensity 100 % the 1/64 phase fills 40 % (1.6 bars) of the reference, so a 1-bar DROP window consists entirely of 1/64.

### Sample DNA

DNA describes relative A/B distributions, not a fixed trigger list. Patterns are proportionally mapped onto the generated triggers (DECIDED: **tiled**, i.e. `pattern[i mod length]` within a phase; not stretched).

Example: `A A B A` over 8 triggers -> `A A B A A A B A`.

Patterns (DECIDED; DROP corrected to the mirror of BUILD):

```
BUILD : A A A A | A A B A | A B A B | B A B A      rows: 1/8 | 1/16 | 1/32 | 1/64
DROP  : B A B A | A B A B | A A B A | A A A A      rows: 1/64 | 1/32 | 1/16 | 1/8
RUSH  : A A A A | A A B A | A B B A B              rows: 1/8 | 1/32 | 1/64
BURST : A B A B                                     single row (1/64)
WAVE  : A A B B                                     single row
```

**Row mapping rule (DECIDED, closes OPEN-3):**
1. Each `|`-separated row belongs to one **grid level**, not to a phase index. Every trigger uses the row of its own grid level. RUSH uses the same 1/8 row for all three 1/8 phases.
2. Rows are **tiled** and restart per phase: `row[i mod rowLength]`, with `i` counted from the first trigger of the phase. Row length is therefore irrelevant (the 5-entry RUSH row is valid).
3. BURST and WAVE have a single row, tiled over the whole roll.
4. At lower intensity only the rows of the reached grid levels are used (BUILD at 0-24 % uses the 1/8 and 1/16 rows). Calmer rolls therefore also get calmer accent patterns.
5. Blend acts per row (see Blend).

WAVE keeps the single row `A A B B` at all intensities (assumed; revisit if a dedicated high-intensity 1/16 <-> 1/64 pattern is wanted).

### Blend (TODO-002, DEFINED, with gaps)

Blend is a deterministic morph between all-A, the roll type's DNA, and all-B.

- 0 %: all A.
- 50 %: original DNA of the roll type.
- 100 %: all B.

Blend acts per DNA row (one row = one grid level). BUILD example (rows as above):
- 25 %: `A A A A | A A B A | A A A B | A A B A`
- 50 %: original DNA
- 75 %: `A B A B | A B B B | A B B B | B A B B`

**Blend formula (DECIDED, closes OPEN-4; replaces the earlier illustrative table):**

Per row of length `n` with `k` original B positions and blend `b` in [0, 1]:
- `b <= 0.5`: the row keeps `round(k * b / 0.5)` of its original B positions (round half up).
- `b > 0.5`: the row has `k + round((n - k) * (b - 0.5) / 0.5)` B positions: all original ones plus added ones.
- Positions are chosen in a fixed **priority order** `P`: sort positions by the number of trailing zero bits of the index (odd positions first, then 2, then 4, ..., position 0 last); ties go to the higher index. For `n = 4`: `P = 3, 1, 2, 0`.
- Adding B uses `P` on the remaining A positions. Removing B keeps the B positions that come first in `P` and removes the others (strong positions first), so B accents stay on weak positions (ghost character).
- Results are therefore always nested: lower blend values never contain B positions that higher values lack. `b = 0` gives all A, `b = 0.5` the original row, `b = 1` all B.
- No randomness; same blend value gives the same pattern. Blend is applied to the row before tiling.

Assumption (confirm or change): the priority order `P` above. Any fixed, nested, deterministic order satisfies the rule.

### Velocity Curves

Flat, Crescendo, Decrescendo, Spike, Inverted Spike, Wave, Ghost.

Template points (DECIDED, `t` over the 4-bar reference, `v` velocity, linear interpolation): Flat 100; Crescendo 40 to 127; Decrescendo 127 to 40; Spike 70, 127 at t = 0.5, 70; Inverted Spike 110, 40 at t = 0.5, 110; Wave 60/120/60/120/60 at t = 0, 0.25, 0.5, 0.75, 1; Ghost 35 with narrow accents of 100 at t = 0, 0.25, 0.5, 0.75 (see `engine/VelocityCurve.cpp`). A trigger's velocity is the curve value at its position over the full reference, so window trimming does not change it.

Each curve type is a **template** (DECIDED). Changing the type regenerates the control points from that type's template; afterwards the points are freely editable. Variate moves only the points, never the type.

## Part 7 - Playback Engine

Grid: 1/4, 1/8, 1/16, 1/32, 1/64. 1/4 is the lowest density level. Triplets are excluded from v1.

Modes (DECIDED):
- **Trigger**: Note-On -> roll forward -> window end -> stop. Note-Off is ignored.
- **Hold**: Note-On -> roll forward -> window end -> key still held -> roll backward -> window start -> stop. Note-Off at any time, in the forward or the reverse phase, ends the roll immediately with the 10 ms release (DECIDED).

Humanize: timing and velocity variation. DNA remains unchanged.

Humanize seed (DECIDED, closes OPEN-7): the preset stores `humanize.seed` (uint32). The timing and velocity deviation of a trigger is a deterministic function of the seed and the trigger's index counted from the start of the 4-bar reference, using the engine's specified PRNG (see Part 10). The same preset therefore renders identically on every bounce, and window trimming does not shift the humanization of the remaining triggers. With Humanize = 0 the seed has no effect.

`humanize.seed` is a **separate field** from `variation.seed`, so pressing Variate does not change the humanization. A new random seed is assigned once when a preset is created (not on every load) and is stored. **Save As keeps the seed**, so a copied preset sounds identical.

Humanize amounts (DECIDED): at 100 % the timing deviates by up to +/- 10 ms (uniform, independent of tempo) and the velocity by up to +/- 12, both scaled linearly by the percent value. The two values of trigger `i` are the first two `nextDouble()` outputs of `Rng(humanize.seed, stream = i)`; the velocity is rounded half up and clamped to 1..127.

Swing: systematic off-beat timing shift, deterministic (DECIDED): within a phase every second step of the phase's grid (counted from the start of the reference) is delayed by `swing% * step / 3`, so 100 % gives a triplet feel. The shift depends on the grid level of the trigger's phase.

Velocity Follow: OFF, VELOCITY, VELOCITY + INTENSITY (default).

Velocity Follow formulas (DECIDED): with VELOCITY and VELOCITY + INTENSITY every trigger velocity (after curve and humanize) is scaled by `played / 127`, rounded half up and clamped to 1..127. With VELOCITY + INTENSITY the roll is also generated with the effective intensity `(played - 1) / 126 * 100`; the stored Intensity of the preset is not changed and applies with OFF and VELOCITY.

Scheduler behaviour (DECIDED, `engine/RollScheduler`): the roll starts on the Note-On sample (or the quantized grid position) with the window already applied, so its first trigger is at the start. Swing and humanize shifts are added to the trigger position; a humanize shift never moves a trigger before the start of the roll. Positions are tracked in 1/64 units, so a tempo change takes effect at the next block (4/4 is assumed; other time signatures are a later topic). In Hold mode the backward phase plays the window's triggers in reverse order at mirrored positions without swing and humanize; the trigger at the window start is not repeated, so the roll ends at the window start.

Monophonic operation: a new roll immediately replaces the current roll.

### Real-time rules (DECIDED)

- The audio thread (`processBlock`) never allocates, locks (it only try-locks) or frees memory. This is checked by tests that count heap allocations during the scheduler, the sample engine and the whole processor (`tests/support/AllocGuard`).
- The roll is generated on the audio thread at Note-On (or at the quantized start) into fixed-capacity lists; the work is a few hundred triggers. Parameter changes and preset edits are prepared on the message thread and only copied on the audio thread.
- Limits, beyond which things are dropped: 256 triggers per roll (the most a 4-bar reference at 1/64 holds), 2048 scheduler events and 256 note events per block, 16 velocity curve points (the schema limit), 4 sample voices.
- Loaded samples are kept alive by the message thread, so the audio thread never releases the last reference to one.

### Host sync (TODO-006, DECIDED)

- Tempo and time signature come from the host. If the host provides none: 120 BPM, 4/4.
- Host transport is not required. A roll starts on Note-On even when the host is stopped, so the instrument stays playable (e.g. for browsing presets).
- Host stop during a roll: the roll runs to its end (no abrupt cut).
- Tempo change during a roll: triggers follow the current tempo; grid positions are recomputed continuously.

### Start quantization (DECIDED, closes TODO-006)

`Quantize` is a Playback parameter stored **in the preset**. Values: **Off | 1/16 | 1/8 | 1/4 | 1 Bar**. Default: **Off** (immediate start on Note-On).

- With Quantize != Off a roll waits on Note-On for the **next** grid position of the host transport. It never starts earlier than the note.
- Host stopped: no grid position exists, the roll starts immediately.
- Monophonic replacement: if a note is played while a roll is running, the old roll keeps running until the next grid position and is replaced there. With Off the replacement is immediate, as before.
- At most one start is pending. A new Note-On before the grid position replaces the pending start.
- Trigger mode: Note-Off is ignored; the roll starts at the grid position anyway.
- Hold mode: if the key is released before the grid position, the roll is **cancelled** (nothing plays).
- UI: the status bar shows "waiting for grid ..." while a start is pending.

## Part 8 - Sample Engine

- Sample A = main sample. Sample B = accent / ghost sample.
- Per sample: Tune, Start, End, Level.
- Sample B is activated by Sample DNA; Blend additionally acts as weighting. Velocity affects both samples identically (no separate velocity scaling for A and B).

Hard Retrigger (DECIDED): new trigger -> 10 ms release on the running voice -> new voice starts with 5 ms attack. This is not a hard cut; it is an effective short crossfade. A brief two-voice tail is allowed and does **not** count as polyphony. It applies to every roll trigger, not only to new MIDI notes (confirmed).

Trigger envelope (engine constants, hidden, not stored in presets, logarithmic fades):
```
ATTACK_MS  = 5
RELEASE_MS = 10
```
User-adjustable attack/release is a future topic.

Sample engine details (DECIDED, `engine/SampleEngine`):
- Fades are linear in dB: the attack rises from -60 dB to 0 dB over 5 ms, the release falls by 60 dB over 10 ms (from the current level, also when it starts during the attack) and then the voice ends.
- Velocity gain is linear, `velocity / 127`, and the same for A and B. Level is applied in dB on top.
- Tune changes the playback speed (`2^(semitones / 12)`, +12 = twice as fast, the sample gets shorter), with linear interpolation. A different sample rate of the file is converted the same way.
- A voice plays from `start` to `end` and ends there without an extra fade. If `end <= start`, the trigger does nothing.
- A trigger for a slot without a sample does nothing, the running voice keeps playing.
- A pool of 4 voices covers the retrigger crossfades; if triggers come faster than the release, the voice that has been fading out longest is cut.
- Mono samples feed both channels, samples with more than two channels use the first two.
- Import formats: WAV, AIFF, FLAC, MP3 (decoded to memory). Samples are stored as WAV or FLAC in the container; saving them comes with the container step.

Note: at high tempi 1/64 steps (about 19 ms at 200 BPM) overlap noticeably with attack + release; verify audibly.

## Part 9 - MIDI

- One freely definable trigger note. Pitch does not transpose.
- Velocity drives the velocity curve and optionally Intensity, according to Velocity Follow.
### MIDI Learn (DECIDED, closes TODO-004)

- A "Trigger Note" field has a Learn button. On click the status bar shows "MIDI Learn: play a note". The next incoming Note-On becomes the trigger note and Learn mode ends. Clicking the button again or pressing Esc cancels. The note can also be typed into the field.
- Only Note-On triggers. All other notes are ignored. MIDI channel: omni (any channel).
- Default trigger note: **C1 (MIDI note 36)**.
- Storage: **plugin state of the instance** (saved with the DAW project). It is **not** part of `.dwpreset` or `.dwbank`, so switching presets or banks never changes the trigger note.
- v1 scope: trigger note only. CC learn for Intensity, Blend and other parameters is a future topic; until then use host automation.

## Part 10 - Variate and Undo

Variate creates an alternative version of the same roll. The character of the roll stays intact.

May change (as specified; basis of the percentages is OPEN-6):
- Intensity +/- 15 %
- Blend +/- 20 %
- Velocity curve points +/- 10 velocity
- Density DNA fine distribution +/- 15 %
- Sample DNA expression +/- 20 %

Must not change: Roll Type, Sample A, Sample B, Velocity Curve type, Playback Mode, Windowing, NKS data.

Example: BUILD, Intensity 70 %, Blend 50 % -> BUILD, Intensity 78 %, Blend 61 %.

Determinism: Variate acts once. After saving there is no randomness; the result becomes part of the preset.

**Persistence (DECIDED, closes OPEN-1):**
- Intensity, Blend and velocity curve points are overwritten directly in their own preset fields. No extra storage.
- Density DNA fine distribution and Sample DNA expression are derived deterministically from a stored seed: `"variation": { "seed": <uint32> }`. `seed = 0` means no variation. One seed covers both.
- Each Variate pass replaces the seed. The DNA-level changes are relative to the base DNA, so they do not accumulate.
- Intensity, Blend and curve points are changed **relative to the current state** on every pass (they can drift over several passes; values are clamped).
- Reproducibility requirements: the engine uses its own specified PRNG (e.g. PCG32 or xoshiro128), never `std::` distributions (implementation-defined across standard libraries), and the preset carries `schemaVersion` and `engineVersion` so old presets stay reproducible if the derivation changes.

**Percentages (DECIDED):** the values above are **percentage points** on the 0-100 % scale, applied relative to the current value and **clamped** to the valid range.

**Zone rule (DECIDED, closes OPEN-6):** Variate keeps Intensity inside its current Intensity zone (0-24, 25-49, 50-100 %). The +/-15 point change is clamped to the zone boundaries, so Variate never changes which grid levels a roll reaches (e.g. 49 % never gains 1/64).

**Variate details (DECIDED, `engine/Variate`, `engine/RollGenerator`):**
- One pass takes all its randomness from a pass seed (the caller picks it at random; 0 becomes 1) via PCG32 stream 7, in this order: Intensity, Blend, then every curve point. Intensity changes by a uniform amount in +/-15 points, Blend in +/-20 points; both are rounded to 0.1. Intensity is clamped to its zone: 0-24.9, 25-49.9, 50-100. Each curve point moves by a whole number in +/-10 and is clamped to 1..127. The pass seed becomes `variation.seed`. A preset without a roll is not varied.
- Density DNA fine distribution (stream 101): the shares of all phases that are not held are multiplied by `1 + 0.15 * u` (`u` uniform in -1..1, one draw per phase in phase order) and scaled back so their sum stays the same. Hold time and grid levels do not change. BURST has a single phase and WAVE has no hold, so WAVE redistributes all four quarters.
- Sample DNA expression (stream 102): a strength `0.2 * u` is drawn once. In every run of consecutive triggers on the same grid level, `round(strength * n)` distinct triggers swap A and B (partial Fisher-Yates). It is applied to the whole 4-bar reference before the window trims it.
- With `variation.seed = 0` neither change happens.
- Redo is possible until the next pass, Clear or loading a preset. The undo state is not saved.

### Undo (DECIDED)

Undo/Redo applies only to **the last Variate pass** (a single level, no chain). If the user likes a variation they save the preset. This replaces the earlier multi-step Variate undo chain.

### Clear (DECIDED, closes OPEN-9)

Clear empties **only the roll**; Sample A, Sample B and their parameters stay.

- Reset by Clear: Roll Type (back to "no roll", the instrument is silent), original gesture, Intensity and Blend (back to defaults), `variation.seed` (0).
- Kept by Clear: Samples A/B (Tune, Start, End, Level), Velocity Curve, Playback settings (mode, window, Quantize, Humanize, Swing, Velocity Follow), `humanize.seed`.
- Clear is not undoable (Undo covers only Variate) and discards the stored Variate undo state.
- Clear marks the preset dirty.

Assumptions (confirm or change):
- The velocity curve and playback settings count as performance setup, not as part of the roll, and are therefore kept.
- Clear asks for confirmation only if the preset is dirty; a freshly loaded, unmodified preset is cleared without a dialog.

## Part 11 - Preset System

Extension: `.dwpreset`. Embedded samples: yes. Dirty state supported. Save = overwrite, Save As = new preset.

Preset stores (DECIDED):
- Roll Type, Intensity, Blend
- Velocity Curve
- Samples A / B (embedded)
- Playback parameters
- Optional: original gesture

Preset does **not** store the trigger list; triggers are generated during playback.

Also stored: `variation.seed` (Part 10), `humanize.seed` (Part 7), `schemaVersion`, `engineVersion`. Preview / NKS metadata: reserved, future.

### Defaults (DECIDED)

Used for a new preset and as the reset values for Clear:

| Parameter       | Default                      |
|-----------------|------------------------------|
| Roll Type       | none (silent until a gesture is drawn) |
| Intensity       | 50 %                         |
| Blend           | 50 % (original DNA)          |
| Window          | 4 bars                       |
| Mode            | Trigger                      |
| Quantize        | Off                          |
| Velocity Curve  | Flat                         |
| Humanize        | 0                            |
| Swing           | 0                            |
| Velocity Follow | VELOCITY + INTENSITY         |
| `variation.seed`| 0                            |

### Host parameters and plugin state (DECIDED)

- The scalar preset values are **host parameters** (automatable): Roll Type (None, Build, Drop, Rush, Burst, Wave), Intensity, Blend, Window, Mode, Quantize, Humanize, Swing, Velocity Follow. The index order of the choice parameters is fixed in `ParameterValues` (`engine/Preset.h`).
- Velocity curve, seeds, gesture, samples and the preset name are plugin state, not parameters.
- The plugin state is saved as JSON (`engine/PluginState.h`): `{ "stateVersion", "triggerNote", "preset" }`, where `preset` has the format of `schemas/preset.schema.json`. The trigger note belongs to the instance and is never part of the preset (Part 9). The parameter values in the state are those of the preset object.
- Loading a state sets the parameters from its preset. A state that cannot be read is ignored and the instance keeps its current values.
- A preset without a roll type (`rollType: null`) is silent: Note-On starts nothing.
- When a preset is read, velocity curve points are sorted by position; unknown fields are rejected (except `metadata`, which is kept untouched).

### Container format (DECIDED, closes TODO-005)

`.dwpreset` and `.dwbank` are both **ZIP archives** with a `manifest.json` in the archive root. `manifest.json` carries `schemaVersion` and the preset or bank description.

`.dwpreset` layout:
```
manifest.json          preset description
samples/               Sample A and B as WAV or FLAC
preview/               reserved (future)
```

Compression: samples are stored without additional compression (WAV/FLAC in store mode); JSON uses Deflate.

Container details (DECIDED, `engine/PresetArchive`):
- Every sample the preset uses must be in the archive under the slot's `file`, and its content must match the slot's `sha256` (checked when writing and when reading; a mismatch rejects the file). Samples the preset does not reference are not written. If both slots use the same file, it is stored once.
- WAV and FLAC files are stored as they are (the hash is that of the original file). Other formats (AIFF, MP3) are converted to 24-bit FLAC when the sample is loaded, and that FLAC is the stored sample.
- Two different files with the same name get distinct archive paths when saving (`name_B.ext` for the second).
- Unknown entries (for example `preview/`) are ignored when reading. Limits when reading: manifest 4 MiB, one sample 512 MiB.
- A slot whose audio cannot be read when a preset is loaded is dropped.
- The plugin state is the same archive plus `instance.json` (`stateVersion`, `triggerNote`), so the samples survive the DAW project. States saved as plain JSON by earlier versions are still read.

## Part 12 - Bank System

Extension: `.dwbank`. ZIP archive (see Part 11 for the shared container rules). Contains presets, samples, previews (reserved), metadata.

Layout (DECIDED):
```
manifest.json          bank metadata and list of presets
presets/*.json         one JSON entry per preset (not nested .dwpreset files)
samples/<sha256>.wav   shared sample pool, named by content hash (or .flac)
preview/               reserved (future)
```

- Samples are **deduplicated by SHA-256** of their content. Presets reference samples by hash, so a sample used by several presets is stored once.
- Presets inside a bank are JSON entries. Exporting a preset from a bank produces a standalone `.dwpreset` including its samples.

## Part 13 - JSON Schemas

Written as JSON Schema (draft 2020-12) in `schemas/`: `velocity-curve.schema.json`, `preset.schema.json`, `bank.schema.json`. They are validated against example files in `schemas/examples/valid` (must pass) and `schemas/examples/invalid` (must be rejected) with `python tools/validate_schemas.py` (needs `jsonschema >= 4.18`).

Values fixed by the schemas (DECIDED):
- Velocity curve: 2-16 points `{t, v}` with `t` in 0..1 over the 4-bar reference, `v` as MIDI velocity 1..127, linear interpolation.
- Sample parameters: `tuneSemitones` -24..24, `start`/`end` as fractions 0..1 of the sample (`end` > `start`, not enforceable in JSON Schema, checked in code), `levelDb` -60..+6.
- Gesture: up to 512 points, normalized 0..1.
- Humanize and Swing as percent 0..100; `windowBars` as 4, 2, 1, 0.5.
- Bank presets carry `id`, `name`, optional `folder` (tree path in the browser), and `file`.

The preset schema covers: roll type, intensity, blend, velocity curve, sample A/B references and parameters, playback settings (including Quantize), `variation.seed`, `humanize.seed`, optional original gesture, `schemaVersion`, `engineVersion`.

## Part 14 - Future Topics

- NKS integration (browser, tags, audio previews, metadata)
- CLAP
- Controller integration (including CC learn for parameters)
- Global default trigger note for new instances
- Noise generator companion
- Additional roll types
- Triplets
- Windows longer than 4 bars
- User-adjustable attack/release

## Part 15 - Open Items

| ID      | Topic                                                                              |
|---------|------------------------------------------------------------------------------------|
| OPEN-1  | ~~Variate persistence~~ **CLOSED** (seed + direct overwrite, Part 10)              |
| OPEN-2  | ~~Intensity for RUSH and BURST~~ **CLOSED** (Part 6)                               |
| OPEN-3  | ~~Sample DNA rows vs grid phases~~ **CLOSED** (row = grid level, Part 6)            |
| OPEN-4  | ~~Blend formula~~ **CLOSED** (Part 6, priority order P to confirm)                 |
| OPEN-5  | ~~Intensity hold-time numbers~~ **CLOSED** (Part 6, with assumptions to confirm)   |
| OPEN-11 | ~~Window trimming for DROP~~ **CLOSED** (DROP trims the end, Part 6)               |
| OPEN-6  | ~~Variate percentages / zone crossing~~ **CLOSED** (points, clamped to zone, Part 10) |
| OPEN-7  | ~~Humanize seed~~ **CLOSED** (`humanize.seed`, Part 7)                             |
| OPEN-8  | ~~Gesture scope~~ **CLOSED** (roll type only, Part 5)                              |
| OPEN-9  | ~~Clear~~ **CLOSED** (roll only, samples stay, Part 10; assumptions to confirm)    |
| OPEN-10 | ~~Container format, Host sync, MIDI Learn~~ **CLOSED** (Parts 7, 9, 11, 12)         |

All OPEN items are closed and the spec is frozen. The assumptions below were accepted without change when freezing:
- Part 6: Blend priority order `P`; WAVE single DNA row `A A B B`; BURST and WAVE trim the early portion (BURST uses `min(window, share * 4 bars)` instead).
- Part 10: Clear keeps velocity curve and playback settings, confirms only when dirty.

The schemas are validated against example files (Part 13). Next: project skeleton (JUCE, VST3 validation per project rules).
