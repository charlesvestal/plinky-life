/*
@Name: Life
@Author: Charles Vestal
@Firmware: latest
@Level: Advanced
@Tags: sequencer, generative, midi, cellular automata
@Description: A Game of Life sequencer. Four voices walk an evolving 16x16 world and play what they find alive.

Life turns Conway's Game of Life into a sequencer. The 16x16 grid is a living
palette, not a piano roll: columns are steps, rows are scale degrees, and the
automaton rewrites the whole thing underneath you on its own clock.

Four independent voices walk through the world, each at its own rate and in its
own direction. When a voice arrives at a column it looks at whichever cells are
alive there and picks one - topmost, nearest to the last note, random, climbing,
falling - or plays all of them at once as a chord. An empty column is a rest, so
the shape of the automaton becomes the rhythm.

Tap any pad to bring a cell to life. Hold the bottom-right pad for mutes, solos,
clear, respawn and transport. Everything else lives on the settings pages.
*/

/* ---------------------------------------------------------------------------
   This file does NOT compile on its own. It has no #includes (they are banned
   in panel code) and only type-checks once the IDE injects the SDK headers.

   The pure logic lives in the src/ headers and is desktop-tested by harness/tests.c.
   `sh build/amalgamate.sh` splices those headers in ahead of this file to
   produce the single plinky_life.cpp that gets flashed.
   ------------------------------------------------------------------------- */

#define PANEL_PAD_COLOR WHITE

/* --- Palette --------------------------------------------------------------

   SYSTEM_NOTES.md section 4: the dim tiers are far dimmer than they look.
   WHITE is already LED_RGB(15,15,15), so DIMMEST(WHITE) lands at 3 of 31 and
   DIMMESTEST at 1 - effectively invisible, and a real source of "dead LED" bug
   reports. Every colour here is therefore written out explicitly. */

#define LIFE_COL_ALIVE      LED_RGB(11, 11, 11)
#define LIFE_COL_ALIVE_DIM  LED_RGB(3, 3, 3)     /* world under the action layer */
#define LIFE_COL_MODIFIER   LED_RGB(20, 0, 16)
#define LIFE_COL_ACTION     LED_RGB(0, 10, 12)
#define LIFE_COL_DANGER     LED_RGB(24, 2, 0)
#define LIFE_COL_OFF        LED_RGB(2, 2, 2)

static const uint32_t life_voice_dim[4] = {
    LED_RGB(7, 0, 0), LED_RGB(0, 7, 0), LED_RGB(0, 1, 10), LED_RGB(6, 4, 0),
};
static const uint32_t life_voice_bright[4] = {
    LED_RGB(31, 4, 4), LED_RGB(6, 31, 6), LED_RGB(9, 9, 31), LED_RGB(31, 24, 0),
};

/* --- Rates ---------------------------------------------------------------

   clock_divider_t scales the transport's Q16 quarter-note phase by
   numerator/denominator, so the numerator is "steps per quarter note". */

typedef struct {
    short num;
    short den;
} life_rate_t;

static const life_rate_t life_rates[] = {
    { 8, 1 }, { 6, 1 }, { 4, 1 }, { 3, 1 }, { 2, 1 }, { 3, 2 },
    { 1, 1 }, { 1, 2 }, { 1, 4 }, { 1, 8 }, { 1, 16 }, { 1, 32 },
};

/* Kept as its own array, not a field of life_rate_t, so it can be handed
   straight to draw_system_style_enum_settings_page(...) as a const char* const*
   without building a temporary. */
static const char *const life_rate_names[] = {
    "32nd", "16T ", "16th", "8T  ", "8th ", "4T  ",
    "1/4 ", "1/2 ", "1BAR", "2BAR", "4BAR", "8BAR",
};
#define LIFE_NUM_RATES ((int)(sizeof(life_rates) / sizeof(life_rates[0])))

static const char *const life_note_names[12] = {
    "C   ", "C#  ", "D   ", "D#  ", "E   ", "F   ",
    "F#  ", "G   ", "G#  ", "A   ", "A#  ", "B   ",
};

static const char *const life_sink_names[3] = { "SYN ", "MIDI", "BOTH" };
enum { LIFE_SINK_SYNTH = 0, LIFE_SINK_MIDI, LIFE_SINK_BOTH };

static const char *const life_port_names[5] = { "OFF ", "USB1", "TRS1", "P1  ", "ALL " };
static const uint8_t life_port_values[5] = {
    MIDI_PORT_NONE, MIDI_PORT_USB1, MIDI_PORT_TRS1, MIDI_PORT_1,
    (uint8_t)(MIDI_PORT_1 | MIDI_PORT_2),
};

/* Simulation CCs. Fixed numbers, documented in the README so a patch on the
   receiving end can rely on them. */
#define LIFE_CC_DENSITY   20
#define LIFE_CC_BIRTHS    21
#define LIFE_CC_DEATHS    22
#define LIFE_CC_STABILITY 23
#define LIFE_CC_VOICE_RND 24   /* .. 27, one per voice */
#define LIFE_NUM_CCS      8

#define LIFE_NUM_VOICES 4

/* Row 15, following the Chords silkscreen - which is also the convention in
   ide_api.md's Global Transport section:

     (12,15) REC   - we have nothing to record, so it stays part of the world
     (13,15) x     - the stock SHIFT key, so the actions modifier lives here
     (14,15) square- stop
     (15,15) play

   Transport is PERMANENT and visible in every mode. It used to be buried under
   the modifier on (15,15), which is the pad Plinky users reach for to start
   playback - so the panel answered "how do I play this?" with a menu. Three
   cells are spent; they still simulate, they just cannot be seen or painted. */
#define LIFE_MODIFIER_X 13
#define LIFE_MODIFIER_Y 15
#define LIFE_STOP_X     14
#define LIFE_PLAY_X     15
#define LIFE_TRANSPORT_Y 15

#define LIFE_COL_PLAY_ON   LED_RGB(0, 31, 6)
#define LIFE_COL_PLAY_OFF  LED_RGB(0, 7, 2)
#define LIFE_COL_STOP_ON   LED_RGB(28, 3, 0)
#define LIFE_COL_STOP_OFF  LED_RGB(8, 1, 0)

/* Default step length, used for the very first note of a voice before two
   divider edges have been seen and the real interval is known. */
#define LIFE_DEFAULT_STEP_US 250000

/* How many of Plinky's synth voices this panel will claim. llm.txt: there are
   12 physical voices, but 8 "is the current practical default for user panels;
   using all 12 can exceed the DSP budget with heavier presets or effects". */
#define LIFE_SYNTH_VOICES 8

/* Every playhead claims at the same priority. An earlier version passed
   `1 + v`, which quietly made voice 4 outrank voice 1 and let the bass steal
   synth voices from the melody. That was the loop index leaking into a musical
   decision, not a musical decision. */
#define LIFE_VOICE_PRIO 2

/* The three things the grid can be showing. The grid is the entire screen, so
   these are modes rather than panes. */
enum { LIFE_UI_WORLD = 0, LIFE_UI_ACTION, LIFE_UI_VOICE, LIFE_UI_PRESET, LIFE_UI_LOAD };

