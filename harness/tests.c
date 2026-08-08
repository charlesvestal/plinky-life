/* plinky-life - desktop test harness.

   life.h, selection.h, scales.h and voice.h are pure functions of plain data
   with no Plinky API in them, so they compile and run natively. This is where
   the musical logic is proved; panel.cpp is then thin glue that cannot be
   tested anywhere but the device.

   Build and run:  sh harness/build.sh */

#include <stdio.h>
#include <string.h>

#include "../src/life.h"
#include "../src/selection.h"
#include "../src/traversal.h"
#include "../src/scales.h"
#include "../src/voice.h"
#include "../src/ccmap.h"
#include "../src/chance.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        ++checks;                                                              \
        if (!(cond)) {                                                         \
            ++failures;                                                        \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                        \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

/* ---------------------------------------------------------------- life.h -- */

static void set_cells(life_world_t *w, const int (*pts)[2], int n) {
    life_clear(w);
    for (int i = 0; i < n; ++i) life_set(w, pts[i][0], pts[i][1], 1);
}

static void test_blinker_oscillates(void) {
    life_world_t a, b, c;
    const int blinker[3][2] = {{4, 5}, {5, 5}, {6, 5}};
    set_cells(&a, blinker, 3);

    life_step(&a, &b, 0);
    CHECK(life_population(&b) == 3, "blinker gen1 population %d", life_population(&b));
    CHECK(life_get(&b, 5, 4) && life_get(&b, 5, 5) && life_get(&b, 5, 6),
          "blinker did not rotate to vertical");

    life_step(&b, &c, 0);
    CHECK(memcmp(a.cell, c.cell, LIFE_CELLS) == 0, "blinker period is not 2");
}

static void test_block_is_stable(void) {
    life_world_t a, b;
    const int block[4][2] = {{2, 2}, {3, 2}, {2, 3}, {3, 3}};
    set_cells(&a, block, 4);

    life_stats_t st;
    life_step(&a, &b, &st);
    CHECK(memcmp(a.cell, b.cell, LIFE_CELLS) == 0, "block is not a still life");
    CHECK(st.births == 0 && st.deaths == 0, "still life reported births/deaths");
    CHECK(st.unchanged == LIFE_CELLS, "still life unchanged %d", st.unchanged);
}

static void test_glider_translates(void) {
    /* A glider returns to its original shape translated by (1,1) every 4 gens. */
    life_world_t w[5];
    const int glider[5][2] = {{1, 0}, {2, 1}, {0, 2}, {1, 2}, {2, 2}};
    set_cells(&w[0], glider, 5);

    for (int i = 0; i < 4; ++i) life_step(&w[i], &w[i + 1], 0);
    CHECK(life_population(&w[4]) == 5, "glider lost cells: %d", life_population(&w[4]));

    life_world_t shifted;
    life_clear(&shifted);
    for (int y = 0; y < LIFE_H; ++y)
        for (int x = 0; x < LIFE_W; ++x)
            if (life_get(&w[0], x, y)) life_set(&shifted, x + 1, y + 1, 1);
    CHECK(memcmp(shifted.cell, w[4].cell, LIFE_CELLS) == 0,
          "glider did not translate by (1,1) in 4 generations");
}

static void test_glider_wraps_the_torus(void) {
    /* 16 wide, moving (1,1) per 4 gens => back to the start after 64 gens.
       On a bounded grid the glider would smear into a still life at the corner;
       this is the test that proves the world is a torus. */
    life_world_t a, b;
    const int glider[5][2] = {{1, 0}, {2, 1}, {0, 2}, {1, 2}, {2, 2}};
    set_cells(&a, glider, 5);

    life_world_t start;
    memcpy(&start, &a, sizeof(start));

    for (int i = 0; i < 64; ++i) {
        life_step(&a, &b, 0);
        memcpy(&a, &b, sizeof(a));
        CHECK(life_population(&a) == 5, "glider died at generation %d (pop %d)",
              i + 1, life_population(&a));
        if (life_population(&a) != 5) return;
    }
    CHECK(memcmp(start.cell, a.cell, LIFE_CELLS) == 0,
          "glider did not return to its start after wrapping the torus");
}

static void test_column_mask(void) {
    life_world_t w;
    life_clear(&w);
    life_set(&w, 3, 0, 1);
    life_set(&w, 3, 15, 1);
    life_set(&w, 4, 7, 1);

    CHECK(life_column_mask(&w, 3) == (unsigned short)((1u << 0) | (1u << 15)),
          "column 3 mask wrong: 0x%04X", life_column_mask(&w, 3));
    CHECK(life_column_mask(&w, 4) == (unsigned short)(1u << 7), "column 4 mask wrong");
    CHECK(life_column_mask(&w, 5) == 0, "empty column should be 0");
}

static void test_respawn_triggers_on_stalled_world(void) {
    life_world_t a, b;
    life_respawn_t r;
    life_respawn_init(&r, 12345);

    /* A block is stable: no births, no deaths. */
    const int block[4][2] = {{2, 2}, {3, 2}, {2, 3}, {3, 3}};
    set_cells(&a, block, 4);

    int fired = 0;
    for (int i = 0; i < 4; ++i) {
        life_stats_t st;
        life_step(&a, &b, &st);
        memcpy(&a, &b, sizeof(a));
        if (life_respawn_tick(&r, &st, 0, 3)) fired = 1;
    }
    CHECK(fired, "respawn never fired on a still life");
}

static void test_respawn_does_not_fire_on_a_live_world(void) {
    life_world_t a, b;
    life_respawn_t r;
    life_respawn_init(&r, 999);

    /* A blinker changes every generation, so it must NOT count as stalled -
       a blinking world is still making music. */
    const int blinker[3][2] = {{4, 5}, {5, 5}, {6, 5}};
    set_cells(&a, blinker, 3);

    for (int i = 0; i < 20; ++i) {
        life_stats_t st;
        life_step(&a, &b, &st);
        memcpy(&a, &b, sizeof(a));
        CHECK(!life_respawn_tick(&r, &st, 0, 3),
              "respawn fired on an oscillating world at gen %d", i);
    }
}

static void test_respawn_fires_on_low_density(void) {
    life_world_t a, b;
    life_respawn_t r;
    life_respawn_init(&r, 7);
    const int block[4][2] = {{2, 2}, {3, 2}, {2, 3}, {3, 3}};
    set_cells(&a, block, 4);

    life_stats_t st;
    life_step(&a, &b, &st);
    CHECK(life_respawn_tick(&r, &st, 10, 0), "respawn did not fire below the density floor");
}

static void test_respawn_apply_adds_cells(void) {
    life_world_t w;
    life_respawn_t r;
    life_clear(&w);
    life_respawn_init(&r, 42);

    life_respawn_apply(&w, &r, 20);
    CHECK(life_population(&w) == 20, "respawn added %d cells, wanted 20", life_population(&w));
    CHECK(r.stable_gens == 0, "respawn did not reset the stability counter");
}

static void test_respawn_apply_terminates_on_a_full_world(void) {
    life_world_t w;
    life_respawn_t r;
    for (int i = 0; i < LIFE_CELLS; ++i) w.cell[i] = 1;
    life_respawn_init(&r, 42);

    life_respawn_apply(&w, &r, 20);   /* must not hang looking for a dead cell */
    CHECK(life_population(&w) == LIFE_CELLS, "full world changed");
}

static void test_rulesets_are_well_formed(void) {
    for (int r = 0; r < LIFE_NUM_RULESETS; ++r) {
        CHECK((life_rulesets[r].birth & ~0x1FFu) == 0, "ruleset %d birth mask has bits above 8", r);
        CHECK((life_rulesets[r].survive & ~0x1FFu) == 0, "ruleset %d survive mask too wide", r);
        CHECK(!(life_rulesets[r].birth & 1u), "ruleset %d births from 0 neighbours", r);
        CHECK(strlen(life_rulesets[r].name) == 4, "ruleset %d label \"%s\" is not 4 chars",
              r, life_rulesets[r].name);
    }
    CHECK(life_rulesets[0].birth == (1u << 3), "ruleset 0 must be Conway B3");
    CHECK(life_rulesets[0].survive == ((1u << 2) | (1u << 3)), "ruleset 0 must be Conway S23");
}

static void test_seeds_kills_every_live_cell(void) {
    /* Seeds is B2/S - nothing survives, which is what makes it explosive. */
    int seeds = -1;
    for (int r = 0; r < LIFE_NUM_RULESETS; ++r)
        if (life_rulesets[r].survive == 0) seeds = r;
    CHECK(seeds >= 0, "expected a survive-nothing ruleset");
    if (seeds < 0) return;

    life_world_t a, b;
    life_respawn_t rs;
    life_respawn_init(&rs, 5);
    life_clear(&a);
    life_respawn_apply(&a, &rs, 40);

    life_stats_t st;
    life_step_rule(&a, &b, &st, seeds);
    for (int i = 0; i < LIFE_CELLS; ++i)
        if (a.cell[i] && b.cell[i]) {
            CHECK(0, "a live cell survived under a survive-nothing rule");
            return;
        }
}

static void test_every_ruleset_is_stable_under_churn(void) {
    /* No rule may produce a cell value other than 0 or 1, or run away. */
    for (int r = 0; r < LIFE_NUM_RULESETS; ++r) {
        life_world_t a, b;
        life_respawn_t rs;
        life_respawn_init(&rs, 900 + r);
        life_clear(&a);
        life_respawn_apply(&a, &rs, 60);
        for (int gen = 0; gen < 120; ++gen) {
            life_stats_t st;
            life_step_rule(&a, &b, &st, r);
            memcpy(&a, &b, sizeof(a));
            CHECK(st.alive >= 0 && st.alive <= LIFE_CELLS, "ruleset %d bad population %d",
                  r, st.alive);
            CHECK(st.births + st.deaths + st.unchanged == LIFE_CELLS,
                  "ruleset %d stats do not account for every cell", r);
            for (int i = 0; i < LIFE_CELLS; ++i)
                if (a.cell[i] > 1) { CHECK(0, "ruleset %d produced cell value %d",
                                           r, a.cell[i]); return; }
        }
    }
}

static void test_out_of_range_ruleset_falls_back_to_conway(void) {
    life_world_t a, b, c;
    const int blinker[3][2] = {{4, 5}, {5, 5}, {6, 5}};
    set_cells(&a, blinker, 3);
    life_step_rule(&a, &b, 0, 99);
    life_step(&a, &c, 0);
    CHECK(memcmp(b.cell, c.cell, LIFE_CELLS) == 0, "a bad ruleset index must behave as Conway");
}

/* ----------------------------------------------------------- selection.h -- */

static unsigned short col(const int *rows, int n) {
    unsigned short m = 0;
    for (int i = 0; i < n; ++i) m |= (unsigned short)(1u << rows[i]);
    return m;
}

static int only_row(unsigned short mask) {
    int found = -1;
    for (int y = 0; y < 16; ++y)
        if (mask & (1u << y)) {
            if (found >= 0) return -2;   /* more than one bit */
            found = y;
        }
    return found;
}

static void test_empty_column_is_a_rest_for_every_rule(void) {
    for (int r = 0; r < SEL_COUNT; ++r) {
        selection_state_t s;
        selection_reset(&s, 1);
        CHECK(selection_pick(&s, 0, (selection_rule_t)r) == 0,
              "rule %s did not rest on an empty column", life_sel_names[r]);
    }
}

static void test_first_and_last(void) {
    const int rows[] = {2, 7, 13};
    unsigned short m = col(rows, 3);
    selection_state_t s;

    selection_reset(&s, 1);
    CHECK(only_row(selection_pick(&s, m, SEL_FIRST)) == 2, "FIRST should pick the topmost row");

    selection_reset(&s, 1);
    CHECK(only_row(selection_pick(&s, m, SEL_LAST)) == 13, "LAST should pick the bottommost row");
}

static void test_up_ascends_in_pitch(void) {
    const int rows[] = {2, 7, 13};   /* pitch-ascending order: 13, 7, 2 */
    unsigned short m = col(rows, 3);
    selection_state_t s;
    selection_reset(&s, 1);

    int got[6];
    for (int i = 0; i < 6; ++i) got[i] = only_row(selection_pick(&s, m, SEL_UP));

    /* starts at cycle_idx 0, so the first advance lands on index 1 = row 7 */
    const int want[6] = {7, 2, 13, 7, 2, 13};
    for (int i = 0; i < 6; ++i)
        CHECK(got[i] == want[i], "UP step %d gave row %d, wanted %d", i, got[i], want[i]);
}

static void test_down_descends_in_pitch(void) {
    const int rows[] = {2, 7, 13};
    unsigned short m = col(rows, 3);
    selection_state_t s;
    selection_reset(&s, 1);

    int got[6];
    for (int i = 0; i < 6; ++i) got[i] = only_row(selection_pick(&s, m, SEL_DOWN));

    const int want[6] = {2, 7, 13, 2, 7, 13};
    for (int i = 0; i < 6; ++i)
        CHECK(got[i] == want[i], "DOWN step %d gave row %d, wanted %d", i, got[i], want[i]);
}

static void test_updown_pingpongs_without_repeating_endpoints(void) {
    const int rows[] = {2, 7, 13};   /* pitch-ascending: 13, 7, 2 */
    unsigned short m = col(rows, 3);
    selection_state_t s;
    selection_reset(&s, 1);

    int got[6];
    for (int i = 0; i < 6; ++i) got[i] = only_row(selection_pick(&s, m, SEL_UPDOWN));

    const int want[6] = {7, 2, 7, 13, 7, 2};
    for (int i = 0; i < 6; ++i)
        CHECK(got[i] == want[i], "UPDOWN step %d gave row %d, wanted %d", i, got[i], want[i]);
}

static void test_walk_picks_the_nearest(void) {
    selection_state_t s;
    selection_reset(&s, 1);
    s.prev_row = 8;

    const int rows[] = {1, 6, 14};
    CHECK(only_row(selection_pick(&s, col(rows, 3), SEL_WALK)) == 6,
          "WALK should have picked row 6, nearest to 8");
}

static void test_rise_and_fall_wrap(void) {
    const int rows[] = {3, 8, 12};
    unsigned short m = col(rows, 3);
    selection_state_t s;

    /* RISE: strictly higher pitch = a smaller row index */
    selection_reset(&s, 1);
    s.prev_row = 8;
    CHECK(only_row(selection_pick(&s, m, SEL_RISE)) == 3, "RISE from row 8 should give row 3");
    CHECK(only_row(selection_pick(&s, m, SEL_RISE)) == 12,
          "RISE from the top should wrap to the lowest pitch");

    /* FALL: strictly lower pitch = a larger row index */
    selection_reset(&s, 1);
    s.prev_row = 8;
    CHECK(only_row(selection_pick(&s, m, SEL_FALL)) == 12, "FALL from row 8 should give row 12");
    CHECK(only_row(selection_pick(&s, m, SEL_FALL)) == 3,
          "FALL from the bottom should wrap to the highest pitch");
}

static void test_all_returns_the_whole_column(void) {
    const int rows[] = {0, 5, 9, 15};
    unsigned short m = col(rows, 4);
    selection_state_t s;
    selection_reset(&s, 1);
    CHECK(selection_pick(&s, m, SEL_ALL) == m, "ALL should return every live cell");
}

static void test_random_always_lands_on_a_live_cell(void) {
    const int rows[] = {1, 4, 11};
    unsigned short m = col(rows, 3);
    selection_state_t s;
    selection_reset(&s, 0xBEEF);

    for (int i = 0; i < 500; ++i) {
        unsigned short got = selection_pick(&s, m, SEL_RANDOM);
        CHECK((got & ~m) == 0 && got != 0, "RANDOM picked a dead cell: 0x%04X", got);
        if ((got & ~m) || !got) return;
    }
}

static void test_retained_rules_survive_the_column_shrinking(void) {
    /* This is the case the automaton creates constantly: a voice's retained
       index refers to a live-cell set that no longer exists. Every rule must
       still land on a live cell rather than reading off the end. */
    const int wide[] = {0, 2, 4, 6, 8, 10, 12, 14};
    const int narrow[] = {9};
    unsigned short big = col(wide, 8), small = col(narrow, 1);

    for (int r = 0; r < SEL_COUNT; ++r) {
        if (r == SEL_ALL) continue;
        selection_state_t s;
        selection_reset(&s, 3);
        for (int i = 0; i < 8; ++i) selection_pick(&s, big, (selection_rule_t)r);

        unsigned short got = selection_pick(&s, small, (selection_rule_t)r);
        CHECK(got == small, "rule %s picked 0x%04X after the column shrank, wanted 0x%04X",
              life_sel_names[r], got, small);
    }
}

static void test_every_rule_lands_on_a_live_cell_under_churn(void) {
    /* Fuzz: run every rule against a real evolving world and assert the chosen
       cells are always alive. Catches off-by-ones the hand-built cases miss. */
    life_world_t a, b;
    life_respawn_t rs;
    life_respawn_init(&rs, 0xC0FFEE);
    life_clear(&a);
    life_respawn_apply(&a, &rs, 90);

    for (int r = 0; r < SEL_COUNT; ++r) {
        selection_state_t s;
        selection_reset(&s, 17 + r);
        life_world_t w;
        memcpy(&w, &a, sizeof(w));

        for (int gen = 0; gen < 200; ++gen) {
            for (int x = 0; x < LIFE_W; ++x) {
                unsigned short m = life_column_mask(&w, x);
                unsigned short got = selection_pick(&s, m, (selection_rule_t)r);
                if (got & ~m) {
                    CHECK(0, "rule %s picked a dead cell at gen %d col %d",
                          life_sel_names[r], gen, x);
                    return;
                }
                if (m != 0 && got == 0) {
                    CHECK(0, "rule %s rested on a non-empty column at gen %d col %d",
                          life_sel_names[r], gen, x);
                    return;
                }
            }
            life_stats_t st;
            life_step(&w, &b, &st);
            memcpy(&w, &b, sizeof(w));
            if (life_respawn_tick(&rs, &st, 8, 4)) life_respawn_apply(&w, &rs, 12);
        }
    }
}

/* ----------------------------------------------------------- traversal.h -- */

static void test_forward_wraps(void) {
    traversal_state_t t;
    traversal_reset(&t, TRAV_FORWARD, 16, 1);
    for (int i = 1; i <= 16; ++i) {
        int p = traversal_advance(&t, TRAV_FORWARD, 16);
        CHECK(p == i % 16, "FWD step %d gave %d", i, p);
    }
}

static void test_reverse_wraps(void) {
    traversal_state_t t;
    traversal_reset(&t, TRAV_REVERSE, 16, 1);
    CHECK(traversal_position(&t, 16) == 15, "REV should start at the last column");
    int p = traversal_advance(&t, TRAV_REVERSE, 16);
    CHECK(p == 14, "REV first step gave %d", p);
    for (int i = 0; i < 14; ++i) p = traversal_advance(&t, TRAV_REVERSE, 16);
    CHECK(p == 0, "REV should reach 0, gave %d", p);
    p = traversal_advance(&t, TRAV_REVERSE, 16);
    CHECK(p == 15, "REV should wrap to 15, gave %d", p);
}

static void test_pingpong_does_not_repeat_endpoints(void) {
    traversal_state_t t;
    traversal_reset(&t, TRAV_PINGPONG, 4, 1);
    const int want[10] = {1, 2, 3, 2, 1, 0, 1, 2, 3, 2};
    for (int i = 0; i < 10; ++i) {
        int p = traversal_advance(&t, TRAV_PINGPONG, 4);
        CHECK(p == want[i], "PING step %d gave %d, wanted %d", i, p, want[i]);
    }
}

static void test_traversal_stays_in_range(void) {
    for (int o = 0; o < TRAV_COUNT; ++o) {
        traversal_state_t t;
        traversal_reset(&t, (traversal_order_t)o, 16, 5 + o);
        for (int i = 0; i < 1000; ++i) {
            int p = traversal_advance(&t, (traversal_order_t)o, 16);
            CHECK(p >= 0 && p < 16, "order %s left range: %d", life_trav_names[o], p);
            if (p < 0 || p >= 16) return;
        }
    }
}

static void test_traversal_clamps_a_stale_position(void) {
    /* A loaded save or a changed setting can leave a position past the end. */
    for (int o = 0; o < TRAV_COUNT; ++o) {
        traversal_state_t t;
        traversal_reset(&t, (traversal_order_t)o, 16, 1);
        t.pos = 900;
        int p = traversal_advance(&t, (traversal_order_t)o, 16);
        CHECK(p >= 0 && p < 16, "order %s did not clamp a stale position: %d",
              life_trav_names[o], p);
        t.pos = -50;
        p = traversal_advance(&t, (traversal_order_t)o, 16);
        CHECK(p >= 0 && p < 16, "order %s did not clamp a negative position: %d",
              life_trav_names[o], p);
    }
}

static void test_traversal_names_are_four_chars(void) {
    for (int i = 0; i < TRAV_COUNT; ++i)
        CHECK(strlen(life_trav_names[i]) == 4, "traversal label \"%s\" is not 4 chars",
              life_trav_names[i]);
}

static void test_selection_names_are_four_chars(void) {
    for (int i = 0; i < SEL_COUNT; ++i)
        CHECK(strlen(life_sel_names[i]) == 4, "selection label \"%s\" is not 4 chars",
              life_sel_names[i]);
}

/* -------------------------------------------------------------- scales.h -- */

static void test_every_scale_contains_its_root(void) {
    for (int i = 0; i < LIFE_NUM_SCALES; ++i)
        CHECK(life_scale_masks[i] & 1, "scale %s (%d) does not contain its root",
              life_scale_long_names[i], i);
}

static void test_scale_masks_are_12_bit_and_unique(void) {
    for (int i = 0; i < LIFE_NUM_SCALES; ++i) {
        CHECK((life_scale_masks[i] & ~0x0FFFu) == 0, "scale %s has bits above 12",
              life_scale_long_names[i]);
        for (int j = i + 1; j < LIFE_NUM_SCALES; ++j)
            CHECK(life_scale_masks[i] != life_scale_masks[j], "scales %s and %s are identical",
                  life_scale_long_names[i], life_scale_long_names[j]);
    }
}

static void test_scale_names_are_four_chars(void) {
    /* draw_system_style_enum_settings_page shows a short label; anything longer
       gets truncated inconsistently. */
    for (int i = 0; i < LIFE_NUM_SCALES; ++i)
        CHECK(strlen(life_scale_names[i]) == 4, "scale label \"%s\" is not 4 chars",
              life_scale_names[i]);
}

static void test_major_scale_degrees(void) {
    const unsigned short major = 0xAB5;
    const int want[8] = {60, 62, 64, 65, 67, 69, 71, 72};
    for (int d = 0; d < 8; ++d) {
        int got = life_degree_to_note(d, 60, major);
        CHECK(got == want[d], "C major degree %d gave %d, wanted %d", d, got, want[d]);
    }
}

static void test_negative_degrees_go_down_an_octave(void) {
    const unsigned short major = 0xAB5;
    CHECK(life_degree_to_note(-1, 60, major) == 59, "degree -1 of C major should be B below");
    CHECK(life_degree_to_note(-7, 60, major) == 48, "degree -7 of C major should be C an octave down");
}

static void test_pentatonic_wraps_at_five(void) {
    const unsigned short minpent = 0x4A9;   /* 0 3 5 7 10 */
    CHECK(life_degree_to_note(5, 60, minpent) == 72, "minor pentatonic degree 5 should be +1 octave");
    CHECK(life_degree_to_note(6, 60, minpent) == 75, "minor pentatonic degree 6 wrong");
}

static void test_notes_stay_in_midi_range(void) {
    for (int i = 0; i < LIFE_NUM_SCALES; ++i)
        for (int d = -64; d < 64; ++d) {
            int n = life_degree_to_note(d, 60, life_scale_masks[i]);
            CHECK(n >= 0 && n <= 127, "scale %s degree %d gave out-of-range note %d",
                  life_scale_long_names[i], d, n);
            if (n < 0 || n > 127) return;
        }
}

static void test_empty_mask_falls_back_to_chromatic(void) {
    CHECK(life_degree_to_note(1, 60, 0) == 61, "an empty scale mask should behave chromatically");
}

static void test_note_to_degree_round_trips(void) {
    /* Every degree must come back from the note it produces. */
    for (int i = 0; i < LIFE_NUM_SCALES; ++i) {
        unsigned short m = life_scale_masks[i];
        for (int d = -20; d <= 20; ++d) {
            int note = life_degree_to_note(d, 60, m);
            if (note <= 0 || note >= 127) continue;      /* clamped, not invertible */
            int back = life_note_to_degree(note, 60, m);
            CHECK(life_degree_to_note(back, 60, m) == note,
                  "scale %s degree %d -> note %d -> degree %d played a different note",
                  life_scale_long_names[i], d, note, back);
        }
    }
}

static void test_note_to_degree_snaps_off_scale_notes(void) {
    const unsigned short minpent = 0x4A9;    /* 0 3 5 7 10 */
    /* C#, not in C minor pentatonic, must still land somewhere sensible. */
    int d = life_note_to_degree(61, 60, minpent);
    int got = life_degree_to_note(d, 60, minpent);
    CHECK(got == 60 || got == 63, "C# should snap to C or D#, got %d", got);
}

static void test_note_to_degree_covers_the_midi_range(void) {
    for (int i = 0; i < LIFE_NUM_SCALES; ++i)
        for (int note = 0; note <= 127; ++note) {
            int d = life_note_to_degree(note, 60, life_scale_masks[i]);
            int back = life_degree_to_note(d, 60, life_scale_masks[i]);
            CHECK(back >= 0 && back <= 127, "note %d gave an out-of-range note %d", note, back);
            int err = back - note; if (err < 0) err = -err;
            CHECK(err <= 6, "note %d snapped %d semitones away in scale %s",
                  note, err, life_scale_long_names[i]);
            if (err > 6) return;
        }
}

/* --------------------------------------------------------------- voice.h -- */

static void test_note_expires_after_its_length(void) {
    voice_notes_t v;
    unsigned char rel[LIFE_MAX_HELD];
    voice_notes_init(&v);

    voice_arm(&v, 60, 100, 5);
    CHECK(voice_num_held(&v) == 1, "arm did not hold the note");
    CHECK(voice_tick(&v, 4, rel) == 0, "note released early");
    CHECK(voice_tick(&v, 1, rel) == 1, "note did not release on time");
    CHECK(rel[0] == 60, "released the wrong note");
    CHECK(voice_num_held(&v) == 0, "note still held after release");
}

static void test_rearming_the_same_note_does_not_double_allocate(void) {
    /* Two note-ons for one pitch would leave the second note-off orphaned and
       the note stuck on external gear. */
    voice_notes_t v;
    unsigned char rel[LIFE_MAX_HELD];
    voice_notes_init(&v);

    voice_arm(&v, 60, 100, 5);
    voice_arm(&v, 60, 100, 5);
    CHECK(voice_num_held(&v) == 1, "re-arming allocated a second slot");

    voice_tick(&v, 5, rel);
    CHECK(voice_num_held(&v) == 0, "note left held");
}

static void test_release_all_clears_everything(void) {
    voice_notes_t v;
    unsigned char rel[LIFE_MAX_HELD];
    voice_notes_init(&v);

    for (int i = 0; i < 16; ++i) voice_arm(&v, 40 + i, 100, 1000);
    CHECK(voice_num_held(&v) == 16, "did not hold a full 16-note chord");

    int n = voice_release_all(&v, rel);
    CHECK(n == 16, "release_all reported %d, wanted 16", n);
    CHECK(voice_num_held(&v) == 0, "release_all left notes held");
}

static void test_chord_of_sixteen_fits(void) {
    /* SEL_ALL on a fully live column must not drop notes. */
    voice_notes_t v;
    voice_notes_init(&v);
    for (int i = 0; i < LIFE_MAX_HELD; ++i)
        CHECK(voice_arm(&v, 40 + i, 100, 10) >= 0, "slot %d refused", i);
}

static void test_note_length_is_never_zero_ticks(void) {
    CHECK(voice_length_ticks(2, 10) >= 1, "a short step at 10%% produced a zero-length note");
    CHECK(voice_length_ticks(100, 50) == 50, "50%% of 100 ticks should be 50");
    CHECK(voice_length_ticks(100, 100) == 100, "100%% should be the whole step");
    CHECK(voice_length_ticks(100, 500) == 100, "length percent should clamp to 100");
}

static void test_poly_budget_reserves_one_per_melody(void) {
    /* 8 voices, one chord voice, three melodies -> the chord gets what is left */
    CHECK(voice_poly_budget(8, 3, 1, 16) == 5, "3 melodies + 1 chord gave %d, wanted 5",
          voice_poly_budget(8, 3, 1, 16));
    /* mute the melodies and the chord gets the whole pool */
    CHECK(voice_poly_budget(8, 0, 1, 16) == 8, "a lone chord should get all 8, got %d",
          voice_poly_budget(8, 0, 1, 16));
}

static void test_poly_budget_splits_between_chords(void) {
    /* two chord voices must not between them claim more than the pool */
    int b = voice_poly_budget(8, 2, 2, 16);
    CHECK(b == 3, "2 melodies + 2 chords gave %d each, wanted 3", b);
    CHECK(b * 2 + 2 <= 8, "two chords plus two melodies oversubscribe: %d", b * 2 + 2);

    int c = voice_poly_budget(8, 0, 4, 16);
    CHECK(c * 4 <= 8, "four chords oversubscribe the pool: %d", c * 4);
}

static void test_poly_budget_never_silences_a_voice(void) {
    /* even absurdly oversubscribed, an audible voice can still sound one note */
    for (int mono = 0; mono <= 12; ++mono)
        for (int chords = 0; chords <= 8; ++chords) {
            int b = voice_poly_budget(8, mono, chords, 16);
            CHECK(b >= 1, "budget %d with %d melodies and %d chords", b, mono, chords);
            if (b < 1) return;
        }
}

static void test_poly_budget_respects_the_held_ceiling(void) {
    CHECK(voice_poly_budget(64, 0, 1, 16) == 16, "must not exceed the held-note array");
}

/* --------------------------------------------------------------- ccmap.h -- */

static void test_cc_block_is_contiguous_and_bounded(void) {
    int base = CC_IN_DEFAULT_BASE;
    CHECK(cc_param_for_number(base - 1, base) == -1, "a CC below the block should be ignored");
    CHECK(cc_param_for_number(base, base) == CC_KEY, "the first CC should be KEY");
    CHECK(cc_param_for_number(cc_last_number(base), base) == CC_PARAM_COUNT - 1,
          "the last CC should be the last parameter");
    CHECK(cc_param_for_number(cc_last_number(base) + 1, base) == -1,
          "a CC past the block should be ignored");
    for (int i = 0; i < CC_PARAM_COUNT; ++i)
        CHECK(cc_param_for_number(base + i, base) == i, "CC %d did not map to param %d",
              base + i, i);
}

static void test_cc_block_can_be_disabled(void) {
    for (int cc = 0; cc < 128; ++cc)
        CHECK(cc_param_for_number(cc, 0) == -1, "base 0 must ignore CC %d", cc);
}

static void test_cc_block_clears_the_outgoing_sim_ccs_by_default(void) {
    /* The panel SENDS 20..27. The default input block must not sit on them. */
    for (int cc = 20; cc <= 27; ++cc)
        CHECK(cc_param_for_number(cc, CC_IN_DEFAULT_BASE) == -1,
              "default input block collides with outgoing CC %d", cc);
}

static void test_cc_index_spans_the_whole_range(void) {
    /* 0 must reach the first option and 127 the last, for every size we use. */
    const int counts[] = {2, 4, 7, 10, 11, 12, 15, 16, 29, 65};
    for (unsigned k = 0; k < sizeof(counts) / sizeof(counts[0]); ++k) {
        int n = counts[k];
        CHECK(cc_to_index(0, n) == 0, "cc 0 should give option 0 of %d", n);
        CHECK(cc_to_index(127, n) == n - 1, "cc 127 should give option %d of %d",
              n - 1, n);
        for (int v = 0; v <= 127; ++v) {
            int i = cc_to_index(v, n);
            CHECK(i >= 0 && i < n, "cc %d of %d options gave %d", v, n, i);
            if (i < 0 || i >= n) return;
        }
    }
}

static void test_cc_index_is_monotonic(void) {
    for (int n = 1; n <= 32; ++n) {
        int last = 0;
        for (int v = 0; v <= 127; ++v) {
            int i = cc_to_index(v, n);
            CHECK(i >= last, "cc_to_index went backwards at %d of %d options", v, n);
            if (i < last) return;
            last = i;
        }
    }
}

static void test_cc_to_range_hits_both_ends(void) {
    CHECK(cc_to_range(0, 1, 7) == 1, "octave low end");
    CHECK(cc_to_range(127, 1, 7) == 7, "octave high end");
    CHECK(cc_to_range(0, -7, 7) == -7, "pitch low end");
    CHECK(cc_to_range(127, -7, 7) == 7, "pitch high end");
    CHECK(cc_to_range(64, -7, 7) == 0, "pitch centre should be 0, got %d",
          cc_to_range(64, -7, 7));
}

static void test_cc_press_fires_once_per_press(void) {
    CHECK(cc_is_press(0, 127), "a button press should fire");
    CHECK(!cc_is_press(127, 127), "holding must not fire again");
    CHECK(!cc_is_press(127, 0), "releasing must not fire");
    CHECK(!cc_is_press(0, 63), "below halfway must not fire");
    CHECK(cc_is_press(63, 64), "halfway is the threshold");
}

static void test_cc_voice_and_group_split(void) {
    CHECK(cc_voice_for_param(CC_KEY) == -1, "KEY is global");
    CHECK(cc_voice_for_param(CC_STEP) == -1, "STEP is global");
    const int firsts[6] = { CC_MUTE_1, CC_RATE_1, CC_RULE_1, CC_ORDER_1,
                            CC_PITCH_1, CC_LENGTH_1 };
    for (int g = 0; g < 6; ++g)
        for (int v = 0; v < 4; ++v) {
            int p = firsts[g] + v;
            CHECK(cc_voice_for_param(p) == v, "param %d should be voice %d, got %d",
                  p, v, cc_voice_for_param(p));
            CHECK(cc_group_for_param(p) == g, "param %d should be group %d, got %d",
                  p, g, cc_group_for_param(p));
        }
}

static void test_cc_per_voice_params_are_four_apart(void) {
    /* A row of four knobs must hit the same parameter on the four voices. */
    CHECK(CC_RATE_2 - CC_RATE_1 == 1 && CC_RATE_4 - CC_RATE_1 == 3,
          "the four rate CCs must be consecutive");
    CHECK(CC_RULE_1 - CC_RATE_1 == 4, "parameter groups must be four apart");
}

static void test_cc_round_trip_is_stable(void) {
    /* The property two-way CC depends on: what we send must decode back to what
       we meant, for every option of every range in use. Without this an echo
       walks the value one step at a time. */
    const int counts[] = {2, 4, 7, 10, 11, 12, 15, 16, 29, 33, 65};
    for (unsigned k = 0; k < sizeof(counts) / sizeof(counts[0]); ++k) {
        int n = counts[k];
        for (int i = 0; i < n; ++i) {
            int cc = cc_from_index(i, n);
            int back = cc_to_index(cc, n);
            CHECK(back == i, "%d of %d encoded to %d and decoded to %d", i, n, cc, back);
            CHECK(cc >= 0 && cc <= 127, "encoded %d of %d out of range: %d", i, n, cc);
            if (back != i) return;
        }
    }
}

static void test_cc_range_round_trip(void) {
    for (int v = -7; v <= 7; ++v)
        CHECK(cc_to_range(cc_from_range(v, -7, 7), -7, 7) == v,
              "pitch %d did not survive the round trip", v);
    for (int v = 1; v <= 7; ++v)
        CHECK(cc_to_range(cc_from_range(v, 1, 7), 1, 7) == v,
              "octave %d did not survive the round trip", v);
    for (int v = 0; v <= 64; ++v)
        CHECK(cc_to_range(cc_from_range(v, 0, 64), 0, 64) == v,
              "floor %d did not survive the round trip", v);
}

static void test_cc_echo_is_a_fixed_point(void) {
    /* Feed our own output back in repeatedly: it must not drift. */
    const int counts[] = {12, 29, 11, 4, 10};
    for (unsigned k = 0; k < sizeof(counts) / sizeof(counts[0]); ++k) {
        int n = counts[k];
        for (int start = 0; start <= 127; ++start) {
            int idx = cc_to_index(start, n);
            for (int hop = 0; hop < 8; ++hop) {
                int cc = cc_from_index(idx, n);
                int next = cc_to_index(cc, n);
                CHECK(next == idx, "value drifted on echo %d: %d -> %d (%d options)",
                      hop, idx, next, n);
                if (next != idx) return;
                idx = next;
            }
        }
    }
}

/* -------------------------------------------------------------- chance.h -- */

static void test_chance_depth_zero_is_off(void) {
    chance_state_t c; chance_reset(&c, 1);
    for (int n = 0; n <= 8; ++n) {
        CHECK(chance_should_fire(0, n, &c), "depth 0 must always fire");
        CHECK(chance_ratchets(0, n) == 1, "depth 0 must not ratchet");
        CHECK(!chance_should_tie(0, 1, &c), "depth 0 must not tie");
    }
    CHECK(chance_pass_ok(0, &c), "depth 0 must play every pass");
    CHECK(chance_pass_divisor(0) == 1, "depth 0 divisor must be 1");
}

static void test_crowded_cells_always_fire(void) {
    /* 8 neighbours is certain at any depth - the world at its densest must not
       start dropping notes. */
    chance_state_t c; chance_reset(&c, 7);
    for (int depth = 0; depth <= 100; ++depth)
        for (int i = 0; i < 20; ++i)
            CHECK(chance_should_fire(depth, 8, &c), "a fully crowded cell was dropped at depth %d",
                  depth);
}

static void test_lonely_cells_drop_out_with_depth(void) {
    /* More depth must mean fewer lone cells firing, monotonically enough to be
       usable as a control. */
    int prev = 1000;
    for (int depth = 0; depth <= 100; depth += 20) {
        chance_state_t c; chance_reset(&c, 99);
        int fired = 0;
        for (int i = 0; i < 1000; ++i) fired += chance_should_fire(depth, 0, &c) ? 1 : 0;
        CHECK(fired <= prev, "depth %d fired %d, more than the shallower %d", depth, fired, prev);
        prev = fired;
    }
    CHECK(prev == 0, "a lone cell at full depth should never fire, got %d", prev);
}

static void test_ratchets_stay_in_range_and_grow(void) {
    for (int depth = 0; depth <= 100; ++depth)
        for (int n = 0; n <= 8; ++n) {
            int r = chance_ratchets(depth, n);
            CHECK(r >= 1 && r <= 4, "ratchets %d out of range at depth %d, %d neighbours",
                  r, depth, n);
            if (r < 1 || r > 4) return;
        }
    CHECK(chance_ratchets(100, 8) == 4, "full depth on a crowded cell should give 4");
    CHECK(chance_ratchets(100, 0) == 1, "a lone cell should never ratchet");
    for (int n = 1; n <= 8; ++n)
        CHECK(chance_ratchets(100, n) >= chance_ratchets(100, n - 1),
              "ratchets must not fall as crowding rises");
}

static void test_ratchets_are_deterministic(void) {
    /* Same cell, same answer - ratchets you cannot predict are unplayable. */
    for (int i = 0; i < 50; ++i)
        CHECK(chance_ratchets(70, 5) == chance_ratchets(70, 5), "ratchets are not deterministic");
}

static void test_only_survivors_tie(void) {
    chance_state_t c; chance_reset(&c, 3);
    for (int depth = 0; depth <= 100; ++depth)
        for (int i = 0; i < 10; ++i)
            CHECK(!chance_should_tie(depth, 0, &c), "a newly born cell must not tie");

    chance_state_t d; chance_reset(&d, 3);
    int tied = 0;
    for (int i = 0; i < 1000; ++i) tied += chance_should_tie(100, 1, &d) ? 1 : 0;
    CHECK(tied == 1000, "a survivor at full depth should always tie, got %d", tied);
}

static void test_pass_divisor_range_and_gating(void) {
    for (int depth = 0; depth <= 100; ++depth) {
        int n = chance_pass_divisor(depth);
        CHECK(n >= 1 && n <= 4, "pass divisor %d out of range at depth %d", n, depth);
        if (n < 1 || n > 4) return;
    }
    chance_state_t c; chance_reset(&c, 1);
    int n = chance_pass_divisor(100);
    int played = 0;
    for (c.pass = 0; c.pass < 40; ++c.pass) played += chance_pass_ok(100, &c) ? 1 : 0;
    CHECK(played == 40 / n, "every-Nth played %d of 40 with divisor %d", played, n);
}

/* ------------------------------------------------------------------------- */

int main(void) {
    test_blinker_oscillates();
    test_block_is_stable();
    test_glider_translates();
    test_glider_wraps_the_torus();
    test_column_mask();
    test_respawn_triggers_on_stalled_world();
    test_respawn_does_not_fire_on_a_live_world();
    test_respawn_fires_on_low_density();
    test_respawn_apply_adds_cells();
    test_respawn_apply_terminates_on_a_full_world();
    test_rulesets_are_well_formed();
    test_seeds_kills_every_live_cell();
    test_every_ruleset_is_stable_under_churn();
    test_out_of_range_ruleset_falls_back_to_conway();

    test_empty_column_is_a_rest_for_every_rule();
    test_first_and_last();
    test_up_ascends_in_pitch();
    test_down_descends_in_pitch();
    test_updown_pingpongs_without_repeating_endpoints();
    test_walk_picks_the_nearest();
    test_rise_and_fall_wrap();
    test_all_returns_the_whole_column();
    test_random_always_lands_on_a_live_cell();
    test_retained_rules_survive_the_column_shrinking();
    test_every_rule_lands_on_a_live_cell_under_churn();

    test_forward_wraps();
    test_reverse_wraps();
    test_pingpong_does_not_repeat_endpoints();
    test_traversal_stays_in_range();
    test_traversal_clamps_a_stale_position();
    test_traversal_names_are_four_chars();
    test_selection_names_are_four_chars();

    test_every_scale_contains_its_root();
    test_scale_masks_are_12_bit_and_unique();
    test_scale_names_are_four_chars();
    test_major_scale_degrees();
    test_negative_degrees_go_down_an_octave();
    test_pentatonic_wraps_at_five();
    test_notes_stay_in_midi_range();
    test_empty_mask_falls_back_to_chromatic();
    test_note_to_degree_round_trips();
    test_note_to_degree_snaps_off_scale_notes();
    test_note_to_degree_covers_the_midi_range();

    test_note_expires_after_its_length();
    test_rearming_the_same_note_does_not_double_allocate();
    test_release_all_clears_everything();
    test_chord_of_sixteen_fits();
    test_note_length_is_never_zero_ticks();
    test_poly_budget_reserves_one_per_melody();
    test_poly_budget_splits_between_chords();
    test_poly_budget_never_silences_a_voice();
    test_poly_budget_respects_the_held_ceiling();

    test_cc_block_is_contiguous_and_bounded();
    test_cc_block_can_be_disabled();
    test_cc_block_clears_the_outgoing_sim_ccs_by_default();
    test_cc_index_spans_the_whole_range();
    test_cc_index_is_monotonic();
    test_cc_to_range_hits_both_ends();
    test_cc_press_fires_once_per_press();
    test_cc_voice_and_group_split();
    test_cc_per_voice_params_are_four_apart();
    test_cc_round_trip_is_stable();
    test_cc_range_round_trip();
    test_cc_echo_is_a_fixed_point();

    test_chance_depth_zero_is_off();
    test_crowded_cells_always_fire();
    test_lonely_cells_drop_out_with_depth();
    test_ratchets_stay_in_range_and_grow();
    test_ratchets_are_deterministic();
    test_only_survivors_tie();
    test_pass_divisor_range_and_gating();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
