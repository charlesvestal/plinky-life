# Bug: faceplate detection returns 0 on a Chords unit

## Symptom
`plate_is_printed()` is false on a Plinky 12 with a Chords faceplate, so every page
lays out for a blank plate: voice selector missing from row 0, the editor's own VOICE
row appears on row 1, the scene picker sits 2 rows high, the play surface uses rows
0-14 instead of 2-13.

## Evidence from the device (not inference)
- Current build logs, every 3s, from on_ui:
  `life: plate boot=0 live=0 saved=0 used=0 printed=0`
  boot   = get_frontpanel_code() from the member initialiser at construction
  live   = get_frontpanel_code() called in on_ui this frame
- EARLIER in development the SAME panel on the SAME unit logged:
  `life: frontpanel now=0 at_construct=32 orient=1 chords=1 drums=0 -> Chords`
  i.e. construction returned 32 (CHORDS) then, and on_ui returned 0.
- The stock Grid panel on this unit shows `yofs = +2` on its settings page. grid.cpp
  derives that from get_frontpanel_code() at construction, BUT it also persists
  y_offset via FIELD, so +2 might be a restored setting rather than live detection.
  This has NOT been distinguished.
- In the web simulator (stage.plinky12.com/ide.html, Chords selected) the same build
  logs `boot=32 live=32` - detection works there, so the simulator cannot reproduce it.

## The code
- Panel: /Volumes/ExtFS/charlesvestal/github/plinky-life/src/panel.cpp
  `struct life : panel_t` at ~line 263
  `const int plate_boot = get_frontpanel_code();` at ~line 384 (member initialiser)
  `plate_code()` / `plate_is_printed()` at ~line 2700
  on_ui seeding at ~line 3148
- Reference incl. full grid.cpp source: llm.txt from
  https://plinky12.com/docs/ide_api/llm.txt  (basic auth p12code / jollygood)
  grid.cpp starts ~line 7100; its member is line 7128; FIELD("y_offset") ~7185;
  the yofs settings page ~7252.

## Ruled out
- Reading it per-frame from on_ui: returns 0 on this unit.
- Latching the last non-zero live reading: nothing non-zero ever arrives.
- Carrying the value in settings: nothing is ever read to carry.
- The settings key "plate" colliding with an older enum: fixed, key is now "fpcode".

## Untested hypotheses worth checking
- Does struct position matter? grid.cpp's member is the 2nd in its struct; ours is
  ~120 members in. Member initialisers run in declaration order.
- WHEN is each panel constructed? A staged load constructs with placement-new at load
  time, not at boot. If the straps are only readable early, a panel constructed during
  a load reads 0. Does grid get constructed at a different moment?
- Does panel struct SIZE matter? This panel embeds two file_picker_t (~3.6KB each) in
  a 128KB arena. Check llm.txt for any documented consequence.
- Is grid's +2 actually live detection, or a restored setting? Deleting grid's settings
  file and reloading it would distinguish these.

## Constraint from the user
Do NOT solve this by adding a manual override setting. It has been added and removed
twice. The panel should detect the faceplate.
