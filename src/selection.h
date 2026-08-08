/* plinky-life - selection rules.

   Pure C. No Plinky API. Compiled directly by the desktop test harness.

   Traversal and selection are deliberately separate concepts:

     traversal  = which column a voice is on   -> handled by playhead_t
     selection  = which live cell in that column actually sounds -> this file

   ZOA's feature list conflates the two. Keeping them apart is what makes the
   behaviour describable, and testable without any hardware.

   Rows are scale degrees: row 15 is degree 0 (lowest pitch), row 0 is degree 15
   (highest pitch). Rules are named in terms of PITCH, not row index, because
   that is what a player hears. "Up" means up in pitch, i.e. toward row 0.

   An empty column is a rest. That is load-bearing: it is what makes the
   automaton's density read as rhythm instead of a wall of notes. */

typedef enum {
    SEL_FIRST = 0,  /* topmost live cell (highest pitch) */
    SEL_LAST,       /* bottommost live cell (lowest pitch) */
    SEL_UP,         /* cycle through live cells, ascending in pitch */
    SEL_DOWN,       /* cycle through live cells, descending in pitch */
    SEL_UPDOWN,     /* ping-pong, ascending first, endpoints not repeated */
    SEL_DOWNUP,     /* ping-pong, descending first */
    SEL_RANDOM,     /* uniform pick among live cells */
    SEL_WALK,       /* live cell nearest in pitch to the previous note */
    SEL_RISE,       /* nearest live cell strictly above the previous, wrapping */
    SEL_FALL,       /* nearest live cell strictly below the previous, wrapping */
    SEL_ALL,        /* every live cell at once - the only rule that makes chords */
    SEL_COUNT
} selection_rule_t;

/* 4-char labels for draw_system_style_enum_settings_page(...). */
static const char *const life_sel_names[SEL_COUNT] = {
    "FRST", "LAST", "UP  ", "DOWN", "UPDN", "DNUP", "RAND", "WALK", "RISE", "FALL", "ALL "
};

typedef struct {
    short cycle_idx;    /* position within the pitch-ascending live-cell list */
    short prev_row;     /* row of the previous note, or -1 if none yet */
    signed char dir;    /* ping-pong direction: +1 ascending, -1 descending */
    unsigned int rng;   /* xorshift32 state for SEL_RANDOM */
} selection_state_t;

static inline void selection_reset(selection_state_t *s, unsigned int seed) {
    s->cycle_idx = 0;
    s->prev_row = -1;
    s->dir = 1;
    s->rng = seed ? seed : 0x9e3779b9u;
}

static inline unsigned int selection_rand(selection_state_t *s) {
    unsigned int x = s->rng ? s->rng : 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->rng = x;
    return x;
}

/* Fill `rows` with the live row indices in PITCH-ASCENDING order (row 15 first,
   row 0 last) and return how many there were. */
static inline int selection_collect(unsigned short col_mask, int *rows) {
    int n = 0;
    for (int y = 15; y >= 0; --y)
        if (col_mask & (unsigned short)(1u << y)) rows[n++] = y;
    return n;
}

/* Choose which live cells in this column sound.

   Returns a row bitmask: 0 means rest, one bit for the single-note rules, and
   the whole column for SEL_ALL.

   Every rule that retains state across ticks (UP, DOWN, UPDOWN, DOWNUP, WALK,
   RISE, FALL) must survive the live-cell set changing size underneath it -
   which it constantly will, because the automaton is rewriting the column
   between ticks. The retained index is therefore clamped into range on read and
   never assumed valid from the previous tick. */
static inline unsigned short selection_pick(selection_state_t *s, unsigned short col_mask,
                                            selection_rule_t rule) {
    int rows[16];
    int n = selection_collect(col_mask, rows);

    if (n == 0) return 0;             /* empty column -> rest */
    if (rule == SEL_ALL) {
        s->prev_row = (short)rows[n - 1];
        return col_mask;
    }

    int chosen;                        /* index into `rows`, pitch-ascending */

    switch (rule) {
    case SEL_FIRST:
        chosen = n - 1;                /* highest pitch = topmost row */
        break;

    case SEL_LAST:
        chosen = 0;                    /* lowest pitch = bottommost row */
        break;

    case SEL_UP:
        chosen = (s->cycle_idx + 1) % n;
        break;

    case SEL_DOWN:
        chosen = ((s->cycle_idx - 1) % n + n) % n;
        break;

    case SEL_UPDOWN:
    case SEL_DOWNUP:
        if (n == 1) {
            chosen = 0;
        } else {
            int dir = s->dir >= 0 ? 1 : -1;
            if (rule == SEL_DOWNUP && s->prev_row < 0) dir = -1;
            int idx = s->cycle_idx;
            if (idx >= n) idx = n - 1;
            if (idx < 0) idx = 0;
            idx += dir;
            if (idx >= n) { idx = n - 2; dir = -1; }   /* endpoint not repeated */
            else if (idx < 0) { idx = 1; dir = 1; }
            chosen = idx;
            s->dir = (signed char)dir;
        }
        break;

    case SEL_RANDOM:
        chosen = (int)(selection_rand(s) % (unsigned int)n);
        break;

    case SEL_WALK: {
        if (s->prev_row < 0) { chosen = n - 1; break; }
        int best = 0, best_d = 32;
        for (int i = 0; i < n; ++i) {
            int d = rows[i] - s->prev_row;
            if (d < 0) d = -d;
            /* strict < keeps the tie-break at the first match in
               pitch-ascending order, i.e. the lower pitch */
            if (d < best_d) { best_d = d; best = i; }
        }
        chosen = best;
        break;
    }

    case SEL_RISE: {
        /* strictly above in pitch = a smaller row index; nearest = the largest
           such row. Wraps to the lowest pitch when there is nothing above. */
        if (s->prev_row < 0) { chosen = 0; break; }
        chosen = -1;
        for (int i = 0; i < n; ++i)
            if (rows[i] < s->prev_row) { chosen = i; break; }
        if (chosen < 0) chosen = 0;
        break;
    }

    case SEL_FALL: {
        /* strictly below in pitch = a larger row index; nearest = the smallest
           such row. Wraps to the highest pitch when there is nothing below. */
        if (s->prev_row < 0) { chosen = n - 1; break; }
        chosen = -1;
        for (int i = n - 1; i >= 0; --i)
            if (rows[i] > s->prev_row) { chosen = i; break; }
        if (chosen < 0) chosen = n - 1;
        break;
    }

    default:
        chosen = n - 1;
        break;
    }

    if (chosen < 0) chosen = 0;
    if (chosen >= n) chosen = n - 1;

    s->cycle_idx = (short)chosen;
    s->prev_row = (short)rows[chosen];
    return (unsigned short)(1u << rows[chosen]);
}

/* How far this note moved from the previous one, 0..127. Drives the per-voice
   "randomness" CC. Returns 0 when there was no previous note. */
static inline int selection_deviation(const selection_state_t *s, int prev_row) {
    if (prev_row < 0 || s->prev_row < 0) return 0;
    int d = s->prev_row - prev_row;
    if (d < 0) d = -d;
    return d * 127 / 15;
}
