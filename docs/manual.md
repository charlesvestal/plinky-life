# plinky-life — manual

A Game of Life sequencer for the Plinky 12.

The 16×16 grid is a **living palette**, not a piano roll. **Columns are steps, rows
are scale degrees.** A toroidal Conway automaton rewrites the whole thing
underneath you on its own clock, and four independent voices walk through it at
their own rates, playing whatever they find alive.

**An empty column is a rest.** That single rule is what turns the shape of the
automaton into rhythm rather than a wall of notes.

> The pad maps below are generated from the same layout table the panel uses
> (`python3 docs/make_maps.py`). A manual that drifts from the panel is worse
> than none, and the drift is invisible until you press the wrong pad.

---

## Quick start

1. **Press `(15,15)`** — the green ▶ in the bottom-right corner. That's play.
2. Listen. A freshly flashed panel seeds itself with 60 cells, so it makes sound
   straight away. Loading a saved scene restores that scene's world instead.
3. **Tap pads** to bring cells to life and disturb it.
4. **Tap `(13,15)`** — the magenta `×` — for mutes, solos, clear, and the voice
   editors.

Nothing advances while transport is stopped. No generations, no notes. The one
exception is the step pad `(3,15)`, so you can walk the world forward by hand.

---

## 1. The world

![World view](img/world.svg)

Every pad is a cell. Tap to toggle it alive or dead.

**Row 15 follows the Chords silkscreen**, which is also the convention in
`ide_api.md`'s Global Transport section — so the pads are where a Plinky player
already expects them:

| Pad | |
|---|---|
| `(12,15)` | *rec* on Chords. We have nothing to record, so it stays part of the world. |
| `(13,15)` | `×` — the actions modifier. Hold **or** tap. |
| `(14,15)` | ■ stop |
| `(15,15)` | ▶ play |

**Transport is permanent and identical in every mode.** It is never modal and
never hidden behind a modifier. Three cells are spent on it; they still
*simulate*, they just can't be seen or hand-painted.

### Reading the playheads

Each voice tints the column it is standing on, in its own colour:

| | Voice | |
|---|---|---|
| 🔴 | 1 | red |
| 🟢 | 2 | green |
| 🔵 | 3 | blue |
| 🟡 | 4 | yellow |

A live cell that a voice actually **picks** flashes at full brightness. So the
dim tint tells you where a voice *is*, and the bright flash tells you what it
*played*.

---

## 2. The action layer

![Action layer](img/action.svg)

Tap `×` — or hold it, either works. The press edge latches the layer open, so it
stays up after you let go. The world dims underneath so you never lose your
place.

The grid cannot show text, so on the device these are **coloured pads, not
labels** — the names below are the diagram's, and position is what you actually
learn. Colour is the cue: each voice's own colour on rows 13–14, red for destroy,
cyan for the world controls.

| Pads | | |
|---|---|---|
| `(0,13)`–`(3,13)` | edit | open that voice's editor |
| `(0,14)`–`(3,14)` | mute | lit = you'll hear that voice |
| `(4,14)`–`(7,14)` | solo | |
| `(0,15)` | clear | wipe the world — the red one |
| `(1,15)` | seed | sprinkle new cells right now |
| `(2,15)` | freeze | stop the palette changing; turns red while frozen |
| `(3,15)` | step | advance exactly one generation |

**Any action drops you back to the world.** The four edit pads open the voice
editor instead.

**Freeze and step are the composing tools:** freeze the palette to write against
a fixed set of cells, or step it forward one generation at a time to hunt for a
shape you like. Step works while transport is stopped.

---

## 3. The voice editor

![Voice editor](img/voice.svg)

`×` → `E1`–`E4`. One parameter per row, with blank rows between them — seven
packed rows are unreadable at arm's length, alternating rows aren't.

**Selected pad bright, the rest of the row dim** so you can see how far the range
goes.