/* The on-grid voice editor. One parameter per row, on odd rows only: the blank
   even rows between them are what makes seven stacked rows readable at arm's
   length on a 16x16. `n` is how many pads wide that row's range is. */
typedef struct {
    signed char y;
    signed char n;
    const char *name;
} life_param_row_t;

static const life_param_row_t life_param_rows[] = {
    {  1,  4, "VOICE" },   /* which voice this editor is editing */
    {  3, 12, "RATE"  },
    {  5, 11, "RULE"  },
    {  7,  4, "ORDER" },
    {  9, 16, "CHAN"  },
    { 11, 15, "PITCH" },   /* bipolar, centre pad is 0 */
    { 13, 10, "LENGTH" },
};
#define LIFE_NUM_PARAM_ROWS ((int)(sizeof(life_param_rows) / sizeof(life_param_rows[0])))
#define LIFE_PITCH_CENTRE 7

/* Voice N always plays preset N. There is no preset-selector row: four
   playheads and four presets is a direct mapping, and an indirection between
   them only earns you the question "which preset is voice 3 on again?".
   Presets 5-12 are still reachable - load one into slots 1-4 from the preset
   editor's own save/load picker. */
#define LIFE_LOAD_X  0
#define LIFE_SOUND_X 1
#define LIFE_SOUND_Y 15

struct life_panel : panel_t {
    /* --- world --- */
    life_world_t world;
    life_world_t scratch;
    life_respawn_t respawn;
    life_stats_t last_stats;

    /* --- per voice --- */
    clock_divider_t voice_div[LIFE_NUM_VOICES];
    traversal_state_t trav[LIFE_NUM_VOICES];
    selection_state_t sel[LIFE_NUM_VOICES];
    voice_notes_t notes[LIFE_NUM_VOICES];
    voice_allocator_t allocator;
    preset_pages_t preset_pages;   /* the stock synth editor, hosted per voice */

    uint32_t last_edge_us[LIFE_NUM_VOICES];
    uint32_t step_us[LIFE_NUM_VOICES];
    uint16_t last_played_mask[LIFE_NUM_VOICES];   /* for the UI flash */
    uint8_t voice_dev[LIFE_NUM_VOICES];           /* per-voice randomness CC */

    clock_divider_t gen_div;

    /* --- durable panel state (on_serialise) --- */
    uint16_t world_rows[LIFE_H];                  /* packed world, for saves */
    uint8_t v_enabled[LIFE_NUM_VOICES];
    uint8_t v_muted[LIFE_NUM_VOICES];
    uint8_t v_rate[LIFE_NUM_VOICES];
    uint8_t v_order[LIFE_NUM_VOICES];
    uint8_t v_rule[LIFE_NUM_VOICES];
    int8_t v_pitch[LIFE_NUM_VOICES];
    uint8_t v_length[LIFE_NUM_VOICES];
    int8_t v_channel[LIFE_NUM_VOICES];    /* MIDI channel 1-16, or -1 to follow the preset */

    uint8_t initialised;      /* 0 only on a zeroed arena - see on_load_finished */
    uint8_t gen_rate;
    uint8_t respawn_floor;
    uint8_t respawn_amount;
    uint8_t respawn_stable;
    uint8_t freeze_life;

    /* --- durable preferences (on_serialise_settings) --- */
    uint8_t pref_root;        /* 0..11 */
    uint8_t pref_scale;       /* index into life_scale_masks */
    uint8_t pref_octave;      /* base octave, 1..7 */
    uint8_t pref_sink;
    uint8_t pref_port;
    uint8_t pref_send_cc;

    /* --- transient UI state, never serialised --- */
    uint8_t edit_voice;       /* which voice the per-voice settings pages edit */
    bool modifier_held;
    uint8_t ui_mode;          /* LIFE_UI_WORLD / _ACTION / _VOICE */
    uint8_t solo_mask;
    uint8_t last_cc[LIFE_NUM_CCS];
    bool cc_primed;
    bool step_once_request;

    /* ---------------------------------------------------------------- */

    void reset_everything(void) {
        life_clear(&world);
        life_clear(&scratch);
        life_respawn_init(&respawn, 0x5eed1234u);
        last_stats.alive = last_stats.births = last_stats.deaths = 0;
        last_stats.unchanged = LIFE_CELLS;

        for (int v = 0; v < LIFE_NUM_VOICES; ++v) {
            traversal_reset(&trav[v], TRAV_FORWARD, LIFE_W, 0x1000u + v * 977u);
            selection_reset(&sel[v], 0x2000u + v * 613u);
            voice_notes_init(&notes[v]);
            last_edge_us[v] = 0;
            step_us[v] = LIFE_DEFAULT_STEP_US;
            last_played_mask[v] = 0;
            voice_dev[v] = 0;
        }

        solo_mask = 0;
        edit_voice = 0;
        modifier_held = false;
        ui_mode = LIFE_UI_WORLD;
        cc_primed = false;
        step_once_request = false;
        for (int i = 0; i < LIFE_NUM_CCS; ++i) last_cc[i] = 255;
    }

    void setup_default_panel_state() override {
        panel_t::setup_default_panel_state();

        /* SYSTEM_NOTES.md section 6b: this hook does NOT run on a staged load,
           which is how the IDE loads a panel. on_load_finished() covers that
           case; both call the same two functions so the defaults cannot drift
           apart between the two entry points. */
        setup_default_panel_state_fields_only();
        set_pref_defaults();

        reset_everything();
        rebuild_runtime();

        /* Seed a world so a fresh panel is immediately playable rather than
           silent - an empty Game of Life stays empty forever. */
        life_respawn_apply(&world, &respawn, 60);

        apply_scale_to_system();
    }

    void set_pref_defaults(void) {
        pref_root = 0;
        pref_scale = 9;            /* minor pentatonic - forgiving by default */
        pref_octave = 3;
        pref_sink = LIFE_SINK_BOTH;
        pref_port = 3;             /* MIDI_PORT_1 */
        pref_send_cc = 1;
    }

    void on_load_finished(void) override {
        panel_t::on_load_finished();

        /* SYSTEM_NOTES.md section 6b: setup_default_panel_state() does NOT run on
           a staged load, which is how the IDE loads a panel. The arena is zeroed,
           so without this the panel comes up with every voice DISABLED, an empty
           world and MIDI off - press play, hear nothing, with no clue why.

           `initialised` is serialised, so it is 1 in any real save and 0 only on
           a zeroed arena. That is what distinguishes "never set up" from "loaded
           a scene where the user deliberately muted everything". */
        bool fresh = !initialised;
        if (fresh) setup_default_panel_state_fields_only();

        /* pref_octave is 1..7, so 0 means the settings file never loaded either.
           Checked before clamp_settings(), which would turn that 0 into a 1. */
        if (pref_octave == 0) set_pref_defaults();

        clamp_settings();
        unpack_world();
        rebuild_runtime();

        if (fresh) {
            /* An empty Game of Life stays empty forever. Seed it so the panel is
               audible the moment transport starts. */
            life_respawn_apply(&world, &respawn, 60);
            initialised = 1;
        }

        apply_scale_to_system();
    }

