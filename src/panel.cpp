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
#define LIFE_MODIFIER_X 15
#define LIFE_MODIFIER_Y 15

/* Default step length, used for the very first note of a voice before two
   divider edges have been seen and the real interval is known. */
#define LIFE_DEFAULT_STEP_US 250000

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
    uint8_t v_preset[LIFE_NUM_VOICES];    /* which of the 12 synth presets this voice plays */
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
    bool action_latch;        /* tap the corner to keep the action layer open */
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
        action_latch = false;
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
            if (v_preset[v] > 11) v_preset[v] = (uint8_t)v;
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

    int preset_for(int v) const { return v_preset[v] <= 11 ? v_preset[v] : v; }

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
        int new_voice = allocator.voice_allocate(sid, (uint8_t)(1 + v));
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

    void fire_voice(int v) {
        int col = traversal_position(&trav[v], LIFE_W);
        uint16_t col_mask = life_column_mask(&world, col);
        int prev = sel[v].prev_row;
        uint16_t chosen = selection_pick(&sel[v], col_mask, (selection_rule_t)v_rule[v]);

        last_played_mask[v] = chosen;
        if (!chosen) return;                       /* an empty column is a rest */

        voice_dev[v] = (uint8_t)selection_deviation(&sel[v], prev);

        int ticks = voice_length_ticks((int)step_us[v], (int)v_length[v]);

        for (int y = 0; y < LIFE_H; ++y) {
            if (!(chosen & (1u << y))) continue;
            int degree = (15 - y) + (int)v_pitch[v];
            int note = life_degree_to_note(degree, root_note(), scale_mask());

            /* The automaton drives dynamics: a cell in a crowded neighbourhood
               hits harder than a lone one. */
            int n = life_neighbours(&world, col, y);
            int velocity = 68 + n * 7;
            if (velocity > 127) velocity = 127;

            if (voice_arm(&notes[v], note, velocity, ticks) >= 0)
                synth_note_on(v, note, velocity);
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

    bool is_action_pad(int x, int y) const {
        if (y == 14) return x < 8;                 /* 0-3 mute, 4-7 solo */
        if (y == 15) return x < 5;                 /* clear seed freeze step play */
        return false;
    }

    uint32_t action_colour(int x, int y) const {
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
            case 4: return is_transport_playing() ? LIFE_COL_ACTION : LIFE_COL_OFF;
            default: break;
            }
        }
        return cell_colour(x, y, true);            /* the dimmed world underneath */
    }

    static const char *action_help(int x, int y) {
        if (y == 14) return x < 4 ? "mute this voice" : "solo this voice";
        if (y == 15) switch (x) {
        case 0: return "clear the world";
        case 1: return "respawn cells now";
        case 2: return "freeze evolution";
        case 3: return "step one generation";
        case 4: return "start or stop";
        default: break;
        }
        return nullptr;
    }

    void do_action(int x, int y) {
        printf("life: action pad (%d,%d)\n", x, y);
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
        case 4:
            if (is_transport_playing()) {
                stop_transport();
                release_all_voices();
            } else {
                start_transport();
            }
            printf("life: transport now %s\n", is_transport_playing() ? "playing" : "stopped");
            break;
        default: break;
        }
    }

    /* --- settings pages -------------------------------------------------

       Rendered with the system helpers, which return the left-button edit
       delta. The right pair page; the left pair adjust. Both are system
       territory on every faceplate, so the panel never touches them. */

    int settings_page_count(void) { return 17; }
    int get_num_panel_settings_pages(void) override { return settings_page_count(); }

    static int clamp_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    void draw_settings(int page) {
        int idx = -1 - page;                       /* page -1 is our page 0 */
        char buf[8];
        int v = edit_voice;

        switch (idx) {
        case 0: {
            int d = draw_system_style_enum_settings_page("KEY ", pref_root, life_note_names, 12);
            if (d) { pref_root = (uint8_t)((pref_root + d + 120) % 12); apply_scale_to_system(); }
            break;
        }
        case 1: {
            int d = draw_system_style_enum_settings_page("SCAL", pref_scale, life_scale_names,
                                                        LIFE_NUM_SCALES);
            if (d) {
                pref_scale = (uint8_t)clamp_int(pref_scale + d, 0, LIFE_NUM_SCALES - 1);
                apply_scale_to_system();
            }
            break;
        }
        case 2: {
            snprintf(buf, sizeof(buf), "%d", pref_octave);
            int d = draw_system_style_settings_page("OCT ", buf, pref_octave * 100 / 7);
            if (d) pref_octave = (uint8_t)clamp_int(pref_octave + d, 1, 7);
            break;
        }
        case 3: {
            int d = draw_system_style_enum_settings_page("GEN ", gen_rate, life_rate_names,
                                                        LIFE_NUM_RATES);
            if (d) gen_rate = (uint8_t)clamp_int(gen_rate + d, 0, LIFE_NUM_RATES - 1);
            break;
        }
        case 4: {
            snprintf(buf, sizeof(buf), "%d", respawn_floor);
            int d = draw_system_style_settings_page("FLOR", buf, respawn_floor * 100 / 64);
            if (d) respawn_floor = (uint8_t)clamp_int(respawn_floor + d, 0, 64);
            break;
        }
        case 5: {
            snprintf(buf, sizeof(buf), "%d", respawn_amount);
            int d = draw_system_style_settings_page("SEED", buf, respawn_amount * 100 / 64);
            if (d) respawn_amount = (uint8_t)clamp_int(respawn_amount + d, 1, 64);
            break;
        }
        case 6: {
            snprintf(buf, sizeof(buf), "%d", respawn_stable);
            int d = draw_system_style_settings_page("STAB", buf, respawn_stable * 100 / 32);
            if (d) respawn_stable = (uint8_t)clamp_int(respawn_stable + d, 0, 32);
            break;
        }
        case 7: {
            int d = draw_system_style_enum_settings_page("OUT ", pref_sink, life_sink_names, 3);
            if (d) {
                release_all_voices();              /* never strand a note on the old sink */
                pref_sink = (uint8_t)clamp_int(pref_sink + d, 0, 2);
            }
            break;
        }
        case 8: {
            int d = draw_system_style_enum_settings_page("PORT", pref_port, life_port_names, 5);
            if (d) {
                release_all_voices();
                pref_port = (uint8_t)clamp_int(pref_port + d, 0, 4);
            }
            break;
        }
        case 9: {
            bool on = pref_send_cc != 0;
            int d = draw_system_style_bool_settings_page("SIM ", on);
            if (d) pref_send_cc = on ? 0 : 1;
            break;
        }
        case 10: {
            snprintf(buf, sizeof(buf), "V%d", v + 1);
            int d = draw_system_style_settings_page("EDIT", buf, (v + 1) * 25,
                                                   0, 0, life_voice_bright[v]);
            if (d) edit_voice = (uint8_t)clamp_int(v + d, 0, LIFE_NUM_VOICES - 1);
            break;
        }
        case 11: {
            int d = draw_system_style_enum_settings_page("RATE", v_rate[v], life_rate_names,
                                                        LIFE_NUM_RATES, life_voice_bright[v]);
            if (d) {
                release_voice(v);                  /* the step length just changed */
                v_rate[v] = (uint8_t)clamp_int(v_rate[v] + d, 0, LIFE_NUM_RATES - 1);
                last_edge_us[v] = 0;
                step_us[v] = LIFE_DEFAULT_STEP_US;
            }
            break;
        }
        case 12: {
            int d = draw_system_style_enum_settings_page("ORDR", v_order[v], life_trav_names,
                                                        TRAV_COUNT, life_voice_bright[v]);
            if (d) v_order[v] = (uint8_t)clamp_int(v_order[v] + d, 0, TRAV_COUNT - 1);
            break;
        }
        case 13: {
            int d = draw_system_style_enum_settings_page("RULE", v_rule[v], life_sel_names,
                                                        SEL_COUNT, life_voice_bright[v]);
            if (d) v_rule[v] = (uint8_t)clamp_int(v_rule[v] + d, 0, SEL_COUNT - 1);
            break;
        }
        case 14: {
            /* Pitch offset and note length share a page to keep the page count
               walkable with two buttons: length is the bar, pitch the number. */
            snprintf(buf, sizeof(buf), "%+d", v_pitch[v]);
            int d = draw_system_style_settings_page("PTCH", buf, v_length[v],
                                                   0, 0, life_voice_bright[v]);
            if (d) v_pitch[v] = (int8_t)clamp_int(v_pitch[v] + d, -30, 30);
            break;
        }
        case 15: {
            snprintf(buf, sizeof(buf), "%d", v_preset[v] + 1);
            int d = draw_system_style_settings_page("SYNT", buf, (v_preset[v] + 1) * 100 / 12,
                                                   0, 0, life_voice_bright[v]);
            if (d) {
                release_voice(v);
                v_preset[v] = (uint8_t)clamp_int(v_preset[v] + d, 0, 11);
            }
            break;
        }
        case 16: {
            if (v_channel[v] < 1) snprintf(buf, sizeof(buf), "AUTO");
            else snprintf(buf, sizeof(buf), "%d", v_channel[v]);
            int d = draw_system_style_settings_page("CHAN", buf,
                                                   v_channel[v] < 1 ? 0 : v_channel[v] * 100 / 16,
                                                   0, 0, life_voice_bright[v]);
            if (d) v_channel[v] = (int8_t)clamp_int(v_channel[v] + d, -1, 16);
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

        /* SYSTEM_NOTES.md section 4, and the read_modifiers() discipline in
           plinky-ambiotica: emit the modifier ONCE, at the top, BEFORE anything
           that tests it, NOT_ISOLATED because being held while another pad is
           tapped IS the gesture. Read its edges immediately, with nothing in
           between - is_last_widget_*() refers to the most recent widget.

           Pass the dim colour to the widget and set_led the bright one over it
           when active: set_led lands after the widget and wins, so the pad
           highlights in the same frame without emitting a second widget. */
        modifier_held = shift_button(LIFE_MODIFIER_X, LIFE_MODIFIER_Y, LIFE_COL_MODIFIER,
                                     NOT_ISOLATED, "hold or tap for mutes, clear, transport");
        bool modifier_pressed = is_last_widget_pressed();

        /* The layer is available by HOLD or by LATCH. Holding is the Plinky
           grammar, but it is also the fragile half - so a tap latches the layer
           open and any action closes it again. Either gesture works, and
           transport is reachable without relying on hold detection at all. */
        if (modifier_pressed) action_latch = !action_latch;
        bool action_mode = modifier_held || action_latch;

        if (action_mode) set_led(LIFE_MODIFIER_X, LIFE_MODIFIER_Y, LED_RGB(31, 0, 24));

        /* ONE widget per pad, in the SAME order, EVERY frame. The mode changes
           only the colour and what a press does.

           This is the bug that made the modifier appear to cancel itself: the
           previous version emitted 255 invisible_buttons in world mode and 13
           buttons in action mode, so the widget set changed shape the instant
           the corner went down, and the held state went with it. */
        for (int y = 0; y < LIFE_H; ++y) {
            for (int x = 0; x < LIFE_W; ++x) {
                if (x == LIFE_MODIFIER_X && y == LIFE_MODIFIER_Y) continue;

                uint32_t col = action_mode ? action_colour(x, y) : cell_colour(x, y, false);
                const char *help = action_mode ? action_help(x, y) : nullptr;

                if (button(x, y, col, NOT_ISOLATED, help)) {
                    if (action_mode) {
                        if (is_action_pad(x, y)) {
                            do_action(x, y);
                            action_latch = false;   /* a latched layer closes after one action */
                        }
                    } else {
                        /* on_ui and on_sequence share the world, and on_sequence
                           can interrupt this at any instruction. */
                        on_sequence_lock_guard_t guard;
                        life_toggle(&world, x, y);
                    }
                }
            }
        }

        if (action_mode)
            set_help_text("Life #fc2#*actions#. - %s", is_transport_playing() ? "playing" : "stopped");
        else
            set_help_text("Life - #fc2#*%d#. alive, %s %s%s", last_stats.alive,
                          life_note_names[pref_root], life_scale_long_names[pref_scale],
                          is_transport_playing() ? "" : " (stopped)");
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
        FIELD("vpre", v_preset);
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
            v_preset[v] = (uint8_t)v;      /* a different synth patch per voice */
            v_channel[v] = (int8_t)(v + 1); /* and a different MIDI channel */
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
