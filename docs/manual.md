# Life — manual

A Game of Life sequencer for the Plinky 12.

The 16×16 grid is a **living palette**, not a piano roll. **Columns are steps,
rows are scale degrees.** A Game of Life world rewrites the whole grid
underneath you on its own clock, and four independent voices walk through it at
their own speeds, playing whatever they find alive.

**An empty column is a rest.** That single rule is what turns the shape of the
world into rhythm rather than a wall of notes.

---

## Quick start

1. **Press `(15,15)`** — the green ▶ in the bottom-right corner. That's play.
2. Listen. A newly loaded panel seeds itself, so it makes sound straight away.
3. **Tap pads** to bring cells to life and disturb it.
4. **Tap `(13,15)`** — the magenta `×` — for mutes, solos, and the voice editors.

Nothing moves while transport is stopped: no generations, no notes. The one
exception is the step pad `(3,15)`, so you can walk the world forward by hand.

---

## 1. The world

![World view](img/world.svg)

Every pad is a cell. Tap to toggle it alive or dead.

Live cells breed and die by Conway's rules — a cell with two or three live
neighbours survives, an empty space with exactly three neighbours comes to life.
**The grid wraps at the edges**, so patterns that drift off one side reappear on
the other and keep going.

### Row 15

The bottom-right corner follows the same layout as Chords, so the pads are where
you already expect them:

| Pad | |
|---|---|
| `(12,15)` | unused — still part of the world |
| `(13,15)` | `×` — actions. Hold or tap. |
| `(14,15)` | ■ stop |
| `(15,15)` | ▶ play |

**Transport works the same in every mode.** It's never hidden behind a modifier
and never changes meaning.

Three cells are given up to those pads. They still live and die with the rest of
the world — you just can't see or paint them.

**On the Chords and Drums overlays** the world still uses all 16 rows, so the top
and bottom rows sit outside the printed pad circles. That's intentional: rows are
pitch, and cropping them would cost you two scale degrees at each end. The sound
page does line up with the printed labels.

### Reading the playheads

Each voice tints the column it is standing on, in its own colour:

| | Voice |
|---|---|
| 🔴 | 1 |
| 🟢 | 2 |
| 🔵 | 3 |
| 🟡 | 4 |

The dim tint shows where a voice **is**. A cell flashing bright is what it just
**played**.

---

## 2. Actions

![Action layer](img/action.svg)

Tap `×`, or hold it — either works. The world dims underneath so you don't lose
your place.

The grid can't show text, so these are coloured pads rather than labels. Position
and colour are what you learn: each voice's own colour on rows 13–14, red for
destructive, cyan for the world controls.

| Pads | | |
|---|---|---|
| `(0,13)`–`(3,13)` | edit | open that voice's editor |
| `(0,14)`–`(3,14)` | mute | lit means you'll hear that voice |
| `(4,14)`–`(7,14)` | solo | silences the other three |
| `(0,15)` | clear | every cell dies — the red one |
| `(1,15)` | seed | sprinkle new cells in right now |
| `(2,15)` | freeze | stop the world changing; glows red while frozen |
| `(3,15)` | step | advance exactly one generation |

**Any action drops you back to the world.** The four edit pads open the voice
editor instead.

**Freeze and step are the composing tools.** Freeze holds the palette still so
you can write against a fixed set of cells; step nudges it forward one generation
at a time so you can hunt for a shape you like. Step works even while stopped.

---

## 3. The voice editor

![Voice editor](img/voice.svg)

`×` → one of the four edit pads.

One setting per row, with blank rows between them so it stays readable at arm's
length. **The bright pad is the current value**; the dim ones show how far the
range goes.

| Row | | |
|---|---|---|
| 1 | **voice** | which voice you're editing — tap to switch |
| 3 | **rate** | `32nd · 16T · 16th · 8T · 8th · 4T · 1/4 · 1/2 · 1BAR · 2BAR · 4BAR · 8BAR` |
| 5 | **rule** | which cell it picks — the 11 rules below |
| 7 | **order** | forward · reverse · ping-pong · random |
| 9 | **channel** | MIDI channel 1–16 |
| 11 | **pitch** | −7 … +7 scale degrees; the dim centre pad is 0 |
| 13 | **length** | 10% … 100% of the step |

And on row 15:

| Pad | |
|---|---|
| `(0,15)` | **load** a preset into this voice |
| `(1,15)` | **edit** this voice's sound |

Press `×` to go back to the world.

Touching any pad describes it on the second screen, so you can read what a rule
does before committing to it.

### Rate is where the groove comes from

All four voices cross all 16 columns, so **they only sound polyrhythmic if their
speeds differ.** The defaults are 8th, quarter-triplet, quarter and half for
exactly that reason. Set all four the same and you get four voices marching in
step, which is rarely what you want.

### Voice 1 plays preset 1

Four voices, four sounds, straight across. To use a different patch, **load** it
into that voice's slot.

---

## 4. Sound

Voice editor → `(1,15)`.

The familiar Plinky synth editor, pointed at the selected voice.

**It rearranges itself to match your faceplate.** You don't have to tell it which
overlay you have — it knows, and the second screen names the layout you're
looking at.

### On Chords and Drums

![Sound page on Chords and Drums](img/sound_printed.svg)

Both overlays print the same synth page, and the sliders move onto it — so every
pad sits under the label that names what it does.

| Area | |
|---|---|
| rows 2–6 | A · D · S · R · PAN · SUB · HP · LP · RESO · DELAY · TIME · FBK · VERB · TAIL · GLOW · VOL |
| rows 7–13, `x0–7` | GLIDE · PITCH · OCT · CHORUS · FOLD · START · END · SPEED |
| `x8`, rows 7–13 | the MOD and XY buttons |
| `x9–15`, rows 7–13 | the XY pad |
| `(0,0)`–`(3,0)` | switch voice — the editor follows |
| `(0,14)` | back to the voice editor |

### On Blocks and Toadstep

![Sound page, standard layout](img/sound.svg)

| Area | |
|---|---|
| rows 0–4 | 16 sliders: effects sends and the mix |
| rows 5–9 | 16 sliders: the main synth parameters |
| `x8–15`, rows 10–14 | the XY pad, with LFO and envelope buttons down its right edge |
| `(0,10)`–`(3,10)` | switch voice — the editor follows |
| `(0,12)`–`(5,12)` | simple · tune · chop · loop · sync · lowpass gate |
| `(0,14)` | back to the voice editor |

The column order here already matches Toadstep's printed faders. Blocks prints no
labels at all.

### Loading a preset

Voice editor → `(0,15)` opens the preset picker: folders on the left, slots on
the right, with cancel and OK at the bottom right.

**This is the one place transport isn't available** — the picker needs those two
pads for its own buttons. `×` still gets you out.

---

## 5. The rules

Two separate things, and it's worth keeping them apart:

- **order** — *which column* a voice moves to next.
- **rule** — *which live cell in that column* actually sounds.

Given the live cells in the column a voice has arrived at:

| Rule | Plays |
|---|---|
| `FRST` | the topmost live cell — the highest note |
| `LAST` | the bottommost — the lowest note |
| `UP` | climbs through the live cells, one per step |
| `DOWN` | descends through them |
| `UPDN` | climbs then falls back, without repeating the ends |
| `DNUP` | falls then climbs back |
| `RAND` | any live cell, evenly |
| `WALK` | the one closest in pitch to the last note — smooth lines |
| `RISE` | the next one above the last note, wrapping to the bottom |
| `FALL` | the next one below, wrapping to the top |
| `ALL` | every live cell at once — **the only rule that makes chords** |

`WALK` gives you melodies. `RAND` gives you sparkle. `ALL` gives you pads. Mixing
them across the four voices is most of the instrument.

### How many notes at once

Every rule but `ALL` plays **one note per step**, so four voices give you up to
four notes. `ALL` plays the whole column.

The synth has eight notes to share. Each voice you can hear is guaranteed one of
them, so a chord can never cut off a melody — and whatever is spare goes to the
voices playing `ALL`. Mute the other three and a single `ALL` voice gets all
eight to itself.

Sending to MIDI only? Then there's no ceiling and chords go out whole.

**Loud cells:** a cell surrounded by neighbours hits harder than a lone one, so
dense clusters accent themselves.

---

## 6. Settings

Right side buttons page through them, left side buttons change the value. The
second screen explains each page as you land on it.