    /* Everything that is derived rather than saved. Safe to call at any time. */
    void rebuild_runtime(void) {
        for (int v = 0; v < LIFE_NUM_VOICES; ++v) {
            traversal_reset(&trav[v], (traversal_order_t)v_order[v], LIFE_W, 0x1000u + v * 977u);
            selection_reset(&sel[v], 0x2000u + v * 613u);
            voice_notes_init(&notes[v]);
            last_edge_us[v] = 0;
            step_us[v] = LIFE_DEFAULT_STEP_US;
            last_played_mask[v] = 0;
        }
        /* The arena is zeroed before construction, which would leave these
           dividers with denominator 0. update() latches the requested values,
           but seed them anyway so no code path can divide by zero first. */
        for (int v = 0; v < LIFE_NUM_VOICES; ++v) {
            voice_div[v].numerator = life_rates[v_rate[v]].num;
            voice_div[v].denominator = life_rates[v_rate[v]].den;
        }
        gen_div.numerator = life_rates[gen_rate].num;
        gen_div.denominator = life_rates[gen_rate].den;

        life_respawn_init(&respawn, 0x5eed1234u);
        cc_primed = false;
        for (int i = 0; i < LIFE_NUM_CCS; ++i) last_cc[i] = 255;
    }

    /* A save can carry a value from an older build, or a field the save never
       wrote at all. SYSTEM_NOTES.md section 6b: absent fields keep whatever was
       already in the object, i.e. the PREVIOUS scene's value. Clamping on load
       is what stops that becoming an out-of-range index. */
    void clamp_settings(void) {
        for (int v = 0; v < LIFE_NUM_VOICES; ++v) {
            if (v_rate[v] >= LIFE_NUM_RATES) v_rate[v] = 4;
            if (v_order[v] >= TRAV_COUNT) v_order[v] = TRAV_FORWARD;
            if (v_rule[v] >= SEL_COUNT) v_rule[v] = SEL_FIRST;
            if (v_length[v] < 10) v_length[v] = 10;
            if (v_length[v] > 100) v_length[v] = 100;
            if (v_pitch[v] < -30) v_pitch[v] = -30;
            if (v_pitch[v] > 30) v_pitch[v] = 30;
            if (v_channel[v] < -1 || v_channel[v] > 16) v_channel[v] = (int8_t)(v + 1);
            v_enabled[v] = v_enabled[v] ? 1 : 0;
            v_muted[v] = v_muted[v] ? 1 : 0;
        }
        if (gen_rate >= LIFE_NUM_RATES) gen_rate = 8;
        if (respawn_floor > 200) respawn_floor = 12;
        if (respawn_amount < 1) respawn_amount = 1;
        if (respawn_amount > 64) respawn_amount = 64;
        if (respawn_stable > 32) respawn_stable = 4;
        if (pref_root > 11) pref_root = 0;
        if (pref_scale >= LIFE_NUM_SCALES) pref_scale = 9;
        if (pref_octave < 1) pref_octave = 1;
        if (pref_octave > 7) pref_octave = 7;
        if (pref_sink > LIFE_SINK_BOTH) pref_sink = LIFE_SINK_BOTH;
        if (pref_port > 4) pref_port = 3;
        if (edit_voice >= LIFE_NUM_VOICES) edit_voice = 0;
    }

    /* current_key / current_scale are system globals shared across panels, so
       choosing a scale here drives the whole instrument rather than keeping a
       private harmonic state. */
    void apply_scale_to_system(void) {
        set_current_key_and_scale((uint8_t)(pref_root & 11), life_scale_masks[pref_scale]);
    }

    uint16_t scale_mask(void) const { return life_scale_masks[pref_scale]; }
    int root_note(void) const { return (int)pref_octave * 12 + (int)pref_root; }
    uint8_t midi_ports(void) const { return life_port_values[pref_port]; }

    /* --- world packing ---------------------------------------------------

       The world goes into the save as 16 row bitmasks rather than 256 JSON
       integers. */
    void pack_world(void) {
        for (int y = 0; y < LIFE_H; ++y) {
            uint16_t m = 0;
            for (int x = 0; x < LIFE_W; ++x)
                if (world.cell[y * LIFE_W + x]) m |= (uint16_t)(1u << x);
            world_rows[y] = m;
        }
    }

    void unpack_world(void) {
        for (int y = 0; y < LIFE_H; ++y)
            for (int x = 0; x < LIFE_W; ++x)
                world.cell[y * LIFE_W + x] = (world_rows[y] & (1u << x)) ? 1 : 0;
    }

    /* ================================================================== */
    /* Note output                                                        */
    /* ================================================================== */

    uint32_t source_id_for(int v, int note) const {
        return 0x11FE0000u | ((uint32_t)v << 8) | (uint32_t)(note & 127);
    }

    /* Voice N plays preset N. Deliberately not configurable - see LIFE_SOUND_X. */
    int preset_for(int v) const { return v; }

    /* -1 means "follow the preset's own channel", which is what
       get_midi_channel_for_preset_idx(...) is for. Otherwise the user pinned an
       explicit channel; the API wants a zero-based wire channel. */
    int midi_wire_channel_for(int v) const {
        if (v_channel[v] >= 1 && v_channel[v] <= 16) return v_channel[v] - 1;
        return -1;
    }

    bool midi_enabled(void) const {
        return (pref_sink == LIFE_SINK_MIDI || pref_sink == LIFE_SINK_BOTH) && midi_ports() != 0;
    }

    bool synth_enabled(void) const {
        return pref_sink == LIFE_SINK_SYNTH || pref_sink == LIFE_SINK_BOTH;
    }

    /* --- MIDI is LEVEL-TRIGGERED ------------------------------------------

       declare_midi_note_for_preset_idx(...) + send_declared_midi_notes() take
       "which notes should be down right now" and derive the on/off/aftertouch
       traffic themselves. That deletes the entire stuck-note failure mode on
       the MIDI side: there is no note-off to forget, because a note that stops
       being declared stops sounding.

       The commit marker runs EVERY sequence frame, not only frames that made a
       note. That is what makes muting, a rate change, transport stop and a sink
       change all release correctly without any of them knowing they had to. */
    void declare_midi_for_frame(void) {
        if (midi_enabled()) {
            uint8_t ports = midi_ports();
            for (int v = 0; v < LIFE_NUM_VOICES; ++v) {
                if (!voice_is_audible(v)) continue;
                for (int i = 0; i < LIFE_MAX_HELD; ++i) {
                    const held_note_t *h = &notes[v].held[i];
                    if (!h->active) continue;
                    declare_midi_note_for_preset_idx(preset_for(v), h->note, h->vel, ports,
                                                     midi_wire_channel_for(v));
                }
            }
        }
        send_declared_midi_notes();
    }

    void synth_note_on(int v, int note, int velocity) {
        if (!synth_enabled()) return;
        uint32_t sid = source_id_for(v, note);
        int old_voice = allocator.find_voice(sid);
        int new_voice = allocator.voice_allocate(sid, LIFE_VOICE_PRIO, 0, LIFE_SYNTH_VOICES);
        if (new_voice < 0) return;
        if (new_voice != old_voice && old_voice >= 0) synth_note_up(old_voice);
        play_synth(new_voice, preset_for(v), velocity, note << 8, true);
    }

