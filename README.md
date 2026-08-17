# plinky-life

A Game of Life sequencer panel for the [Plinky 12](https://plinky12.com), in the spirit of
[ZOA](https://apps.apple.com/us/app/zoa-living-midi-sequencer/id1581881354).

The 16×16 grid is a living palette, not a piano roll: **columns are steps, rows are scale
degrees**, and a Conway world rewrites the whole grid underneath you on its own clock. Four
independent voices walk through it at their own speeds, playing whatever they find alive.

**An empty column is a rest** — which is what turns the shape of the world into rhythm.

**→ [Manual](docs/manual.md)** — how to play it, with pad maps of every mode.

## The panel

**[`life.cpp`](https://raw.githubusercontent.com/charlesvestal/plinky-life/main/life.cpp)** — load this into the Plinky IDE.

## Building

```sh
sh tests.sh                  # everything checkable without hardware
sh harness/build.sh          # desktop tests for the pure logic
sh harness/compile_check.sh  # amalgamate, then type-check against stubbed headers
sh build/amalgamate.sh       # just produce life.cpp
python3 docs/make_maps.py    # regenerate the manual's pad maps
```

`src/panel.cpp` does not compile on its own — panel code can't use `#include`, and it only
type-checks once the IDE injects the SDK headers. `amalgamate.sh` splices the pure headers in
ahead of it to produce the single `life.cpp` that gets flashed.

`harness/plinky_stubs.h` is a transcription of the published API, not the SDK. It exists so the
generated file can be type-checked locally instead of flash-and-see. **If it and the real
firmware disagree, the firmware is right** — it has been wrong at least once, and the resulting
errors only showed up server-side.

## Layout

```
src/life.h        Conway world, population stats, respawn trigger
src/selection.h   the 11 selection rules over a column
src/traversal.h   column order: forward, reverse, ping-pong, random
src/scales.h      29 scale masks, degree -> note
src/voice.h       note lifecycle, polyphony budget, one exit path
src/ccmap.h       the MIDI CC block, in and out, and its value scaling
src/chance.h      conditional triggers derived from the world
src/panel.cpp     clocks, output, drawing, touch, settings pages

harness/          desktop tests + stubbed headers for the compile check
build/            amalgamation into life.cpp
docs/             manual, generated pad maps, design spec
```

The seven headers are pure functions of plain data with no Plinky API in them, which is why they
run natively — the musical logic is proved on a laptop and `panel.cpp` is thin glue. Over 34,000
assertions cover the world, the selection rules, the scale tables and the note lifecycle.

The pad maps in the manual are generated from the same layout table the panel uses, so they can't
drift from what the pads actually do.

## Design notes

`docs/superpowers/specs/` holds the design spec, including the deviations made during
implementation and why.

`SYSTEM_NOTES.md` is the working reference for Plinky 12 panel development generally — hardware,
execution model, memory, and the gotchas that cost real debugging time. Not specific to this
panel.

## Licence

MIT, see [`LICENSE`](LICENSE).

## Credits

Conway's Game of Life, by way of ZOA's idea of using it as a musical palette rather than a
visualisation.

## How this was built

This panel was built with help from coding agents like Claude, but with significant design,
oversight and hours from a human (me). If that's not to your taste, totally fine!
