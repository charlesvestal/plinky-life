/* plinky-life - scale tables and degree->note mapping.

   Pure C. No Plinky API. Compiled directly by the desktop test harness.

   A scale is a 12-bit root-relative bitmask, matching Plinky's `current_scale`
   global: bit 0 is the root, bit n is n semitones above it. Every scale below
   therefore round-trips through `set_current_key_and_scale(...)`, so choosing a
   scale here drives the whole instrument's harmonic state rather than keeping a
   private one.

   We deliberately do NOT call `shift_note_along_scale(...)`. It is used inside
   the public header bundle but never declared there, so its exact signature is
   unverifiable from the published API. Doing the mapping here costs ~15 lines
   and keeps this file testable on the desktop. */

#define LIFE_NUM_SCALES 29

static const unsigned short life_scale_masks[LIFE_NUM_SCALES] = {
    0xFFF, /* Chromatic          0 1 2 3 4 5 6 7 8 9 10 11 */
    0xAB5, /* Major              0 2 4 5 7 9 11 */
    0x5AD, /* Natural Minor      0 2 3 5 7 8 10 */
    0x6AD, /* Dorian             0 2 3 5 7 9 10 */
    0x5AB, /* Phrygian           0 1 3 5 7 8 10 */
    0xAD5, /* Lydian             0 2 4 6 7 9 11 */
    0x6B5, /* Mixolydian         0 2 4 5 7 9 10 */
    0x56B, /* Locrian            0 1 3 5 6 8 10 */
    0x295, /* Major Pentatonic   0 2 4 7 9 */
    0x4A9, /* Minor Pentatonic   0 3 5 7 10 */
    0x4E9, /* Blues              0 3 5 6 7 10 */
    0x555, /* Whole Tone         0 2 4 6 8 10 */
    0x9AD, /* Harmonic Minor     0 2 3 5 7 8 11 */
    0xAAD, /* Melodic Minor      0 2 3 5 7 9 11 */
    0xB6D, /* Diminished W-H     0 2 3 5 6 8 9 11 */
    0x6DB, /* Diminished H-W     0 1 3 4 6 7 9 10 */
    0x5B3, /* Phrygian Dominant  0 1 4 5 7 8 10 */
    0x9B3, /* Double Harmonic    0 1 4 5 7 8 11 */
    0x9CD, /* Hungarian Minor    0 2 3 6 7 8 11 */
    0x18B, /* Pelog              0 1 3 7 8 */
    0x18D, /* Hirajoshi          0 2 3 7 8 */
    0x6D5, /* Lydian Dominant    0 2 4 6 7 9 10 */
    0x55B, /* Altered            0 1 3 4 6 8 10 */
    0x9AB, /* Neapolitan Minor   0 1 3 5 7 8 11 */
    0xAAB, /* Neapolitan Major   0 1 3 5 7 9 11 */
    0x4A3, /* In Sen             0 1 5 7 10 */
    0x463, /* Iwato              0 1 5 6 10 */
    0x28D, /* Kumoi              0 2 3 7 9 */
    0x4A5, /* Egyptian           0 2 5 7 10 */
};

/* 4-char labels for draw_system_style_enum_settings_page(...). */
static const char *const life_scale_names[LIFE_NUM_SCALES] = {
    "CHRM",
    "MAJ ",
    "MIN ",
    "DOR ",
    "PHRY",
    "LYD ",
    "MIX ",
    "LOC ",
    "MAJ5",
    "MIN5",
    "BLUE",
    "WHOL",
    "HMIN",
    "MMIN",
    "DIM ",
    "DIM2",
    "SPAN",
    "BHAI",
    "HUNG",
    "PELO",
    "HIRA",
    "LYDD",
    "ALTR",
    "NEAM",
    "NEAJ",
    "INSN",
    "IWAT",
    "KUMO",
    "EGYP",
};

static const char *const life_scale_long_names[LIFE_NUM_SCALES] = {
    "Chromatic",
    "Major",
    "Natural Minor",
    "Dorian",
    "Phrygian",
    "Lydian",
    "Mixolydian",
    "Locrian",
    "Major Pentatonic",
    "Minor Pentatonic",
    "Blues",
    "Whole Tone",
    "Harmonic Minor",
    "Melodic Minor",
    "Diminished W-H",
    "Diminished H-W",
    "Phrygian Dominant",
    "Double Harmonic",
    "Hungarian Minor",
    "Pelog",
    "Hirajoshi",
    "Lydian Dominant",
    "Altered",
    "Neapolitan Minor",
    "Neapolitan Major",
    "In Sen",
    "Iwato",
    "Kumoi",
    "Egyptian",
};

static inline int life_popcount12(unsigned short mask) {
    int n = 0;
    for (int i = 0; i < 12; ++i)
        if (mask & (1u << i)) ++n;
    return n;
}

/* Semitone offset of the idx-th set bit, or -1. */
static inline int life_nth_degree_semitone(unsigned short mask, int idx) {
    for (int i = 0; i < 12; ++i) {
        if (mask & (1u << i)) {
            if (idx == 0) return i;
            --idx;
        }
    }
    return -1;
}

/* The inverse: which scale degree is nearest this MIDI note.

   Needed for note input - a played note has to land on a row, and rows are
   degrees. A note outside the scale snaps to the nearest degree rather than
   being dropped, so playing the black keys in a pentatonic still draws
   something instead of silently doing nothing. */
static inline int life_note_to_degree(int note, int root, unsigned short mask);

/* Map a scale degree to a MIDI note.

   `degree` may be negative or run past the top of the scale; it wraps into
   octaves. Returns a note clamped to 0..127. An empty mask falls back to
   chromatic so a corrupt save can never produce silence or a divide by zero. */
static inline int life_degree_to_note(int degree, int root, unsigned short mask) {
    if (mask == 0) mask = 0xFFF;
    int n = life_popcount12(mask);
    int octave = degree / n;
    int idx = degree % n;
    if (idx < 0) { idx += n; --octave; }  /* C truncates toward zero; we need floor */
    int note = root + octave * 12 + life_nth_degree_semitone(mask, idx);
    return note < 0 ? 0 : (note > 127 ? 127 : note);
}

static inline int life_note_to_degree(int note, int root, unsigned short mask) {
    if (mask == 0) mask = 0xFFF;
    int n = life_popcount12(mask);

    /* Start from the octave the note is in, then check the degrees around it -
       far cheaper than sweeping the whole range, and exact because
       life_degree_to_note is monotonic in the degree. */
    int rough = ((note - root) * n) / 12;
    int best = rough, best_d = 1 << 20;
    for (int d = rough - n - 1; d <= rough + n + 1; ++d) {
        int diff = life_degree_to_note(d, root, mask) - note;
        if (diff < 0) diff = -diff;
        if (diff < best_d) { best_d = diff; best = d; }
    }
    return best;
}