    void synth_note_off(int v, int note) {
        uint32_t sid = source_id_for(v, note);
        int voice = allocator.find_voice(sid);
        if (voice >= 0) {
            synth_note_up(voice);
            allocator.voice_deallocate(sid);
        }
    }

    /* The single exit path for a voice. Mute, rate change, transport stop,
       solo change and panel unload all come through here - stuck MIDI notes are
       the classic failure mode for a sequencer with an external sink, and one
       release function is what makes them impossible. */
    void release_voice(int v) {
        uint8_t released[LIFE_MAX_HELD];
        int n = voice_release_all(&notes[v], released);
        for (int i = 0; i < n; ++i) synth_note_off(v, released[i]);
        last_played_mask[v] = 0;   /* MIDI needs nothing: it is level-triggered */
    }

    void release_all_voices(void) {
        for (int v = 0; v < LIFE_NUM_VOICES; ++v) release_voice(v);
    }

    bool voice_is_audible(int v) const {
        if (!v_enabled[v] || v_muted[v]) return false;
        if (solo_mask && !(solo_mask & (1u << v))) return false;
        return true;
    }

    /* ================================================================== */
    /* Sequencing - core0, IRQ context. Keep it short and deterministic.  */
    /* ================================================================== */

    void step_generation(void) {
        life_stats_t st;
        life_step(&world, &scratch, &st);
        for (int i = 0; i < LIFE_CELLS; ++i) world.cell[i] = scratch.cell[i];
        last_stats = st;

        if (life_respawn_tick(&respawn, &st, (int)respawn_floor, (int)respawn_stable))
            life_respawn_apply(&world, &respawn, (int)respawn_amount);

        if (pref_send_cc) send_simulation_ccs();
    }

    /* How many notes this playhead may sound at once.

       Every audible playhead is guaranteed one voice, so a chord can never
       starve a melody. Whatever is left over goes to the playheads on ALL, split
       between them - so a single ALL with the others muted gets the whole synth,
       which is the point of putting a voice on ALL in the first place.

       With MIDI alone there is no ceiling to respect, so chords go out whole.
       When the synth is in play the limit applies to BOTH sinks, so external
       gear and the internal synth always agree about what is sounding. */
    int poly_budget(int v) const {
        if (!synth_enabled()) return LIFE_MAX_HELD;
        if (v_rule[v] != SEL_ALL) return 1;

        int mono = 0, chords = 0;
        for (int i = 0; i < LIFE_NUM_VOICES; ++i) {
            if (!voice_is_audible(i)) continue;
            if (v_rule[i] == SEL_ALL) ++chords;
            else ++mono;
        }
        return voice_poly_budget(LIFE_SYNTH_VOICES, mono, chords, LIFE_MAX_HELD);
    }

    void fire_voice(int v) {
        int col = traversal_position(&trav[v], LIFE_W);
        uint16_t col_mask = life_column_mask(&world, col);
        int prev = sel[v].prev_row;
        uint16_t chosen = selection_pick(&sel[v], col_mask, (selection_rule_t)v_rule[v]);

        last_played_mask[v] = chosen;
        if (!chosen) return;                       /* an empty column is a rest */

        voice_dev[v] = (uint8_t)selection_deviation(&sel[v], prev);

        int ticks = voice_length_ticks((int)step_us[v], (int)v_length[v]);
        int budget = poly_budget(v);
        int sounded = 0;

        /* Lowest pitch first (row 15 up), so a chord that has to be trimmed
           keeps its roots and loses the top rather than the other way round. */
        for (int y = LIFE_H - 1; y >= 0; --y) {
            if (!(chosen & (1u << y))) continue;
            if (sounded >= budget) break;
            int degree = (15 - y) + (int)v_pitch[v];
            int note = life_degree_to_note(degree, root_note(), scale_mask());

            /* The automaton drives dynamics: a cell in a crowded neighbourhood
               hits harder than a lone one. */
            int n = life_neighbours(&world, col, y);
            int velocity = 68 + n * 7;
            if (velocity > 127) velocity = 127;

            if (voice_arm(&notes[v], note, velocity, ticks) >= 0) {
                synth_note_on(v, note, velocity);
                ++sounded;
            }
        }
    }

    void on_sequence(int delta_time_us) override {
        int64_t phase = get_clock_phase();
        bool playing = is_transport_playing();

        /* Expire held notes first, so a note that ends exactly as the next one
           begins does not leave the new note's slot stolen from under it. */
        for (int v = 0; v < LIFE_NUM_VOICES; ++v) {
            uint8_t released[LIFE_MAX_HELD];
            int n = voice_tick(&notes[v], delta_time_us, released);
            for (int i = 0; i < n; ++i) synth_note_off(v, released[i]);
        }

        if (!playing) {
            /* Transport stopped: silence everything and hold position. The
               world only advances via the action layer's single-step pad. */
            for (int v = 0; v < LIFE_NUM_VOICES; ++v)
                if (voice_num_held(&notes[v])) release_voice(v);
            if (step_once_request) {
                step_once_request = false;
                step_generation();
            }
            declare_midi_for_frame();   /* nothing declared -> everything releases */
            return;
        }

        if (step_once_request) {
            step_once_request = false;
            step_generation();
        }

        const life_rate_t *gr = &life_rates[gen_rate];
        int gen_edges = gen_div.update(phase, gr->num, gr->den, UPDATE_DIV_ON_QUARTER_NOTE);
        if (!freeze_life && gen_edges > 0 && sequencer_should_advance_playhead()) {
            /* A long stall can report many crossed edges at once. Cap the catch
               up so a seek cannot burn hundreds of generations inside an IRQ. */
            if (gen_edges > 4) gen_edges = 4;
            for (int i = 0; i < gen_edges; ++i) step_generation();
        }

        uint32_t now = time_us();
        for (int v = 0; v < LIFE_NUM_VOICES; ++v) {
            const life_rate_t *r = &life_rates[v_rate[v]];
            int edges = voice_div[v].update(phase, r->num, r->den, UPDATE_DIV_ON_QUARTER_NOTE);
            if (edges <= 0) continue;

            if (!voice_is_audible(v)) {
                if (voice_num_held(&notes[v])) release_voice(v);
                continue;
            }

            if (last_edge_us[v]) {
                uint32_t dt = now - last_edge_us[v];
                if (dt > 1000 && dt < 30000000u) step_us[v] = dt;
            }
            last_edge_us[v] = now;

            if (sequencer_should_advance_playhead())
                traversal_advance(&trav[v], (traversal_order_t)v_order[v], LIFE_W);
            fire_voice(v);
        }

        /* Once per frame, after every voice has had its say. */
        declare_midi_for_frame();
    }

    /* --- simulation CCs --------------------------------------------------

       Sent on generation edges only, never per sequence tick - that IS the
       throttle - and only when a value actually changed, so a settled world
       stops talking instead of flooding the bus. */
    void send_cc_if_changed(int idx, int cc, int value) {
        if (value < 0) value = 0;
        if (value > 127) value = 127;
        if (cc_primed && last_cc[idx] == (uint8_t)value) return;
        last_cc[idx] = (uint8_t)value;
        uint8_t ports = midi_ports();
        if (ports) midi_write_cc(ports, get_system_midi_channel() - 1, cc, value);
    }

