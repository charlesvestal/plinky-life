/* plinky-life - incoming MIDI CC map.

   Pure C. No Plinky API. Compiled directly by the desktop test harness.

   A contiguous block of controllers that mirrors the panel's own settings, so
   anything on the grid or the settings pages can also be driven from a DAW or a
   controller.

   Laid out BY PARAMETER, not by voice: the four voices' rates are four
   consecutive controllers, so a row of four knobs on a controller does the same
   thing to each voice. Grouping by voice would have put rate, rule, order,
   pitch, length and mute on one row instead, which is not how anyone reaches
   for four knobs.

   The block starts wherever the user puts it. It deliberately does NOT overlap
   the simulation CCs this panel SENDS (20..27) at the default base of 30, but
   nothing stops you moving it on top of them if you want the loopback. */

typedef enum {
    CC_KEY = 0,       /* root note */
    CC_SCALE,
    CC_OCTAVE,
    CC_GEN_RATE,      /* how often the world evolves */
    CC_FLOOR,         /* respawn population floor */
    CC_SEED_AMOUNT,
    CC_STALL,
    CC_CLEAR,         /* momentary: fires on the rising edge past halfway */
    CC_SEED_NOW,      /* momentary */
    CC_FREEZE,        /* level: >= halfway is frozen */
    CC_STEP,          /* momentary: one generation */

    CC_MUTE_1,        /* four consecutive, one per voice, and so on below */
    CC_MUTE_2, CC_MUTE_3, CC_MUTE_4,
    CC_RATE_1, CC_RATE_2, CC_RATE_3, CC_RATE_4,
    CC_RULE_1, CC_RULE_2, CC_RULE_3, CC_RULE_4,
    CC_ORDER_1, CC_ORDER_2, CC_ORDER_3, CC_ORDER_4,
    CC_PITCH_1, CC_PITCH_2, CC_PITCH_3, CC_PITCH_4,
    CC_LENGTH_1, CC_LENGTH_2, CC_LENGTH_3, CC_LENGTH_4,

    CC_PARAM_COUNT
} cc_param_t;

#define CC_IN_DEFAULT_BASE 30
#define CC_FIRST_PER_VOICE CC_MUTE_1

/* Which parameter a controller number addresses, or -1 for one we ignore.
   A base of 0 disables the whole block. */
static inline int cc_param_for_number(int cc, int base) {
    if (base <= 0) return -1;
    if (cc < base) return -1;
    int idx = cc - base;
    if (idx >= CC_PARAM_COUNT) return -1;
    return idx;
}

/* The highest controller number the block occupies, for a range check. */
static inline int cc_last_number(int base) { return base + CC_PARAM_COUNT - 1; }

/* Split 0..127 evenly across `count` options.

   127 must land on the last option - a controller sending its maximum has to be
   able to reach the top of the range, which a plain (v * count) / 128 does
   provide, but only because 127 * count / 128 == count - 1 for every count we
   use. The clamp makes that a guarantee rather than an arithmetic coincidence. */
static inline int cc_to_index(int value, int count) {
    if (count < 1) return 0;
    if (value < 0) value = 0;
    if (value > 127) value = 127;
    int i = (value * count) / 128;
    if (i >= count) i = count - 1;
    return i;
}

/* Map 0..127 onto an inclusive integer range. */
static inline int cc_to_range(int value, int lo, int hi) {
    if (hi < lo) return lo;
    return lo + cc_to_index(value, hi - lo + 1);
}

/* Momentary controls fire on the rising edge through the midpoint, so a button
   that sends 127 then 0 fires once rather than twice. */
static inline int cc_is_press(int previous, int value) {
    return previous < 64 && value >= 64;
}

/* Which voice a per-voice parameter belongs to, or -1 if it is global. */
static inline int cc_voice_for_param(int param) {
    if (param < CC_FIRST_PER_VOICE || param >= CC_PARAM_COUNT) return -1;
    return (param - CC_FIRST_PER_VOICE) % 4;
}

/* Which group a per-voice parameter belongs to: 0 mute, 1 rate, 2 rule,
   3 order, 4 pitch, 5 length. -1 if it is global. */
static inline int cc_group_for_param(int param) {
    if (param < CC_FIRST_PER_VOICE || param >= CC_PARAM_COUNT) return -1;
    return (param - CC_FIRST_PER_VOICE) / 4;
}
