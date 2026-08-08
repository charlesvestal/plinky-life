# plinky-life — design

**Date:** 2026-08-08
**Status:** implemented
**Target:** Plinky 12 panel, blank/universal faceplate

> **Two deviations were made during implementation.** Both are recorded in §14 at the
> bottom, and both are the same kind of decision: an API the published docs describe but do
> not pin down precisely enough to rely on.

A Conway's Game of Life sequencer for the Plinky 12, in the spirit of
[ZOA: Living MIDI Sequencer](https://apps.apple.com/us/app/zoa-living-midi-sequencer/id1581881354).

A 16×16 cellular automaton is a *palette*, not a piano roll. Four independent voices walk
through it at their own rates, and what they find alive is what they play. The automaton
rewrites the palette underneath them on its own clock.

---

## 1. Concepts

**The world.** A 16×16 toroidal Conway (B3/S23) automaton. Columns are steps; rows are scale
degrees. Row 15 is degree 0, row 0 is degree 15.

**Traversal.** Which column a voice is on. *(Implemented in `traversal.h` rather than
`playhead_t` — see §14.)*

**Selection.** Which live cell *in that column* actually sounds. This is the movement-rule
layer, and it is where the generative character comes from.

**An empty column is a rest.** This is load-bearing: it is what makes the automaton's density
read as rhythm rather than as a wall of notes.

The two layers are deliberately separate. ZOA's feature list conflates them; keeping them apart
is what makes the behaviour describable and testable.

---

## 2. Timing

Five independent `clock_divider_t` instances, all updated from `on_sequence()`:

| Divider | Purpose |
|---|---|
| `gen_clock` | how often the world evolves one generation |
| `voice_clock[0..3]` | how often each voice advances to its next column |

Each voice pairs its divider with a 16-step traversal. The transport-start, transport-seek and
post-load edge cases are handled by gating the advance on `sequencer_should_advance_playhead()`,
so a divider's synthetic start edge fires the current column without stepping past it.

*(This was specified as `playhead_t`; §14 records why it is `traversal.h` instead.)*

**Polyrhythm comes from differing rates, not differing loop lengths.** All four voices span
all 16 columns. Per-voice loop bounds are **cut from v1**.

The generation clock is fully independent of the voice clocks. The world can evolve far slower
than the voices read it (the normal musical case: a slowly breathing palette) or far faster
(chaotic).

---

## 3. Selection rules

Given the set of live cells in a voice's current column, ordered top (row 0) to bottom (row 15):

| Rule | Picks |
|---|---|
| `FIRST` | topmost live cell |
| `LAST` | bottommost live cell |
| `UP` | cycles through live cells in order, index retained across ticks |
| `DOWN` | same, descending |
| `UPDOWN` | ping-pongs through them, endpoints not repeated |
| `DOWNUP` | ping-pongs, starting downward |
| `RANDOM` | uniform pick among live cells |
| `WALK` | live cell whose row is nearest the voice's previous note row |
| `RISE` | nearest live cell strictly above the previous row, wrapping to the bottom |
| `FALL` | nearest live cell strictly below the previous row, wrapping to the top |
| `ALL` | every live cell in the column, simultaneously |

`ALL` is **not from ZOA**. It is added because it is the only rule that produces harmony rather
than a monophonic line per voice, and it costs roughly six lines. It is the one place this design
knowingly departs from the reference.

Rules that retain state (`UP`, `DOWN`, `UPDOWN`, `DOWNUP`, `WALK`, `RISE`, `FALL`) hold that
state per voice and must degrade gracefully when the column's live-cell set changes size between
ticks — which it constantly will, because the automaton is rewriting it. Concretely: the retained
index is clamped into range on read, never assumed valid from the previous tick.

---

## 4. Pitch mapping

Row → scale degree → MIDI note via the system's scale machinery:

```c
int degree = (15 - row) + voice.pitch_offset;
int note   = life_degree_to_note(degree, root_note, current_scale);
```

*(Specified as `shift_note_along_scale(...)`; §14 records why the mapping is ours instead.)*

16 rows is roughly two octaves of a 7-note scale. An **octave span** setting compresses or
stretches this by scaling the degree step.

### Scales use the system globals

`current_key` (root note) and `current_scale` (a **12-bit root-relative bitmask**, bit 0 = root,
bit *n* = *n* semitones above) are Plinky system globals, shared across panels, with
`set_current_key_and_scale(...)` as the setter.

Every one of ZOA's 29 scales fits in one `uint16_t`. So the scale set is a
`static const uint16_t[29]` table — **in flash, free against the 128 KB arena** — and choosing a
scale sets the system global. The panel drives the instrument's harmonic state rather than
keeping a private one; change scale in plinky-life and the rest of the Plinky follows.

Verified subset (bit 0 = root):

| Scale | Semitones | Mask |
|---|---|---|
| Chromatic | 0–11 | `0xFFF` |
| Major (Ionian) | 0 2 4 5 7 9 11 | `0xAB5` |
| Natural Minor (Aeolian) | 0 2 3 5 7 8 10 | `0x5AD` |
| Dorian | 0 2 3 5 7 9 10 | `0x6AD` |
| Phrygian | 0 1 3 5 7 8 10 | `0x5AB` |
| Lydian | 0 2 4 6 7 9 11 | `0xAD5` |
| Mixolydian | 0 2 4 5 7 9 10 | `0x6B5` |
| Locrian | 0 1 3 5 6 8 10 | `0x56B` |
| Major Pentatonic | 0 2 4 7 9 | `0x295` |
| Minor Pentatonic | 0 3 5 7 10 | `0x4A9` |
| Blues | 0 3 5 6 7 10 | `0x4E9` |
| Whole Tone | 0 2 4 6 8 10 | `0x555` |
| Harmonic Minor | 0 2 3 5 7 8 11 | `0x9AD` |
| Melodic Minor | 0 2 3 5 7 9 11 | `0xAAD` |
| Diminished (W-H) | 0 2 3 5 6 8 9 11 | `0xB6D` |
| Diminished (H-W) | 0 1 3 4 6 7 9 10 | `0x6DB` |
| Phrygian Dominant | 0 1 4 5 7 8 10 | `0x5B3` |
| Double Harmonic / Bhairav | 0 1 4 5 7 8 11 | `0x9B3` |
| Hungarian Minor | 0 2 3 6 7 8 11 | `0x9CD` |
| Pelog | 0 1 3 7 8 | `0x18B` |
| Hirajoshi | 0 2 3 7 8 | `0x18D` |

All 29 are implemented in `src/scales.h`. Every entry is covered by tests asserting bit 0 is
set, that no bits sit above 12, and that no two scales collide — a cheap guard against
transcription slips in a hand-written table. The 21 masks above were additionally verified by
recomputing them from their semitone lists.

---

## 5. Note output — two sinks

Voice *N* maps to `preset_idx` *N* (0–3). Both sinks derive from that one index, which is why
supporting both costs so little:

**Internal.** `voice_allocator_t` claims a synth voice for the voice's stable `source_id`, then
`play_synth(synth_voice, preset_idx, velocity, note_q8, retrigger)`.

**MIDI.** `get_midi_channel_for_preset_idx(preset_idx, true)` gives the wire channel, then
`midi_write(ports, MAKE_NOTEONMSG(channel, note, velocity))`.

Each voice therefore gets its own Plinky patch *and* its own MIDI channel, following the
instrument's existing preset↔channel convention rather than inventing one.

The sequencer emits an abstract note event; the two sinks consume it. Neither sink knows about
the automaton.

### Note lifecycle

Each voice holds its currently sounding note — a small fixed array, since `ALL` can sound up to
16 at once — each with a countdown in sequence ticks. Note length is a percentage (10–100%) of
that voice's step interval. On expiry: `play_synth(..., velocity=0, ...)` and a MIDI note-off.

Muting a voice, changing its rate, stopping transport, and panel unload must **all** release
held notes. Stuck MIDI notes are the classic failure mode here; every path that stops a voice
goes through one `release_all(voice)` function.

---

## 6. Simulation CCs

Eight CCs, recomputed in a single pass over the world **once per generation** and sent with
`midi_write_cc(...)`. Sending on generation edges rather than sequence ticks *is* the throttle.

| CC | Derived from |
|---|---|
| density | live cells / 256 |
| births | cells born this generation |
| deaths | cells died this generation |
| stability | cells unchanged since last generation |
| randomness ×4 | per voice: how far its selection deviated from its previous row |

Values are only transmitted when they change, to keep the MIDI bus quiet on a static world.

---

## 7. Liveness

Pure Conway on a 16×16 torus settles into still-lifes and blinkers within roughly 100–200
generations. When the palette freezes, four playheads walking it become a static loop — so
**auto-respawn is not a nicety, it is what keeps the instrument alive.**

Two settings: a **density floor** and a **respawn amount**. When live population drops below the
floor, *or* `stability` stays at 100% for N generations, sprinkle `respawn_amount` new live cells
at random empty positions.

Hand-painting cells is always available and is the primary way to disturb a settled world
deliberately. Selectable rulesets, cell age/decay and golden-ratio note values are **cut**.

---

## 8. Layout and interaction

All 256 pads are the world. `(15,15)` is drawn as the modifier badge instead of its cell — the
cell still simulates, it just cannot be seen or painted. That is the entire cost of having an
on-grid modifier while keeping a full-size world.

- **Tap a pad** → toggle that cell.
- **Hold `(15,15)`** → the world dims and 16 action pads overlay it: 4 voice mutes, 4 voice
  solos, clear, respawn-now, freeze-life, step-one-generation, transport. Release to return.
- **Playheads** draw as a tinted column per voice with a bright flash on the chosen cell. Four
  hues over white cells.

**Side buttons are untouched.** `llm.txt` is explicit that they are system territory: right pair
change page, left pair adjust the current value, on every faceplate.

### Widget discipline

Per `SYSTEM_NOTES.md` §4, which was learned expensively on `plinky-ambiotica`:

- The corner modifier is a `shift_button`, emitted **once per frame** into a member, read
  everywhere else from that member.
- It is `NOT_ISOLATED` — a modifier exists to be held while another pad is tapped, so crowding
  *is* the gesture.
- It is emitted **before** anything that tests it. Immediate mode is order-dependent.
- No raw touch comparisons anywhere.

### Pages

**Everything draws at absolute `y` 0..15.** `llm.txt` documents panel pages as living at
`y=16`, `y=32` on a taller scrolling surface; `SYSTEM_NOTES.md` §4 records that exact mechanism
causing a page-scroll touch bug on `plinky-ambiotica`, where a held finger swept through other
pages' pads mid-animation. We follow `SYSTEM_NOTES.md`. Settings pages are unaffected —
`draw_system_style_settings_page(...)` draws itself.

### Settings pages

Rendered with `draw_system_style_settings_page(...)` / `_bool_` / `_enum_` variants, which
return the left-button edit delta directly.

Global: key · scale · octave span · generation rate · respawn floor · respawn amount · MIDI ports.
Per voice (×4): enable · rate · traversal order · selection rule · pitch offset · note length.

---

## 9. Concurrency

`on_ui()` paints cells while `on_sequence()` reads the same array from IRQ context. `llm.txt`
names this hazard and supplies the fix: **every world mutation is wrapped in
`on_sequence_lock_guard_t`.**

Generation stepping happens inside `on_sequence()` and needs no guard. The guard covers touch
edits, clear, respawn, and the modifier-layer actions.

---

## 10. Memory

| Item | Size |
|---|---|
| `world[16][16]` | 256 B |
| generation scratch buffer | 256 B |
| 4 × voice state | < 1 KB |
| 5 × `clock_divider_t` + 4 × `traversal_state_t` | < 200 B |
| scale table | flash (free) |

**No PSRAM. No heap. Nothing near the 128 KB arena limit.** Stack usage stays under the ~200
byte guidance by keeping the generation pass operating on members rather than locals; the only
sizeable local anywhere is `selection_collect`'s `int rows[16]` at 64 bytes.

(Worth noting that `playhead_t` would have carried `skip_steps[MAX_STEPS/32]` = 128 bytes each
regardless of the 16 steps we use. Not a problem at this budget, but `traversal_state_t` is 8.)

---

## 11. Components

Deliberately split so the musical logic is testable without any Plinky API:

| Unit | Responsibility | Depends on |
|---|---|---|
| `life.h` | toroidal Conway step, population stats, respawn trigger | nothing |
| `selection.h` | the 11 selection rules over a column | nothing |
| `scales.h` | scale table, degree → note | `scale_t` only |
| `voice.h` | note lifecycle, velocity, length countdown | abstract note event |
| `panel.cpp` | clocks, playheads, sinks, draw, touch, settings | all of the above + Plinky API |

The first four are pure functions of plain data. `panel.cpp` is glue.

---

## 12. Testing

The same shape as the `plinky-ambiotica` desktop harness: `life.h`, `selection.h` and `scales.h`
compile directly into a native test binary, no hardware and no Plinky API involved.

- a glider translates correctly and **wraps** the torus
- a blinker oscillates with period 2; a block is stable
- each of the 11 selection rules, against hand-built columns
- an **empty column produces a rest**, for every rule
- retained-state rules survive the live-cell set shrinking under them
- the respawn trigger fires on a stalled world and not on a live one
- every scale mask has bit 0 set and the right popcount

On-device verification is a separate pass: playhead colour legibility at playing distance, and
no stuck MIDI notes across mute / rate-change / transport-stop / panel-unload.

Per `SYSTEM_NOTES.md` §6b: flash via `plinky12.com`, **not** staging, or `printf` output will
silently never appear.

---

## 13. Cut from v1

Per-step holds / ties / accents / ratchets / conditional triggers · selectable CA rulesets ·
cell age and decay · golden-ratio note values · preset sharing · per-voice loop bounds ·
8×8 world mode.

---

## 14. Deviations made during implementation

**1. Degree → note is ours, not `shift_note_along_scale(...)`.**
`scale_t`, `scale_from_bitmask(...)` and `shift_note_along_scale(...)` are *called* inside the
published header bundle (`fill_scale_string_roots`) but *declared* in neither `llm.txt` nor
`plinky_library.h`. Their parameter order is therefore unverifiable from the published API.
`life_degree_to_note(degree, root, mask)` in `scales.h` does the mapping in ~15 lines, has no
undeclared dependency, and is desktop-tested. Scales still drive the system globals via
`set_current_key_and_scale(...)`, so nothing about §4's intent changed.

**2. Column traversal is ours, not `playhead_t`.**
`llm.txt` documents `playhead_t::update()` as taking "`clock_divider_t::update() > 0`" while
declaring it as `update(int num_steps, ...)`. Those two contracts disagree and panel code cannot
be compiled locally to settle which is right. `traversal.h` implements FORWARD / REVERSE /
PINGPONG / RANDOM in ~20 lines, is desktop-tested, and removes the risk entirely. `playhead_t`'s
loop bounds were already cut from v1, so the only thing given up is its shuffle mode.

Both files are pure and tested, so if either API is clarified later, swapping back is a local
change with tests already in place to confirm the behaviour is unchanged.

**Also added, not in the original design:** `harness/plinky_stubs.h` plus
`harness/compile_check.sh`. A single-file panel with no local toolchain otherwise only surfaces
type errors by flashing. The stubs are transcribed from the published API dump, so passing the
check proves the panel is self-consistent with the documented surface — not that it is correct
on hardware.

## 15. Open questions

- Whether `shift_note_along_scale(...)` and `playhead_t::update(...)` behave as §14 assumes.
  Both were routed around rather than guessed at; if the maintainers clarify either, swapping
  back is a local change with tests already in place.
- Whether four column tints remain distinguishable over a dense white world at 5-bit colour.
  This is the main design risk and can only be settled on hardware.
- Whether the corner-pad modifier feels natural in practice, or whether a two-pad corner
  (`(15,15)` + `(14,15)`) reads better.