    void send_simulation_ccs(void) {
        send_cc_if_changed(0, LIFE_CC_DENSITY, last_stats.alive * 127 / LIFE_CELLS);
        send_cc_if_changed(1, LIFE_CC_BIRTHS, last_stats.births * 127 / 64);
        send_cc_if_changed(2, LIFE_CC_DEATHS, last_stats.deaths * 127 / 64);
        send_cc_if_changed(3, LIFE_CC_STABILITY, last_stats.unchanged * 127 / LIFE_CELLS);
        for (int v = 0; v < LIFE_NUM_VOICES; ++v)
            send_cc_if_changed(4 + v, LIFE_CC_VOICE_RND + v, voice_dev[v]);
        cc_primed = true;
    }

    /* ================================================================== */
    /* Drawing and touch - core0 foreground                               */
    /* ================================================================== */

    /* Which voices are sitting on this column, as a bitmask. */
    uint8_t voices_on_column(int x) const {
        uint8_t m = 0;
        for (int v = 0; v < LIFE_NUM_VOICES; ++v) {
            if (!voice_is_audible(v)) continue;
            if (traversal_position(&trav[v], LIFE_W) == x) m |= (uint8_t)(1u << v);
        }
        return m;
    }

    uint32_t cell_colour(int x, int y, bool dim) const {
        bool alive = world.cell[y * LIFE_W + x] != 0;
        uint8_t on_col = voices_on_column(x);

        if (on_col) {
            for (int v = 0; v < LIFE_NUM_VOICES; ++v) {
                if (!(on_col & (1u << v))) continue;
                /* the cell this voice actually chose flashes at full brightness */
                if (last_played_mask[v] & (1u << y)) return life_voice_bright[v];
            }
            if (alive) return dim ? LIFE_COL_ALIVE_DIM : LIFE_COL_ALIVE;
            /* the playhead column itself, tinted by its lowest-numbered voice */
            for (int v = 0; v < LIFE_NUM_VOICES; ++v)
                if (on_col & (1u << v)) return life_voice_dim[v];
        }

        if (alive) return dim ? LIFE_COL_ALIVE_DIM : LIFE_COL_ALIVE;
        return 0;
    }

    /* --- the action layer -------------------------------------------------

       Pads that do something while the modifier is held. Everything else keeps
       showing the world, dimmed, so you never lose your place. */

    bool is_transport_pad(int x, int y) const {
        return y == LIFE_TRANSPORT_Y && (x == LIFE_STOP_X || x == LIFE_PLAY_X);
    }

    uint32_t transport_colour(int x) const {
        bool playing = is_transport_playing();
        if (x == LIFE_PLAY_X) return playing ? LIFE_COL_PLAY_ON : LIFE_COL_PLAY_OFF;
        return playing ? LIFE_COL_STOP_OFF : LIFE_COL_STOP_ON;
    }

    void do_transport(int x) {
        if (x == LIFE_PLAY_X) {
            if (!is_transport_playing()) start_transport();
        } else {
            stop_transport();
            release_all_voices();
        }
        printf("life: transport now %s\n", is_transport_playing() ? "playing" : "stopped");
    }

    bool is_action_pad(int x, int y) const {
        if (y == 13) return x < 4;                 /* 0-3 edit that voice */
        if (y == 14) return x < 8;                 /* 0-3 mute, 4-7 solo */
        if (y == 15) return x < 4;                 /* clear seed freeze step */
        return false;
    }

    uint32_t action_colour(int x, int y) const {
        if (y == 13 && x < 4) return life_voice_dim[x];
        if (y == 14) {
            if (x < 4) return voice_is_audible(x) ? life_voice_bright[x] : LIFE_COL_OFF;
            if (x < 8) return (solo_mask & (1u << (x - 4))) ? life_voice_bright[x - 4]
                                                            : life_voice_dim[x - 4];
        } else if (y == 15) {
            switch (x) {
            case 0: return LIFE_COL_DANGER;
            case 1: return LIFE_COL_ACTION;
            case 2: return freeze_life ? LIFE_COL_DANGER : LIFE_COL_ACTION;
            case 3: return LIFE_COL_ACTION;
            default: break;
            }
        }
        return cell_colour(x, y, true);            /* the dimmed world underneath */
    }

    static const char *action_help(int x, int y) {
        if (y == 13 && x < 4) return "open this voice's editor - rate, rule, sound";
        if (y == 14) return x < 4 ? "mute this voice"
                                  : "solo this voice - silences the other three";
        if (y == 15) switch (x) {
        case 0: return "clear the world - every cell dies";
        case 1: return "sprinkle new cells in right now";
        case 2: return "freeze evolution - the palette stops changing";
        case 3: return "advance exactly one generation, even while stopped";
        default: break;
        }
        return nullptr;
    }

    void do_action(int x, int y) {
        printf("life: action pad (%d,%d)\n", x, y);
        if (y == 13 && x < 4) {
            edit_voice = (uint8_t)x;
            ui_mode = LIFE_UI_VOICE;
            return;
        }
        if (y == 14 && x < 4) {
            v_muted[x] = v_muted[x] ? 0 : 1;
            if (!voice_is_audible(x)) release_voice(x);
            return;
        }
        if (y == 14 && x < 8) {
            solo_mask ^= (uint8_t)(1u << (x - 4));
            for (int i = 0; i < LIFE_NUM_VOICES; ++i)
                if (!voice_is_audible(i)) release_voice(i);
            return;
        }
        if (y != 15) return;
        switch (x) {
        case 0: { on_sequence_lock_guard_t guard; life_clear(&world); break; }
        case 1: { on_sequence_lock_guard_t guard;
                  life_respawn_apply(&world, &respawn, (int)respawn_amount); break; }
        case 2: freeze_life = freeze_life ? 0 : 1; break;
        case 3: step_once_request = true; break;
        default: break;
        }
    }

    /* --- the on-grid voice editor -----------------------------------------

       Reachable in one tap instead of fourteen side-button clicks, and every
       parameter is visible at once rather than one page at a time. Selected pad
       bright, the rest of the row dim so you can see how far the range goes. */

    int param_row_for_y(int y) const {
        for (int i = 0; i < LIFE_NUM_PARAM_ROWS; ++i)
            if (life_param_rows[i].y == y) return i;
        return -1;
    }

    int get_param(int row) const {
        int v = edit_voice;
        switch (row) {
        case 0: return edit_voice;
        case 1: return v_rate[v];
        case 2: return v_rule[v];
        case 3: return v_order[v];
        case 4: return (v_channel[v] >= 1 && v_channel[v] <= 16) ? v_channel[v] - 1 : v;
        case 5: return v_pitch[v] + LIFE_PITCH_CENTRE;
        case 6: return (v_length[v] - 10) / 10;
        default: return 0;
        }
    }

    void set_param(int row, int i) {
        int v = edit_voice;
        switch (row) {
        case 0: edit_voice = (uint8_t)i; return;
        case 1:
            /* the step length just changed, so the held note's countdown is
               measured against the wrong interval */
            release_voice(v);
            v_rate[v] = (uint8_t)i;
            last_edge_us[v] = 0;
            step_us[v] = LIFE_DEFAULT_STEP_US;
            return;
        case 2: v_rule[v] = (uint8_t)i; return;
        case 3: v_order[v] = (uint8_t)i; return;
        case 4: release_voice(v); v_channel[v] = (int8_t)(i + 1); return;
        case 5: v_pitch[v] = (int8_t)(i - LIFE_PITCH_CENTRE); return;
        case 6: v_length[v] = (uint8_t)(10 + i * 10); return;
        default: return;
        }
    }

