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

    test_note_expires_after_its_length();
    test_rearming_the_same_note_does_not_double_allocate();
    test_release_all_clears_everything();
    test_chord_of_sixteen_fits();
    test_note_length_is_never_zero_ticks();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
