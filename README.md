# plinky-life

A Game of Life sequencer for the [Plinky 12](https://plinky12.com), in the spirit of
[ZOA](https://apps.apple.com/us/app/zoa-living-midi-sequencer/id1581881354).

The 16×16 grid is a living palette, not a piano roll. **Columns are steps, rows are scale
degrees**, and a toroidal Conway automaton rewrites the whole thing underneath you on its own
clock. Four independent voices walk through it at their own rates and play whatever they find
alive.

**An empty column is a rest.** That is what makes the shape of the automaton become the rhythm.

**→ [Read the manual](docs/manual.md)** for pad maps of every mode.

## Playing it

Row 15 follows the Chords silkscreen, which is also the convention in
`ide_api.md`'s Global Transport section — so the pads are where a Plinky player
already expects them:

```
(12,15) rec    (13,15) ×      (14,15) ▢      (15,15) ▷
 unused         actions        stop           play
```

**Press ▷ to start.** Transport is permanent and identical in every mode — never
modal, never hidden behind a modifier.

| | |
|---|---|
| **Tap a pad** | toggle that cell alive or dead |
| **`×` (13,15)** | hold or tap for the action layer |
| **Right side buttons** | page through the settings |
| **Left side buttons** | adjust the current setting |

The side buttons are system territory on every faceplate, so the panel never
touches them.

Three cells are spent on row 15. They still *simulate* — they just can't be seen
or hand-painted.

### The action layer

Hold or tap `×`:

```
row 13   E1 E2 E3 E4                    edit that voice
row 14   V1 V2 V3 V4   S1 S2 S3 S4      mutes, then solos
row 15   CLR SEED FRZ STEP              world
```

Any action drops you back to the world. `EDIT` opens the voice editor instead.

### The voice editor

One parameter per row, blank rows between them so seven stacked rows stay
readable at arm's length. Selected pad bright, the rest of the row dim so you
can see how far the range goes.

```
y=1    VOICE   which voice you are editing
y=3    RATE    32nd .. 8 bars          <- polyrhythm lives here
y=5    RULE    the 11 selection rules
y=7    ORDER   fwd / rev / ping / rand
y=9    SYNTH   which of the 12 presets this voice plays
       (14,9)  opens Plinky's own preset editor for it
y=11   CHAN    MIDI channel 1-16
y=13   PITCH   -7 .. +7 scale degrees, centre pad is 0
y=15   LENGTH  10% .. 100% of the step
```

Press `×` to get back to the world.

## Selection rules

Traversal is *which column* a voice is on. Selection is *which live cell in that column* sounds.
Every voice picks its own.

| Rule | Picks |
|---|---|
| `FRST` / `LAST` | topmost / bottommost live cell |
| `UP` / `DOWN` | cycles through the live cells, ascending / descending in pitch |
| `UPDN` / `DNUP` | ping-pongs through them, endpoints not repeated |
| `RAND` | uniform pick |
| `WALK` | the live cell nearest in pitch to the previous note |
| `RISE` / `FALL` | nearest live cell strictly above / below the previous, wrapping |
| `ALL` | every live cell at once — the only rule that makes chords |

`ALL` is not from ZOA. It's here because it's the only way the panel produces harmony rather
than four independent monophonic lines.

Velocity comes from the automaton too: a cell in a crowded neighbourhood hits harder than a
lone one.

## Output

Each voice picks its own synth preset (`SYNTH`) and its own MIDI channel (`CHAN`), defaulting to
preset *N* on channel *N+1* so the four are separate out of the box. `(14,9)` in the voice editor
opens Plinky's stock preset editor (`preset_pages_t`) for that voice's patch — sliders, XY pad and
flag buttons are all the firmware's, not ours. The `OUT` setting picks
internal synth, MIDI, or both.

MIDI is **level-triggered**: `declare_midi_note_for_preset_idx(...)` declares which notes should
be down each frame and `send_declared_midi_notes()` commits, so the runtime derives the on/off
traffic. There is no note-off to forget — mute, rate change, transport stop and sink changes all
release correctly without any of them knowing they had to.

### Simulation CCs

Eight CCs, recomputed once per generation and sent only when a value changes, so a settled
world goes quiet instead of flooding the bus.

| CC | Meaning |
|---|---|
| 20 | population density |
| 21 | births this generation |
| 22 | deaths this generation |
| 23 | stability (cells unchanged) |
| 24–27 | per-voice randomness — how far each voice's last pick moved |

## Scales

29 scales, stored as 12-bit root-relative bitmasks. Choosing one calls
`set_current_key_and_scale(...)`, so it drives **the whole instrument's** harmonic state rather
than keeping a private one — change scale here and the rest of the Plinky follows.

## Keeping the world alive

Pure Conway on a 16×16 torus settles into still-lifes and blinkers within roughly 100–200
generations, and a frozen palette means four playheads walking a static loop. Auto-respawn is
therefore load-bearing, not a nicety:

- **`FLOR`** — sprinkle new cells when the population falls below this
- **`SEED`** — how many cells to sprinkle
- **`STAB`** — how many generations of *no change at all* count as stalled

Oscillators keep births and deaths non-zero, so a blinking world never counts as stalled. It's
still making music.

## Building

```sh
sh harness/build.sh          # desktop tests for the pure logic
sh harness/compile_check.sh  # amalgamate, then type-check against stubbed headers
sh build/amalgamate.sh       # just produce plinky_life.cpp
```

`src/panel.cpp` does not compile on its own — panel code can't use `#include`, and it only
type-checks once the IDE injects the SDK headers. `amalgamate.sh` splices the pure headers in
ahead of it to produce the single `plinky_life.cpp` that gets flashed.

`harness/plinky_stubs.h` is a transcription of the published API, not the SDK. It exists so the
generated file can be type-checked locally instead of flash-and-see. **If it and the real
firmware disagree, the firmware is right.**

Flash via `plinky12.com`, **not** `stage.plinky12.com` — `printf` and Device Logs silently do
nothing on staging.

## Layout

```
src/life.h        toroidal Conway, population stats, respawn trigger
src/selection.h   the 11 selection rules over a column
src/traversal.h   column order: forward, reverse, ping-pong, random
src/scales.h      29 scale masks, degree -> note
src/voice.h       note lifecycle, one exit path so notes cannot stick
src/panel.cpp     clocks, sinks, drawing, touch, settings pages
```

The first five are pure functions of plain data with no Plinky API in them, which is why they
can be tested on a laptop. `panel.cpp` is glue.
