/* plinky-life - conditional triggers, derived from the world.

   Pure C. No Plinky API. Compiled directly by the desktop test harness.

   The obvious way to do this is a per-step overlay you draw by hand. Here they
   are read off the cell the voice actually landed on instead, because the world
   is the thing that moves: a value drawn onto step 7 would be the only part of
   the panel that does not respond to what you are watching. A glider passing
   through a voice's path should audibly change its rhythm, and that only happens
   if the rhythm is a function of the cells.

   Each behaviour has a DEPTH of 0..100, and depth 0 is off. So one control both
   enables a behaviour and says how strongly it responds - there is no separate
   switch to forget.

   All four take the same shape: at depth 0 the behaviour never fires, at depth
   100 the world drives it fully. */

typedef struct {
    unsigned int rng;
    unsigned int pass;    /* how many times this voice has crossed the world */
} chance_state_t;

static inline void chance_reset(chance_state_t *c, unsigned int seed) {
    c->rng = seed ? seed : 0x6d2b79f5u;
    c->pass = 0;
}

static inline unsigned int chance_rand(chance_state_t *c) {
    unsigned int x = c->rng ? c->rng : 0x6d2b79f5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    c->rng = x;
    return x;
}

/* Does this note sound at all?

   Lonely cells get dropped, crowded ones always play. `neighbours` is 0..8, so
   a cell with 8 neighbours is certain at any depth and a cell with none is
   dropped in proportion to depth. At depth 0 everything fires. */
static inline int chance_should_fire(int depth, int neighbours, chance_state_t *c) {
    if (depth <= 0) return 1;
    if (neighbours < 0) neighbours = 0;
    if (neighbours > 8) neighbours = 8;
    if (depth > 100) depth = 100;

    int chance = 100 - (depth * (8 - neighbours)) / 8;
    if (chance >= 100) return 1;
    if (chance <= 0) return 0;
    return (int)(chance_rand(c) % 100u) < chance;
}

/* How many times to retrigger inside one step, 1..4.

   Crowding adds repeats, so a dense knot of cells rolls and a sparse line does
   not. Deterministic on purpose: ratchets that came and went at random would be
   impossible to play along with. */
static inline int chance_ratchets(int depth, int neighbours) {
    if (depth <= 0) return 1;
    if (neighbours < 0) neighbours = 0;
    if (neighbours > 8) neighbours = 8;
    if (depth > 100) depth = 100;

    int extra = (neighbours * depth * 3) / (8 * 100);
    if (extra > 3) extra = 3;
    return 1 + extra;
}

/* Hold the previous note instead of striking a new one.

   Only cells that were already alive last generation can tie - something that
   persisted is what a held note means. Cells born this generation always
   strike. */
static inline int chance_should_tie(int depth, int survived, chance_state_t *c) {
    if (depth <= 0 || !survived) return 0;
    if (depth > 100) depth = 100;
    return (int)(chance_rand(c) % 100u) < depth;
}

/* Play only every Nth crossing of the world, N = 1..4.

   The one behaviour here that is about TIME rather than the cells: it gives a
   voice a phrase longer than a single pass, which nothing else in the panel
   provides. */
static inline int chance_pass_divisor(int depth) {
    if (depth <= 0) return 1;
    if (depth > 100) depth = 100;
    return 1 + (depth * 3) / 100;
}

static inline int chance_pass_ok(int depth, const chance_state_t *c) {
    int n = chance_pass_divisor(depth);
    if (n <= 1) return 1;
    return (int)(c->pass % (unsigned int)n) == 0;
}
