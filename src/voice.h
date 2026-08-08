/* plinky-life - note lifecycle.

   Pure C. No Plinky API. Compiled directly by the desktop test harness.

   This owns the question of what is currently sounding and when it stops, and
   nothing else. It does not know about the automaton, and it does not know
   whether a note ends up on Plinky's internal synth or on a MIDI cable - the
   panel decides that by consuming the release list this produces.

   Stuck notes are the classic failure mode for a sequencer with a MIDI sink, so
   every path that stops a voice - mute, rate change, transport stop, panel
   unload - goes through voice_release_all(). There is exactly one way for a
   note to end. */

#define LIFE_MAX_HELD 16   /* SEL_ALL can sound a whole 16-row column at once */

typedef struct {
    unsigned char note;
    unsigned char vel;
    unsigned char active;
    int ticks_left;
} held_note_t;

typedef struct {
    held_note_t held[LIFE_MAX_HELD];
} voice_notes_t;

static inline void voice_notes_init(voice_notes_t *v) {
    for (int i = 0; i < LIFE_MAX_HELD; ++i) {
        v->held[i].note = 0;
        v->held[i].vel = 0;
        v->held[i].active = 0;
        v->held[i].ticks_left = 0;
    }
}

static inline int voice_num_held(const voice_notes_t *v) {
    int n = 0;
    for (int i = 0; i < LIFE_MAX_HELD; ++i) n += v->held[i].active ? 1 : 0;
    return n;
}

/* Start a note. Returns the slot, or -1 if all 16 are busy.

   Re-arming a note that is already sounding refreshes its countdown rather than
   allocating a second slot; two note-ons for one pitch would leave the second
   note-off orphaned on the MIDI side. */
static inline int voice_arm(voice_notes_t *v, int note, int vel, int ticks) {
    for (int i = 0; i < LIFE_MAX_HELD; ++i)
        if (v->held[i].active && v->held[i].note == (unsigned char)note) {
            v->held[i].ticks_left = ticks;
            v->held[i].vel = (unsigned char)vel;
            return i;
        }
    for (int i = 0; i < LIFE_MAX_HELD; ++i)
        if (!v->held[i].active) {
            v->held[i].note = (unsigned char)note;
            v->held[i].vel = (unsigned char)vel;
            v->held[i].active = 1;
            v->held[i].ticks_left = ticks;
            return i;
        }
    return -1;
}

/* Advance every countdown by `ticks`. Notes that expire are appended to
   `released` (which must have room for LIFE_MAX_HELD entries) and the count is
   returned. The caller is responsible for actually sending the note-offs. */
static inline int voice_tick(voice_notes_t *v, int ticks, unsigned char *released) {
    int n = 0;
    for (int i = 0; i < LIFE_MAX_HELD; ++i) {
        if (!v->held[i].active) continue;
        v->held[i].ticks_left -= ticks;
        if (v->held[i].ticks_left <= 0) {
            released[n++] = v->held[i].note;
            v->held[i].active = 0;
        }
    }
    return n;
}

/* Release everything immediately. The single exit path for a voice.
   `released` must have room for LIFE_MAX_HELD entries. */
static inline int voice_release_all(voice_notes_t *v, unsigned char *released) {
    int n = 0;
    for (int i = 0; i < LIFE_MAX_HELD; ++i) {
        if (!v->held[i].active) continue;
        released[n++] = v->held[i].note;
        v->held[i].active = 0;
    }
    return n;
}

/* Note length is a percentage (10..100) of the voice's step interval. Always at
   least 1 tick, so a very short step at a low percentage still produces an
   audible note rather than a note-on immediately followed by a note-off. */
static inline int voice_length_ticks(int step_ticks, int length_percent) {
    if (length_percent < 10) length_percent = 10;
    if (length_percent > 100) length_percent = 100;
    int t = step_ticks * length_percent / 100;
    return t < 1 ? 1 : t;
}
