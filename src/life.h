/* plinky-life - the cellular automaton.

   Pure C. No Plinky API. Compiled directly by the desktop test harness.

   A 16x16 toroidal Conway world (B3/S23). Columns are steps, rows are scale
   degrees: row 15 is degree 0, row 0 is degree 15. The world is a *palette*
   that voices read, not a piano roll. */

#define LIFE_W 16
#define LIFE_H 16
#define LIFE_CELLS (LIFE_W * LIFE_H)

/* Row-major, world[y * LIFE_W + x]. 1 = alive. */
typedef struct {
    unsigned char cell[LIFE_CELLS];
} life_world_t;

typedef struct {
    int alive;      /* live cells after the step */
    int births;     /* cells born this generation */
    int deaths;     /* cells that died this generation */
    int unchanged;  /* cells identical to the previous generation */
} life_stats_t;

static inline int life_get(const life_world_t *w, int x, int y) {
    return w->cell[(y & 15) * LIFE_W + (x & 15)] ? 1 : 0;
}

static inline void life_set(life_world_t *w, int x, int y, int alive) {
    w->cell[(y & 15) * LIFE_W + (x & 15)] = alive ? 1 : 0;
}

static inline void life_toggle(life_world_t *w, int x, int y) {
    unsigned char *c = &w->cell[(y & 15) * LIFE_W + (x & 15)];
    *c = *c ? 0 : 1;
}

static inline void life_clear(life_world_t *w) {
    for (int i = 0; i < LIFE_CELLS; ++i) w->cell[i] = 0;
}

static inline int life_population(const life_world_t *w) {
    int n = 0;
    for (int i = 0; i < LIFE_CELLS; ++i) n += w->cell[i] ? 1 : 0;
    return n;
}

/* Live cells in column x, as a bitmask over rows: bit r set means row r alive.
   Bit 0 is row 0 (the top row, the highest pitch). */
static inline unsigned short life_column_mask(const life_world_t *w, int x) {
    unsigned short m = 0;
    x &= 15;
    for (int y = 0; y < LIFE_H; ++y)
        if (w->cell[y * LIFE_W + x]) m |= (unsigned short)(1u << y);
    return m;
}

/* --- Rulesets -------------------------------------------------------------

   A rule is two 9-bit masks: which neighbour counts cause a birth, and which
   let a live cell survive. Conway is B3/S23. The others are not decoration -
   they change what the panel IS, because the rule decides whether the palette
   drifts, churns or explodes. */

typedef struct {
    unsigned short birth;     /* bit n set: a dead cell with n neighbours is born */
    unsigned short survive;   /* bit n set: a live cell with n neighbours lives */
    const char *name;         /* 4 chars, for the settings page */
} life_rule_t;

#define LIFE_B(n) (1u << (n))

static const life_rule_t life_rulesets[] = {
    { LIFE_B(3),                                     LIFE_B(2) | LIFE_B(3),
      "LIFE" },   /* Conway B3/S23 - gliders, sparse, settles eventually */
    { LIFE_B(3) | LIFE_B(6),                         LIFE_B(2) | LIFE_B(3),
      "HIGH" },   /* HighLife B36/S23 - replicators, keeps regenerating */
    { LIFE_B(3),                                     LIFE_B(1) | LIFE_B(2) | LIFE_B(3) |
                                                     LIFE_B(4) | LIFE_B(5),
      "MAZE" },   /* B3/S12345 - dense corridors, slow churn, drone-like */
    { LIFE_B(3),                                     LIFE_B(4) | LIFE_B(5) | LIFE_B(6) |
                                                     LIFE_B(7) | LIFE_B(8),
      "CORL" },   /* Coral B3/S45678 - grows slowly into thick shapes */
    { LIFE_B(3) | LIFE_B(4),                         LIFE_B(3) | LIFE_B(4),
      "34  " },   /* 34 Life B34/S34 - restless, never settles for long */
    { LIFE_B(2),                                     0,
      "SEED" },   /* Seeds B2/S - everything dies every step, explosive */
};
#define LIFE_NUM_RULESETS ((int)(sizeof(life_rulesets) / sizeof(life_rulesets[0])))