    uint32_t voice_colour(int x, int y) const {
        if (y == LIFE_SOUND_Y && x == LIFE_LOAD_X) return LED_RGB(6, 4, 14);
        if (y == LIFE_SOUND_Y && x == LIFE_SOUND_X) return LIFE_COL_ACTION;
        int row = param_row_for_y(y);
        if (row < 0) return 0;                       /* the blank separator rows */
        if (x >= life_param_rows[row].n) return 0;

        /* the voice selector paints each pad in its own voice's colour */
        if (row == 0) return (x == edit_voice) ? life_voice_bright[x] : life_voice_dim[x];

        int v = edit_voice;
        if (x == get_param(row)) return life_voice_bright[v];
        /* PITCH centre stays findable even when it is not selected */
        if (row == 5 && x == LIFE_PITCH_CENTRE) return LIFE_COL_OFF;
        return life_voice_dim[v];
    }

    /* Per-pad help, not per-row. Touching a RULE pad should say what that rule
       DOES - it is the most opaque part of the panel and the second screen is
       the only place words can appear.

       Everything returned here is a static string. A shared formatting buffer
       would be wrong: this runs for all 256 pads each frame and the widget layer
       keeps the pointer, so a later pad would overwrite the touched one's text. */
    const char *voice_help(int x, int y) const {
        if (y == LIFE_SOUND_Y && x == LIFE_LOAD_X) return "load a preset into this voice";
        if (y == LIFE_SOUND_Y && x == LIFE_SOUND_X) return "edit this voice's sound";

        int row = param_row_for_y(y);
        if (row < 0 || x >= life_param_rows[row].n) return nullptr;

        switch (row) {
        case 0: return "which voice you are editing";
        case 1: return life_rate_names[x];
        case 2: return life_sel_help[x];
        case 3: return life_trav_help[x];
        case 4: return "MIDI channel for this voice";
        case 5: return "pitch offset in scale degrees; the dim centre pad is 0";
        case 6: return "note length, as a share of this voice's step";
        default: return life_param_rows[row].name;
        }
    }

    void do_voice_edit(int x, int y) {
        if (y == LIFE_SOUND_Y && x == LIFE_LOAD_X) {
            ui_mode = LIFE_UI_LOAD;
            return;
        }
        if (y == LIFE_SOUND_Y && x == LIFE_SOUND_X) {
            ui_mode = LIFE_UI_PRESET;
            return;
        }
        int row = param_row_for_y(y);
        if (row < 0 || x >= life_param_rows[row].n) return;
        set_param(row, x);
    }

    /* One line describing the whole voice, for the second-screen help view -
       the only place this panel can show words. */
    void set_voice_help_text(void) {
        int v = edit_voice;
        set_help_text("#fc2#*Voice %d#. every %s, %s going %s. ch %d, pitch %+d, len %d%%",
                      v + 1, life_rate_names[v_rate[v]], life_sel_help[v_rule[v]],
                      life_trav_names[v_order[v]],
                      (v_channel[v] >= 1 && v_channel[v] <= 16) ? v_channel[v] : v + 1,
                      (int)v_pitch[v], (int)v_length[v]);
    }

    /* --- the hosted synth editor ------------------------------------------

       preset_pages_t is the stock preset editor and it takes a preset_idx, so
       handing it this voice's preset makes it edit this voice's sound. We write
       none of it: two slider banks, the XY pad, the flag buttons and the LFO
       controls are all the firmware's.

       ide_api.md, Choosing The Right Layer: "start from the largest matching
       helper and only drop down a layer for the parts that are genuinely
       custom." Routing a playhead to a preset is custom. Editing that preset
       is not.

       Layout follows the reference panel in llm.txt exactly:
         rows 0..9    two 16-wide slider banks (flag buttons suppressed)
         x8..15 rows 10..14  the synth XY pad and its LFO/env buttons
         x0..7  rows 10..14  ours: voice selector and the flag buttons
         row 15       transport, drawn by the caller */
    void draw_preset_editor(void) {
        int preset = preset_for(edit_voice);

        preset_pages.edit(preset, 0, 0, false);
        preset_pages.xy_pad(preset, 8, 10);

        /* Switch voice without leaving: the editor follows whichever voice is
           selected, which is the whole point of hosting it here. */
        for (int v = 0; v < LIFE_NUM_VOICES; ++v)
            if (button(v, 10, v == edit_voice ? life_voice_bright[v] : life_voice_dim[v],
                       NOT_ISOLATED, "edit this voice's sound"))
                edit_voice = (uint8_t)v;

        synth_flags_button(0, 12, preset, SYNTH_FLAG_BUTTON_SIMPLE);
        synth_flags_button(1, 12, preset, SYNTH_FLAG_BUTTON_TUNE);
        synth_flags_button(2, 12, preset, SYNTH_FLAG_BUTTON_CHOP);
        synth_flags_button(3, 12, preset, SYNTH_FLAG_BUTTON_LOOP);
        synth_flags_button(4, 12, preset, SYNTH_FLAG_BUTTON_SYNC);
        synth_flags_button(5, 12, preset, SYNTH_FLAG_BUTTON_LOWPASS_GATE);

        /* back to the voice editor */
        if (button(0, 14, LIFE_COL_ACTION, NOT_ISOLATED, "back to the voice editor"))
            ui_mode = LIFE_UI_VOICE;
    }

    /* The preset picker: folders on the left, 64 slots on the right, with its
       own cancel and OK buttons.

       It places those buttons at (14, y+15) and (15, y+15) - exactly where our
       stop and play pads live. So this is the ONE mode that does not draw
       transport: the picker owns row 15 while it is up. That is the right call
       for a modal file dialog, and x still escapes because the picker leaves
       (13,15) alone. */
    void draw_preset_loader(void) {
        int r = preset_pages.saveload_action(preset_for(edit_voice), 0);
        if (r != 0) ui_mode = LIFE_UI_VOICE;   /* loaded, saved, or cancelled */
    }

    /* --- settings pages -------------------------------------------------

       Rendered with the system helpers, which return the left-button edit
       delta. The right pair page; the left pair adjust. Both are system
       territory on every faceplate, so the panel never touches them. */

    /* Global only. Per-voice config moved onto the grid, where it is one tap
       deep and visible all at once instead of fourteen side-button clicks. */
    int settings_page_count(void) { return 10; }
    int get_num_panel_settings_pages(void) override { return settings_page_count(); }