| | | |
|---|---|---|
| `KEY ` | root note | sets the whole instrument's key |
| `SCAL` | scale | 29 of them |
| `OCT ` | base octave | 1–7 |
| `GEN ` | generation rate | how often the world evolves |
| `FLOR` | respawn floor | sprinkle cells in below this population |
| `SEED` | respawn amount | how many to sprinkle |
| `STAB` | stall limit | generations of no change that count as stuck |
| `OUT ` | output | synth · MIDI · both |
| `PORT` | MIDI ports | off · USB1 · TRS1 · port 1 · all |
| `CC  ` | simulation CCs out | on/off — see below |
| `CIN ` | CC control | off, or the first controller number |
| `COUT` | CC mirror | send changes back out on the same controllers |

Key and scale set the **whole instrument's** harmony, not just this panel's, so
the rest of the Plinky follows along.



---

## 7. Saving

Press the right side button **down** from the world to reach the scene page:
folders on the left, slots on the right, cancel and OK bottom right.

A scene holds **the world itself** — every live cell — plus every voice's rate,
rule, order, channel, pitch, length and mute, and the generation rate and
respawn settings. Load one back and you get the same world and the same four
voices.

The settings pages are separate: key, scale, octave, output, ports and CCs are
preferences, saved automatically as you change them and shared by every scene.

---

## 8. Keeping it alive

Left alone, a Game of Life world eventually settles into shapes that never
change — and a frozen palette means four playheads walking a loop that never
varies. So Life watches for that and sprinkles new cells in.

- `FLOR` — top the world up when the population drops below this
- `SEED` — how many cells to add
- `STAB` — how many generations of *nothing at all changing* count as stuck

Blinking patterns still count as alive, so a world that's oscillating is left
alone. It's still making music.

Set `FLOR` and `STAB` to 0 if you'd rather it be allowed to die.

---

## 9. Output

Each voice has its own **sound** and its own **MIDI channel**, defaulting to
channels 1–4. `OUT` chooses the internal synth, MIDI, or both.

### Controlling Life from a DAW

With `CIN` set — 30 by default — a block of 35 controllers on the system MIDI
channel drives the panel. Counting up from the base:

| Offset | |
|---|---|
| +0 | key |
| +1 | scale |
| +2 | octave |
| +3 | generation rate |
| +4 | respawn floor |
| +5 | respawn amount |
| +6 | stall limit |
| +7 | clear the world |
| +8 | seed now |
| +9 | freeze |
| +10 | step one generation |
| +11 … +14 | mute, voices 1–4 |
| +15 … +18 | rate, voices 1–4 |
| +19 … +22 | rule, voices 1–4 |
| +23 … +26 | order, voices 1–4 |
| +27 … +30 | pitch, voices 1–4 |
| +31 … +34 | length, voices 1–4 |

The per-voice controls are grouped **by parameter**, so a row of four knobs sets
the same thing on all four voices.

Clear, seed and step are momentary — they fire once when the value crosses
halfway going up, so a button that sends 127 then 0 fires once.

With `COUT` on it works both ways: change something on the grid and the same
controller goes back out, so a DAW or a controller with motorised faders stays
in step. You can leave both on with everything on one channel — an echo of what
Life sent decodes to the value it already has, so it changes nothing and sends
nothing back.

### Simulation CCs out

With `CC` on, Life sends eight controllers describing the world itself, once per
generation — modulation taken from the world's own behaviour:

| CC | |
|---|---|
| 20 | how full the world is |
| 21 | births this generation |
| 22 | deaths this generation |
| 23 | how still it is |
| 24–27 | how far each voice's last note moved |

They're only sent when a value actually changes, so a settled world goes quiet
rather than flooding the bus.

---

## 10. If something seems wrong

**Nothing plays.** Check transport is running — `(15,15)` glows bright green.
Nothing moves while stopped.

**Still nothing.** Check `OUT` and `PORT`. `OUT` on synth sends no MIDI; `PORT`
off sends none either.

**One voice is silent.** Check its mute on row 14, and check whether another
voice is soloed — a solo anywhere silences the rest.

**It went static.** Either freeze `(2,15)` is on — it glows red — or the world
got stuck and `FLOR`/`STAB` are too low to rescue it. Seed `(1,15)` adds cells
immediately.

**It's too busy.** Lower `SEED`, slow `GEN` down, or mute a voice. Voices set to
`ALL` play chords, and four of those at once is a lot.

**It sounds like one voice.** Give the four different **rates**. Same rate means
same rhythm.