/* Toroidal neighbour count. Edges wrap, which is what keeps small worlds
   musically alive - a bounded 16x16 clips gliders dead at the border. */
static inline int life_neighbours(const life_world_t *w, int x, int y) {
    int n = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            n += w->cell[((y + dy) & 15) * LIFE_W + ((x + dx) & 15)] ? 1 : 0;
        }
    return n;
}

/* Advance one generation into `next`, reporting what changed.
   `next` must not alias `w`. `rule` indexes life_rulesets. */
static inline void life_step_rule(const life_world_t *w, life_world_t *next,
                                  life_stats_t *stats, int rule) {
    if (rule < 0 || rule >= LIFE_NUM_RULESETS) rule = 0;
    unsigned birth = life_rulesets[rule].birth;
    unsigned survive = life_rulesets[rule].survive;
    int alive = 0, births = 0, deaths = 0, unchanged = 0;
    for (int y = 0; y < LIFE_H; ++y) {
        for (int x = 0; x < LIFE_W; ++x) {
            int was = w->cell[y * LIFE_W + x] ? 1 : 0;
            int n = life_neighbours(w, x, y);
            int now = was ? ((survive >> n) & 1) : ((birth >> n) & 1);
            next->cell[y * LIFE_W + x] = (unsigned char)now;
            if (now) ++alive;
            if (now && !was) ++births;
            else if (!now && was) ++deaths;
            else ++unchanged;
        }
    }
    if (stats) {
        stats->alive = alive;
        stats->births = births;
        stats->deaths = deaths;
        stats->unchanged = unchanged;
    }
}

/* Conway, for callers that do not care about rulesets - including the tests
   that assert classic Life behaviour. */
static inline void life_step(const life_world_t *w, life_world_t *next, life_stats_t *stats) {
    life_step_rule(w, next, stats, 0);
}

/* --- Liveness -------------------------------------------------------------

   Pure Conway on a 16x16 torus settles into still-lifes and blinkers within
   roughly 100-200 generations. When the palette freezes, four playheads walking
   it become a static loop and the instrument dies. Auto-respawn is therefore
   load-bearing, not a nicety. */

typedef struct {
    int stable_gens;     /* consecutive generations with no births and no deaths */
    unsigned int rng;    /* xorshift32 state; seeded non-zero */
} life_respawn_t;

static inline unsigned int life_rand(unsigned int *state) {
    unsigned int x = *state ? *state : 0x1234567u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static inline void life_respawn_init(life_respawn_t *r, unsigned int seed) {
    r->stable_gens = 0;
    r->rng = seed ? seed : 0x1234567u;
}

/* Feed each generation's stats in. Returns non-zero when the world needs help:
   either population has fallen below `density_floor`, or nothing has changed for
   `stable_limit` generations (a still-life or a period-1 lock).

   Oscillators such as blinkers keep births and deaths non-zero, so they do NOT
   count as stable - which is correct, a blinking world is still making music. */
static inline int life_respawn_tick(life_respawn_t *r, const life_stats_t *stats,
                                    int density_floor, int stable_limit) {
    if (stats->births == 0 && stats->deaths == 0) ++r->stable_gens;
    else r->stable_gens = 0;

    if (stats->alive < density_floor) return 1;
    if (stable_limit > 0 && r->stable_gens >= stable_limit) return 1;
    return 0;
}

/* Sprinkle `count` live cells into random dead positions. Gives up after a
   bounded number of attempts so a nearly-full world cannot spin. */
static inline void life_respawn_apply(life_world_t *w, life_respawn_t *r, int count) {
    int attempts = count * 8 + 16;
    while (count > 0 && attempts-- > 0) {
        int i = (int)(life_rand(&r->rng) % (unsigned int)LIFE_CELLS);
        if (!w->cell[i]) {
            w->cell[i] = 1;
            --count;
        }
    }
    r->stable_gens = 0;
}
