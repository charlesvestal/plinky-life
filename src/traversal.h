/* plinky-life - column traversal.

   Pure C. No Plinky API. Compiled directly by the desktop test harness.

   Traversal is which column a voice is on. Selection (selection.h) is which
   live cell in that column sounds. Polyrhythm comes from voices having
   different RATES, not different loop lengths - all four span all 16 columns.

   The design originally delegated this to Plinky's `playhead_t`. We do it here
   instead: `llm.txt` documents `playhead_t::update()` as taking
   "`clock_divider_t::update() > 0`" but declares it as `update(int num_steps,
   ...)`, and those two contracts disagree. Panel code cannot be compiled
   locally to settle the question, so twenty lines of our own - testable on the
   desktop like everything else here - is the cheaper risk. */

typedef enum {
    TRAV_FORWARD = 0,
    TRAV_REVERSE,
    TRAV_PINGPONG,
    TRAV_RANDOM,
    TRAV_COUNT
} traversal_order_t;

/* 4-char labels for draw_system_style_enum_settings_page(...). */
static const char *const life_trav_names[TRAV_COUNT] = { "FWD ", "REV ", "PING", "RAND" };

static const char *const life_trav_help[TRAV_COUNT] = {
    "Forward - left to right through the 16 columns",
    "Reverse - right to left",
    "PingPong - out and back, without repeating the ends",
    "Random - a random column each step",
};

typedef struct {
    short pos;
    signed char dir;
    unsigned int rng;
} traversal_state_t;

static inline void traversal_reset(traversal_state_t *t, traversal_order_t order, int n,
                                   unsigned int seed) {
    t->pos = (order == TRAV_REVERSE) ? (short)(n - 1) : 0;
    t->dir = (order == TRAV_REVERSE) ? -1 : 1;
    t->rng = seed ? seed : 0x2545f491u;
}

static inline unsigned int traversal_rand(traversal_state_t *t) {
    unsigned int x = t->rng ? t->rng : 0x2545f491u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    t->rng = x;
    return x;
}

/* Advance one step and return the new column, always within 0..n-1.

   The position is clamped on entry rather than trusted, so a stale position
   from a changed setting or a loaded save can never index out of range. */
static inline int traversal_advance(traversal_state_t *t, traversal_order_t order, int n) {
    if (n < 1) n = 1;
    int p = t->pos;
    if (p < 0) p = 0;
    if (p >= n) p = n - 1;

    switch (order) {
    case TRAV_REVERSE:
        p = (p - 1 + n) % n;
        break;

    case TRAV_PINGPONG:
        if (n == 1) {
            p = 0;
        } else {
            int dir = t->dir >= 0 ? 1 : -1;
            p += dir;
            if (p >= n) { p = n - 2; dir = -1; }   /* endpoints are not repeated */
            else if (p < 0) { p = 1; dir = 1; }
            t->dir = (signed char)dir;
        }
        break;

    case TRAV_RANDOM:
        p = (int)(traversal_rand(t) % (unsigned int)n);
        break;

    case TRAV_FORWARD:
    default:
        p = (p + 1) % n;
        break;
    }

    t->pos = (short)p;
    return p;
}

static inline int traversal_position(const traversal_state_t *t, int n) {
    if (n < 1) n = 1;
    int p = t->pos;
    if (p < 0) p = 0;
    if (p >= n) p = n - 1;
    return p;
}