| Row | Parameter | Range |
|---|---|---|
| 1 | **VOICE** | which voice you're editing — tap to switch |
| 3 | **RATE** | `32nd · 16T · 16th · 8T · 8th · 4T · 1/4 · 1/2 · 1BAR · 2BAR · 4BAR · 8BAR` |
| 5 | **RULE** | the 11 selection rules (below) |
| 7 | **ORDER** | `FWD · REV · PING · RAND` |
| 9 | **CHAN** | MIDI channel 1–16 |
| 11 | **PITCH** | −7 … +7 scale degrees; the centre pad is 0 and stays visible |
| 13 | **LENGTH** | 10% … 100% of that voice's step |

Row 15 carries the two sound pads:

| Pad | |
|---|---|
| `(0,15)` | **LOAD** — pick a preset from the bank into this voice |
| `(1,15)` | **SOUND** — edit this voice's sound |

Press `×` to go back to the world.

### There is no preset selector

**Voice N plays preset N.** Four playheads, four presets, direct mapping — an
indirection between them only earns you the question *"which preset is voice 3
on again?"*.

Presets 5–12 aren't lost: **LOAD** puts any preset from the bank into this
voice's slot.

### RATE is where the polyrhythm lives

All four voices span all 16 columns, so **they only sound polyrhythmic if their
rates differ.** Defaults are 8th, quarter-triplet, quarter and half for exactly
this reason. Setting all four to the same rate gives you four voices marching in
lockstep, which is almost never what you want.

---

## 4. Editing a voice's sound

![Hosted synth editor](img/sound.svg)

Voice editor → `(1,15)`.

**This is Plinky's own preset editor, not ours.** `preset_pages_t` takes a preset
index, so handing it the selected voice's preset makes it edit that voice's
sound. Every slider, the XY pad, the LFO and envelope buttons and the flag
buttons are the firmware's.

| Area | |
|---|---|
| rows 0–4 | 16 sliders: fx sends and the mix parameters |
| rows 5–9 | 16 sliders: the main synth parameters |
| `x8–15`, rows 10–14 | the synth XY pad, LFO and env buttons down its right edge |
| `(0,10)`–`(3,10)` | switch voice — the editor follows, without leaving |
| `(0,12)`–`(5,12)` | simple · tune · chop · loop · sync · lowpass gate |
| `(0,14)` | back to the voice editor |

`×` returns to the world from here, as everywhere.

### Loading a preset

Voice editor → `(0,15)` opens the stock picker: folders on the left, 64 slots on
the right, with its own cancel and OK buttons.

**This is the one mode with no transport pads.** The picker puts its buttons at
`(14,15)` and `(15,15)` — exactly where stop and play live — so it owns row 15
while it's up. That's the right call for a modal dialog, and `×` still escapes
because the picker leaves `(13,15)` alone.

`ide_api.md` puts the principle well: *"start from the largest matching helper and
only drop down a layer for the parts that are genuinely custom."* Routing a
playhead is custom. Editing and loading presets is not.

---

## 5. The selection rules

Two separate ideas, and keeping them apart is what makes the panel describable:

- **Traversal** (`ORDER`) — *which column* a voice is on.
- **Selection** (`RULE`) — *which live cell in that column* actually sounds.

Given the live cells in a voice's current column:

| Rule | Picks |
|---|---|
| `FRST` | topmost live cell (highest pitch) |
| `LAST` | bottommost live cell (lowest pitch) |
| `UP` | cycles through them, ascending in pitch |
| `DOWN` | cycles through them, descending |
| `UPDN` | ping-pongs, ascending first, endpoints not repeated |
| `DNUP` | ping-pongs, descending first |
| `RAND` | uniform pick |
| `WALK` | the live cell nearest in pitch to the previous note |
| `RISE` | nearest live cell strictly above the previous, wrapping |
| `FALL` | nearest live cell strictly below the previous, wrapping |
| `ALL` | **every** live cell at once — the only rule that makes chords |