    static int clamp_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    /* Every settings page emits help text.

       The 4-character label on the grid is all the hardware can show, and "SIM"
       or "STAB" tells you nothing on its own. The second-screen view is the only
       place this panel can use words, so each page says what it does, what the
       value means right now, and what it affects.

       Markup per ide_api.md: `#fc2` is a theme-rotated 3-digit hex colour, `#*`
       is bold, and `#.` resets - styles are sticky until reset. */
    void draw_settings(int page) {
        int idx = -1 - page;                       /* page -1 is our page 0 */
        char buf[8];

        switch (idx) {
        case 0: {
            int d = draw_system_style_enum_settings_page("KEY ", pref_root, life_note_names, 12);
            if (d) { pref_root = (uint8_t)((pref_root + d + 120) % 12); apply_scale_to_system(); }
            set_help_text("#fc2#*Key#. - root note, now #fc2#*%s#.. Sets the whole instrument's "
                          "key, not just this panel.", life_note_names[pref_root]);
            break;
        }
        case 1: {
            int d = draw_system_style_enum_settings_page("SCAL", pref_scale, life_scale_names,
                                                        LIFE_NUM_SCALES);
            if (d) {
                pref_scale = (uint8_t)clamp_int(pref_scale + d, 0, LIFE_NUM_SCALES - 1);
                apply_scale_to_system();
            }
            set_help_text("#fc2#*Scale#. - #fc2#*%s#.. Grid rows are degrees of this scale, so "
                          "every cell is in key. %d of %d.",
                          life_scale_long_names[pref_scale], pref_scale + 1, LIFE_NUM_SCALES);
            break;
        }
        case 2: {
            snprintf(buf, sizeof(buf), "%d", pref_octave);
            int d = draw_system_style_settings_page("OCT ", buf, pref_octave * 100 / 7);
            if (d) pref_octave = (uint8_t)clamp_int(pref_octave + d, 1, 7);
            set_help_text("#fc2#*Octave#. - base octave #fc2#*%d#. of 7. The bottom grid row "
                          "plays %s%d; higher rows climb the scale from there.",
                          pref_octave, life_note_names[pref_root], pref_octave);
            break;
        }
        case 3: {
            int d = draw_system_style_enum_settings_page("GEN ", gen_rate, life_rate_names,
                                                        LIFE_NUM_RATES);
            if (d) gen_rate = (uint8_t)clamp_int(gen_rate + d, 0, LIFE_NUM_RATES - 1);
            set_help_text("#fc2#*Generation rate#. - the world evolves every #fc2#*%s#.. This is "
                          "separate from the voice rates: slow it down for a palette that breathes.",
                          life_rate_names[gen_rate]);
            break;
        }
        case 4: {
            snprintf(buf, sizeof(buf), "%d", respawn_floor);
            int d = draw_system_style_settings_page("FLOR", buf, respawn_floor * 100 / 64);
            if (d) respawn_floor = (uint8_t)clamp_int(respawn_floor + d, 0, 64);
            set_help_text("#fc2#*Respawn floor#. - if fewer than #fc2#*%d#. of 256 cells are "
                          "alive, sprinkle new ones in. 0 disables it and lets the world die.",
                          respawn_floor);
            break;
        }
        case 5: {
            snprintf(buf, sizeof(buf), "%d", respawn_amount);
            int d = draw_system_style_settings_page("SEED", buf, respawn_amount * 100 / 64);
            if (d) respawn_amount = (uint8_t)clamp_int(respawn_amount + d, 1, 64);
            set_help_text("#fc2#*Respawn amount#. - how many cells to sprinkle, now #fc2#*%d#.. "
                          "Also what the SEED pad in the action layer drops in.", respawn_amount);
            break;
        }
        case 6: {
            snprintf(buf, sizeof(buf), "%d", respawn_stable);
            int d = draw_system_style_settings_page("STAB", buf, respawn_stable * 100 / 32);
            if (d) respawn_stable = (uint8_t)clamp_int(respawn_stable + d, 0, 32);
            if (respawn_stable)
                set_help_text("#fc2#*Stall limit#. - after #fc2#*%d#. generations with nothing "
                              "changing at all, respawn. Blinkers keep changing, so they never "
                              "count as stalled.", respawn_stable);
            else
                set_help_text("#fc2#*Stall limit#. - #fc2#*off#.. A world frozen into still lifes "
                              "will stay frozen; only the floor can rescue it.");
            break;
        }
        case 7: {
            int d = draw_system_style_enum_settings_page("OUT ", pref_sink, life_sink_names, 3);
            if (d) {
                release_all_voices();              /* never strand a note on the old sink */
                pref_sink = (uint8_t)clamp_int(pref_sink + d, 0, 2);
            }
            set_help_text("#fc2#*Output#. - %s.",
                          pref_sink == LIFE_SINK_SYNTH ? "#fc2#*Plinky's own synth#. only, no MIDI"
                          : pref_sink == LIFE_SINK_MIDI ? "#fc2#*MIDI#. only - silent on its own"
                                                        : "#fc2#*both#. the internal synth and MIDI");
            break;
        }
        case 8: {
            int d = draw_system_style_enum_settings_page("PORT", pref_port, life_port_names, 5);
            if (d) {
                release_all_voices();
                pref_port = (uint8_t)clamp_int(pref_port + d, 0, 4);
            }
            set_help_text("#fc2#*MIDI port#. - %s. Each voice sends on its own channel, set in "
                          "the voice editor.",
                          pref_port == 0 ? "#fc2#*off#. - no MIDI leaves the panel"
                          : pref_port == 1 ? "#fc2#*USB 1#."
                          : pref_port == 2 ? "#fc2#*TRS 1#."
                          : pref_port == 3 ? "#fc2#*port 1#., USB and TRS"
                                           : "#fc2#*every port#., USB and TRS 1 and 2");
            break;
        }
        case 9: {
            bool on = pref_send_cc != 0;
            int d = draw_system_style_bool_settings_page("CC  ", on);
            if (d) pref_send_cc = on ? 0 : 1;
            if (pref_send_cc)
                set_help_text("#fc2#*Simulation CCs#. - #fc2#*on#.. Sends CC20 density, 21 births, "
                              "22 deaths, 23 stability and 24-27 per-voice movement, once per "
                              "generation. Modulation from the automaton itself.");
            else
                set_help_text("#fc2#*Simulation CCs#. - #fc2#*off#.. Turn on to send CC20-27 "
                              "describing the world: density, births, deaths, stability and "
                              "per-voice movement.");
            break;
        }
        default:
            break;
        }
    }