`ALL` is not from ZOA. It's here because it's the only way this panel produces
harmony rather than four independent monophonic lines.

Rules that remember something (`UP`, `DOWN`, `UPDN`, `DNUP`, `WALK`, `RISE`,
`FALL`) survive the column changing size underneath them — which it constantly
does, because the automaton is rewriting it between ticks.

**Velocity comes from the automaton too:** a cell in a crowded neighbourhood hits
harder than a lone one.

---

## 6. Settings pages

Right side buttons page through them, left side buttons change the value. Both
pairs are system territory on every faceplate, so the panel never touches them.

Ten pages, all **global** — per-voice config lives on the grid.

| | | |
|---|---|---|
| `KEY ` | root note | C … B |
| `SCAL` | scale | 29 of them |
| `OCT ` | base octave | 1–7 |
| `GEN ` | **regeneration speed** | how often the world evolves one generation |
| `FLOR` | respawn floor | sprinkle new cells below this population |
| `SEED` | respawn amount | how many to sprinkle |
| `STAB` | stall limit | generations of *no change at all* that count as stalled |
| `OUT ` | output | `SYN` · `MIDI` · `BOTH` |
| `PORT` | MIDI ports | `OFF` · `USB1` · `TRS1` · `P1` · `ALL` |
| `SIM ` | simulation CCs | on/off |

### Scales drive the whole instrument

`KEY` and `SCAL` call `set_current_key_and_scale(...)`, which sets Plinky's
*global* harmonic state rather than a private copy. Change the scale here and the
rest of the instrument follows.

---

## 7. Keeping the world alive

Pure Conway on a 16×16 torus settles into still-lifes and blinkers within roughly
100–200 generations. A frozen palette means four playheads walking a static loop,
so **auto-respawn is load-bearing, not a nicety.**

- `FLOR` — sprinkle when population drops below this
- `SEED` — how many cells to sprinkle
- `STAB` — how many generations of *no change at all* count as stalled

Oscillators keep births and deaths non-zero, so **a blinking world never counts
as stalled.** It's still making music, and interrupting it would be wrong.

The world wraps at the edges. That's what keeps gliders circulating instead of
smearing into a still-life at the border.

---

## 8. Output

Each voice picks its own **synth preset** (`SYNTH`) and its own **MIDI channel**
(`CHAN`), defaulting to preset *N* on channel *N+1* so all four are separate out
of the box. `OUT` chooses internal synth, MIDI, or both.

### Simulation CCs

Eight CCs derived from the automaton, recomputed once per generation and sent
only when a value changes — so a settled world goes quiet instead of flooding the
bus.

| CC | |
|---|---|
| 20 | population density |
| 21 | births this generation |
| 22 | deaths this generation |
| 23 | stability — cells unchanged |
| 24–27 | per-voice randomness: how far each voice's last pick moved |

Turn them off with `SIM`.

---

## 9. Troubleshooting

**Nothing plays.** Check transport is running — `(15,15)` lights bright green.
Nothing advances while stopped.

**Still nothing.** Check `OUT` and `PORT` on the settings pages. `OUT` at `SYN`
sends no MIDI; `PORT` at `OFF` sends none either.

**One voice is silent.** Check its mute on row 14 of the action layer, and check
whether another voice is soloed — a solo anywhere mutes everything else.

**It went static.** Either freeze `(2,15)` is on — it glows red — or the world
stalled and `FLOR`/`STAB` are too low to rescue it. Seed `(1,15)` sprinkles cells
immediately.

**It's too busy.** Lower `SEED`, slow `GEN` down, or mute a voice or two. Voices
set to `ALL` play chords, and four of those at once is a lot.

---

## Appendix: what the panel logs

`do_action()` prints the pad it received and the resulting transport state:

```
life: action pad (4,15)
life: transport now playing
```

Flash from **plinky12.com**, not `stage.plinky12.com` — `printf` and Device Logs
silently do nothing on staging.