    void on_ui(int delta_time_us) override {
        (void)delta_time_us;

        int page = get_scroll_page();
        if (page < 0) {
            draw_settings(page);
            return;
        }

        leds_clear();

        /* SYSTEM_NOTES.md section 4, and read_modifiers() in plinky-ambiotica:
           emit the modifier ONCE, at the top, BEFORE anything that tests it,
           NOT_ISOLATED because being held while another pad is tapped IS the
           gesture. Read its edge immediately - is_last_widget_*() refers to the
           most recently emitted widget, so nothing may come between. */
        modifier_held = shift_button(LIFE_MODIFIER_X, LIFE_MODIFIER_Y, LIFE_COL_MODIFIER,
                                     NOT_ISOLATED, "hold or tap for actions");
        bool modifier_pressed = is_last_widget_pressed();

        /* Tap toggles, hold peeks. Anything that is not the world goes back to
           the world, so the corner always means "get me out of here". */
        if (modifier_pressed)
            ui_mode = (ui_mode == LIFE_UI_WORLD) ? LIFE_UI_ACTION : LIFE_UI_WORLD;

        int mode = ui_mode;
        if (mode == LIFE_UI_WORLD && modifier_held) mode = LIFE_UI_ACTION;
        if (mode != LIFE_UI_WORLD)
            set_led(LIFE_MODIFIER_X, LIFE_MODIFIER_Y, LED_RGB(31, 0, 24));

        /* The preset picker owns row 15 while it is up - see draw_preset_loader. */
        if (mode == LIFE_UI_LOAD) {
            draw_preset_loader();
            set_help_text("V%d #fc2#*load preset#. - pick a slot, or cancel", edit_voice + 1);
            return;
        }

        /* Transport, once, before anything else and in every other mode. Same
           pad, same colour, same action - never modal, never hidden. */
        for (int i = 0; i < 2; ++i) {
            int tx = i ? LIFE_PLAY_X : LIFE_STOP_X;
            if (button(tx, LIFE_TRANSPORT_Y, transport_colour(tx), NOT_ISOLATED,
                       tx == LIFE_PLAY_X ? "play" : "stop"))
                do_transport(tx);
        }

        /* The hosted synth editor owns the grid: it emits sliders and an XY pad
           rather than one button per pad, so the per-pad loop must not run over
           the top of it. */
        if (mode == LIFE_UI_PRESET) {
            draw_preset_editor();
            set_help_text("V%d #fc2#*sound#. - x to leave", edit_voice + 1);
            return;
        }

        /* ONE widget per pad, in the SAME order, EVERY frame, in every mode.
           The mode changes only each pad's colour and what a press does, which
           is why the voice editor lives inside this loop rather than drawing
           itself alongside it. */
        for (int y = 0; y < LIFE_H; ++y) {
            for (int x = 0; x < LIFE_W; ++x) {
                if (x == LIFE_MODIFIER_X && y == LIFE_MODIFIER_Y) continue;
                if (is_transport_pad(x, y)) continue;   /* emitted once, above */

                uint32_t col;
                const char *help;
                if (mode == LIFE_UI_VOICE) {
                    col = voice_colour(x, y);
                    help = voice_help(x, y);
                } else if (mode == LIFE_UI_ACTION) {
                    col = action_colour(x, y);
                    help = action_help(x, y);
                } else {
                    col = cell_colour(x, y, false);
                    help = nullptr;
                }

                if (!button(x, y, col, NOT_ISOLATED, help)) continue;

                if (mode == LIFE_UI_VOICE) {
                    do_voice_edit(x, y);
                } else if (mode == LIFE_UI_ACTION) {
                    if (is_action_pad(x, y)) {
                        do_action(x, y);
                        /* one-shot: an action drops back to the world unless it
                           opened another mode */
                        if (ui_mode == LIFE_UI_ACTION) ui_mode = LIFE_UI_WORLD;
                    }
                } else {
                    /* on_ui and on_sequence share the world, and on_sequence can
                       interrupt this at any instruction. */
                    on_sequence_lock_guard_t guard;
                    life_toggle(&world, x, y);
                }
            }
        }

        if (mode == LIFE_UI_VOICE)
            set_voice_help_text();
        else if (mode == LIFE_UI_ACTION)
            set_help_text("#fc2#*Actions#. - edit, mute, solo, clear, seed, freeze, step");
        else
            /* The world view is where you spend the time, so its one line of
               text says what the four voices are set to - otherwise the only
               way to know is to open each editor in turn. */
            set_help_text("#fc2#*%d#. alive, %s %s, gen %s%s  #fc2#*|#.  %s %s  %s %s  %s %s  %s %s",
                          last_stats.alive, life_note_names[pref_root],
                          life_scale_long_names[pref_scale], life_rate_names[gen_rate],
                          is_transport_playing() ? "" : " (stopped)",
                          life_rate_names[v_rate[0]], life_sel_names[v_rule[0]],
                          life_rate_names[v_rate[1]], life_sel_names[v_rule[1]],
                          life_rate_names[v_rate[2]], life_sel_names[v_rule[2]],
                          life_rate_names[v_rate[3]], life_sel_names[v_rule[3]]);
    }

    /* ================================================================== */
    /* Persistence                                                        */
    /* ================================================================== */

    int get_version(void) override { return 1; }

    bool on_serialise(serialiser_t &s, int version) override {
        (void)version;
        pack_world();

        /* SYSTEM_NOTES.md section 6b: named fields are only written back when
           PRESENT, so a field a save omits silently keeps the PREVIOUS scene's
           value. Reset to a known default before OBJECT_BEGIN so an absent
           field loads as a default rather than as whatever was last on screen.

           ONLY when loading. The same serialise function runs for both
           directions, and resetting on the way out would write defaults to disk
           and throw away the state the user asked to save. */
        if (is_serialise_in_progress == 1) setup_default_panel_state_fields_only();

        OBJECT_BEGIN(s);
        FIELD("world", world_rows);
        FIELD("ven", v_enabled);
        FIELD("vmute", v_muted);
        FIELD("vrate", v_rate);
        FIELD("vordr", v_order);
        FIELD("vrule", v_rule);
        FIELD("vptch", v_pitch);
        FIELD("vlen", v_length);
        FIELD("vchan", v_channel);
        FIELD("gen", gen_rate);
        FIELD("floor", respawn_floor);
        FIELD("seed", respawn_amount);
        FIELD("stab", respawn_stable);
        FIELD("freeze", freeze_life);
        FIELD("init", initialised);
        OBJECT_END(s);

        clamp_settings();
        unpack_world();
        return true;
    }

    /* The reset half of setup_default_panel_state(), without the world seeding
       or the runtime rebuild, so on_serialise can use it as a known baseline. */
    void setup_default_panel_state_fields_only(void) {
        for (int v = 0; v < LIFE_NUM_VOICES; ++v) {
            v_enabled[v] = 1;
            v_muted[v] = 0;
            v_rate[v] = (uint8_t)(4 + v);
            v_order[v] = TRAV_FORWARD;
            v_rule[v] = (uint8_t)(v == 0 ? SEL_FIRST : v == 1 ? SEL_WALK
                                        : v == 2   ? SEL_RANDOM
                                                   : SEL_LAST);
            v_pitch[v] = (int8_t)(v * -3);
            v_length[v] = 60;
            v_channel[v] = (int8_t)(v + 1);   /* a different MIDI channel each */
        }
        gen_rate = 8;
        respawn_floor = 12;
        respawn_amount = 10;
        respawn_stable = 4;
        freeze_life = 0;
        initialised = 1;
    }

    bool on_serialise_settings(serialiser_t &s, int version) override {
        (void)version;
        if (is_serialise_in_progress == 1) {   /* load only - see on_serialise */
            pref_root = 0;
            pref_scale = 9;
            pref_octave = 3;
            pref_sink = LIFE_SINK_BOTH;
            pref_port = 3;
            pref_send_cc = 1;
        }

        OBJECT_BEGIN(s);
        FIELD("root", pref_root);
        FIELD("scale", pref_scale);
        FIELD("oct", pref_octave);
        FIELD("sink", pref_sink);
        FIELD("port", pref_port);
        FIELD("cc", pref_send_cc);
        OBJECT_END(s);

        clamp_settings();
        return true;
    }
};
