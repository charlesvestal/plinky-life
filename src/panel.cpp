/*
@Name: Life
@Author: Charles Vestal
@Firmware: latest
@Level: Advanced
@Tags: sequencer, generative, midi, cellular automata, game of life
@Preferred Panels: blocks, chords, drums
@Category: Sequencer
@Documentation: https://github.com/charlesvestal/plinky-life/blob/main/docs/manual.md
@Discussion: https://github.com/charlesvestal/plinky-life
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

/* The cell a voice just took. Deliberately NOT the voice's own colour: that put
   the note on the same hue as the column tint it sits in, which is why it read
   as playhead decoration rather than as a note.

   Magenta is the one hue in the world view with no green in it. Live cells are
   white, the four voice tints are red, green, blue and yellow, so nothing else
   can be mistaken for it. Which voice played it is already answered by the
   column it is standing in. */
#define LIFE_COL_TRIGGER     LED_RGB(29, 0, 29)
#define LIFE_COL_TRIGGER_DIM LED_RGB(8, 0, 8)

/* LED_RGB packs 5-bit channels as g<<24 | r<<16 | b<<8. `q` is 0..256. */
static inline uint32_t life_scale_col(uint32_t c, int q) {
    if (q <= 0) return 0;
    if (q > 256) q = 256;
    int g = (int)((c >> 24) & 31), r = (int)((c >> 16) & 31), b = (int)((c >> 8) & 31);
    return LED_RGB((r * q) >> 8, (g * q) >> 8, (b * q) >> 8);
}

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

static const char *const life_swing_names[8] = {
    "16TH", "ST 1", "ST 2", "ST 3", "ST 4", "ST 5", "ST 6", "ST 7",
};

/* One line per rule for the second screen: "MAZE" says nothing on its own. */
static const char *const life_rule_help[] = {
    "Conway - gliders drifting through a sparse world. The classic",
    "HighLife - like Conway but shapes replicate, so it keeps regenerating",
    "Maze - dense slow-churning corridors. Good for drones",
    "Coral - grows slowly outward into thick stable shapes",
    "34 Life - restless, never settles for long. Busy and unpredictable",
    "Seeds - everything dies every step and explodes outward. Chaos",
};

/* AUTO asks the firmware; the rest force it.

   Detection is the right default and works on units whose straps read, but it
   cannot be relied on alone: a Chords panel has been seen reporting code 0
   (FRONT_PANEL_CODE_NONE) while get_frontpanel_orientation() said 1, i.e. the
   firmware knew a panel was mounted but not which one. Without an override that
   unit can never get its printed layout. */
static const char *const life_plate_names[4] = { "AUTO", "BLNK", "CHRD", "DRUM" };
enum { LIFE_PLATE_AUTO = 0, LIFE_PLATE_BLANK, LIFE_PLATE_CHORDS, LIFE_PLATE_DRUMS };

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

/* The world is ALWAYS the full 16 rows, on every faceplate.

   Chords and Drums print pad circles only on rows 2..13, so on those plates the
   top and bottom rows of the world sit under printed control rows. That
   misalignment is deliberate and settled: rows are scale degrees, and cropping
   to the printed circles would make it a 12-degree instrument instead of a
   16-degree one. The grid is the grid; we align to the silkscreen on the sound
   page, where the labels actually name what the pads do.

   Row 15, following the Chords silkscreen - which is also the convention in
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

/* Plinky has 12 physical synth voices. The sequencer and the play surface both
   want some, and they allocate independently, so they are given separate ranges
   rather than left to steal from each other: the surface takes 0..3, the
   sequencer 4..11.

   llm.txt notes that 8 "is the current practical default for user panels; using
   all 12 can exceed the DSP budget with heavier presets or effects" - so the
   sequencer keeps its eight and the surface's four are the ones on top. Worth
   watching on hardware with a heavy preset. */
#define LIFE_PLAY_VOICES     4
#define LIFE_SEQ_VOICE_FIRST 4
#define LIFE_SEQ_VOICE_END   12
#define LIFE_SYNTH_VOICES    (LIFE_SEQ_VOICE_END - LIFE_SEQ_VOICE_FIRST)

/* Every playhead claims at the same priority. An earlier version passed
   `1 + v`, which quietly made voice 4 outrank voice 1 and let the bass steal
   synth voices from the melody. That was the loop index leaking into a musical
   decision, not a musical decision. */
#define LIFE_VOICE_PRIO 2

/* The three things the grid can be showing. The grid is the entire screen, so
   these are modes rather than panes. */
enum { LIFE_UI_WORLD = 0, LIFE_UI_ACTION, LIFE_UI_VOICE, LIFE_UI_PRESET, LIFE_UI_LOAD,
       LIFE_UI_PLAY };

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
    {  5, 11, "PICK"  },   /* which live cell this voice takes - NOT the world's rule */
    {  7,  4, "ORDER" },
    {  9, 15, "PITCH" },   /* bipolar, centre pad is 0 */
    { 11, 10, "LENGTH" },
    { 13, 11, "ACCENT" },  /* how much crowding drives velocity */
};
#define LIFE_NUM_PARAM_ROWS ((int)(sizeof(life_param_rows) / sizeof(life_param_rows[0])))

/* The voice editor's second page. Four behaviours, each 0..100 in tens, and 0
   is off - so one control both enables a behaviour and sets how hard it bites.
   The voice selector repeats on both pages so you never lose track of who you
   are editing. */
static const life_param_row_t life_chance_rows[] = {
    {  1,  4, "VOICE" },
    {  3, 11, "CHANCE" },   /* how much lone cells get skipped */
    {  5, 11, "RATCHET" },  /* how much crowding adds repeats */
    {  7, 11, "TIE" },      /* how often survivors hold instead of striking */
    {  9, 11, "EVERY" },    /* play only every Nth crossing */
    { 11, 16, "CHAN" },     /* MIDI channel - routing, not timing */
};
#define LIFE_NUM_CHANCE_ROWS ((int)(sizeof(life_chance_rows) / sizeof(life_chance_rows[0])))

/* Row 15, x2: flip between the two pages of the voice editor. */

#define LIFE_PITCH_CENTRE 7

/* Voice N always plays preset N. There is no preset-selector row: four
   playheads and four presets is a direct mapping, and an indirection between
   them only earns you the question "which preset is voice 3 on again?".
   Presets 5-12 are still reachable - load one into slots 1-4 from the preset
   editor's own save/load picker. */
/* Row 15, x0..x3, is the shared nav strip: load, sound, settings, play.
   See draw_voice_nav. */

/* Chords prints SAVE and LOAD at (12,14) and (13,14), so the file pickers put
   their commit buttons there. Row 14 is a printed control row on Chords and
   Drums and free on a blank plate, so the same spot works everywhere. */
#define LIFE_SAVE_X      12
#define LIFE_LOAD_BTN_X  13
#define LIFE_FILE_BTN_Y  14
#define LIFE_SOUND_Y 15

struct life_panel : panel_t {
    /* --- world --- */
    life_world_t world;
    life_world_t scratch;
    life_world_t prev_world;   /* last generation, so ties know what survived */
    life_respawn_t respawn;
    life_stats_t last_stats;

    /* --- per voice --- */
    clock_divider_t voice_div[LIFE_NUM_VOICES];
    traversal_state_t trav[LIFE_NUM_VOICES];
    selection_state_t sel[LIFE_NUM_VOICES];
    chance_state_t chance[LIFE_NUM_VOICES];

    /* A ratchet is the same note struck again inside one step. */
    uint8_t rat_left[LIFE_NUM_VOICES];
    uint8_t rat_note[LIFE_NUM_VOICES];
    uint8_t rat_vel[LIFE_NUM_VOICES];
    uint32_t rat_gap_us[LIFE_NUM_VOICES];
    uint32_t rat_next_us[LIFE_NUM_VOICES];
    voice_notes_t notes[LIFE_NUM_VOICES];
    voice_allocator_t allocator;
    preset_pages_t preset_pages;   /* the stock synth editor, hosted per voice */
    file_picker_t scene_picker;    /* scenes, placed by us - see draw_scene_page */
    int scene_commit_wait;         /* frames left to wait for a staged load to finish */
    play_surface_t play_surface;   /* play the selected voice over the running world */
    uint8_t string_roots[16];

    /* do_play_surface is LEVEL-TRIGGERED: it calls back only for fingers that
       are currently down and simply stops mentioning one that lifts. There is
       no release callback, so anything sounding that it did not name this frame
       is ours to stop. */
    uint16_t play_down;            /* surface voices currently sounding */
    uint16_t play_seen;            /* named by the callback this frame */
    uint8_t play_note[16];         /* so a slide to a new note retriggers */
    bool play_active;              /* were we on the surface last frame */
    int picker_channel;            /* preset channel the picker is open for, -1 closed */
    bool scene_picker_open;

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
    uint8_t rule_set;         /* which cellular automaton rule the world runs */
    uint8_t swing;            /* 0..100, applied to every voice */
    uint8_t swing_pattern;    /* 0 = plain swing, 1..7 = the stolper patterns */
    uint8_t v_accent[LIFE_NUM_VOICES];  /* 0..100: how much crowding drives velocity */
    uint8_t v_prob[LIFE_NUM_VOICES];    /* 0..100: how much lone cells get skipped */
    uint8_t v_ratchet[LIFE_NUM_VOICES]; /* 0..100: how much crowding adds repeats */
    uint8_t v_tie[LIFE_NUM_VOICES];     /* 0..100: how often survivors hold */
    uint8_t v_cond[LIFE_NUM_VOICES];    /* 0..100: play every Nth crossing */

    /* --- durable preferences (on_serialise_settings) --- */
    uint8_t pref_root;        /* 0..11 */
    uint8_t pref_scale;       /* index into life_scale_masks */
    uint8_t pref_octave;      /* base octave, 1..7 */
    uint8_t pref_sink;
    uint8_t pref_port;
    uint8_t pref_send_cc;
    uint8_t pref_cc_in;       /* base controller number for the CC block, 0 = off */
    uint8_t pref_cc_out;      /* mirror parameter changes back out on the same block */
    uint8_t pref_cv_out;      /* drive the two CV outputs from the world */
    uint8_t pref_note_in;     /* played notes draw cells into the world */
    uint8_t pref_plate;       /* AUTO, or force the faceplate - see life_plate_names */

    /* --- transient UI state, never serialised --- */
    uint8_t edit_voice;       /* which voice the per-voice settings pages edit */
    uint8_t voice_page;       /* 0 = rate/rule/pitch, 1 = the behaviour controls */
    char readout[12];         /* the value text under your finger, built each frame */
    bool modifier_held;
    uint8_t ui_mode;          /* LIFE_UI_WORLD / _ACTION / _VOICE */
    uint8_t solo_mask;
    uint8_t last_cc[LIFE_NUM_CCS];
    bool cc_primed;
    bool step_once_request;
    bool settings_dirty;      /* a preference changed and has not reached the SD card */

    /* Incoming CC. on_midi() is an interrupt, so it does the cheapest possible
       thing - record the value, set a bit - and on_ui() applies it on core0.
       Setting the key from an interrupt would mean calling into the system's
       harmonic state from an IRQ, which is not a thing worth risking for a knob
       turn. */
    uint8_t cc_in_value[CC_PARAM_COUNT];
    uint8_t cc_in_last[CC_PARAM_COUNT];
    uint64_t cc_in_pending;
    uint32_t musical_state_seen;   /* so we notice key or scale changed elsewhere */
    uint32_t plate_log_us;         /* when the front panel was last reported */

    /* Read at CONSTRUCTION, in a member initialiser, which is what grid.cpp
       does and the only place it works.

       On a Chords unit get_frontpanel_code() returns the plate here and 0 from
       on_ui later, while get_frontpanel_orientation() reports 1 throughout - so
       the firmware knows a panel is mounted, but the code is only readable
       early. Reading it per frame looked tidier and silently broke detection. */
    int plate_at_construct = get_frontpanel_code();

    /* Note input. on_midi records; on_ui paints, because touching the world is
       exactly what the sequence lock exists to protect. */
    uint8_t note_in_row[16];
    uint8_t note_in_count;
    uint8_t note_write_x;

    /* What the outside world was last told each parameter is. Sending only on
       change is half of what stops a feedback loop; the other half is
       cc_from_index()'s round-trip stability. */
    uint8_t cc_out_last[CC_PARAM_COUNT];
    bool cc_out_primed;

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
            chance_reset(&chance[v], 0x3000u + v * 271u);
            voice_notes_init(&notes[v]);
            last_edge_us[v] = 0;
            step_us[v] = LIFE_DEFAULT_STEP_US;
            last_played_mask[v] = 0;
            rat_left[v] = 0;
            voice_dev[v] = 0;
        }

        solo_mask = 0;
        edit_voice = 0;
        voice_page = 0;
        modifier_held = false;
        ui_mode = LIFE_UI_WORLD;
        cc_primed = false;
        step_once_request = false;
        settings_dirty = false;
        for (int i = 0; i < LIFE_NUM_CCS; ++i) last_cc[i] = 255;
        for (int i = 0; i < CC_PARAM_COUNT; ++i) {
            cc_in_value[i] = cc_in_last[i] = 0;
            cc_out_last[i] = 255;
        }
        cc_in_pending = 0;
        cc_out_primed = false;
        musical_state_seen = 0;
        scene_commit_wait = 0;
        play_down = 0;
        play_seen = 0;
        play_active = false;
        picker_channel = -1;
        scene_picker_open = false;
        for (int i = 0; i < 16; ++i) play_note[i] = 0;
        note_in_count = 0;
        note_write_x = 0;
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
        pref_cc_in = CC_IN_DEFAULT_BASE;
        pref_cc_out = 1;
        pref_cv_out = 1;
        pref_note_in = 1;
        pref_plate = LIFE_PLATE_AUTO;
    }

    void on_load_finished(void) override {
        panel_t::on_load_finished();

        plate_log_us = 0;   /* log the front panel promptly after a load */

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
            chance_reset(&chance[v], 0x3000u + v * 271u);
            voice_notes_init(&notes[v]);
            last_edge_us[v] = 0;
            step_us[v] = LIFE_DEFAULT_STEP_US;
            last_played_mask[v] = 0;
            rat_left[v] = 0;
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
            if (v_accent[v] > 100) v_accent[v] = 60;
            if (v_prob[v] > 100) v_prob[v] = 0;
            if (v_ratchet[v] > 100) v_ratchet[v] = 0;
            if (v_tie[v] > 100) v_tie[v] = 0;
            if (v_cond[v] > 100) v_cond[v] = 0;
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
        if (rule_set >= LIFE_NUM_RULESETS) rule_set = 0;
        if (swing > 100) swing = 0;
        if (swing_pattern > 7) swing_pattern = 0;
        if (pref_root > 11) pref_root = 0;
        if (pref_scale >= LIFE_NUM_SCALES) pref_scale = 9;
        if (pref_octave < 1) pref_octave = 1;
        if (pref_octave > 7) pref_octave = 7;
        if (pref_sink > LIFE_SINK_BOTH) pref_sink = LIFE_SINK_BOTH;
        if (pref_port > 4) pref_port = 3;
        if (pref_cc_in > 127 - (CC_PARAM_COUNT - 1)) pref_cc_in = 0;
        if (pref_plate > LIFE_PLATE_DRUMS) pref_plate = LIFE_PLATE_AUTO;
        if (edit_voice >= LIFE_NUM_VOICES) edit_voice = 0;
    }

    /* current_key / current_scale are system globals shared across panels, so
       choosing a scale here drives the whole instrument rather than keeping a
       private harmonic state. */
    void apply_scale_to_system(void) {
        set_current_key_and_scale((uint8_t)(pref_root & 11), life_scale_masks[pref_scale]);
    }

    /* Read the INSTRUMENT's key and scale, not a private copy. Life sets those
       globals from its own settings page, but anything else - another panel, an
       incoming NRPN - can set them too, and this way we simply follow instead of
       quietly playing in a key the rest of the box has left. */
    uint16_t scale_mask(void) const {
        return current_scale ? current_scale : life_scale_masks[pref_scale];
    }
    int root_note(void) const { return (int)pref_octave * 12 + (int)(current_key % 12); }

    /* The globals changed under us: move the settings-page pickers to match, so
       what the page shows is what the instrument is actually doing. */
    void follow_musical_state(void) {
        uint32_t gen = get_current_musical_state_generation();
        if (gen == musical_state_seen) return;
        musical_state_seen = gen;

        uint8_t key = (uint8_t)(current_key % 12);
        if (key != pref_root) { pref_root = key; settings_dirty = true; }

        for (int i = 0; i < LIFE_NUM_SCALES; ++i)
            if (life_scale_masks[i] == current_scale) {
                if (pref_scale != i) { pref_scale = (uint8_t)i; settings_dirty = true; }
                return;
            }
        /* A scale we do not have a name for. We still PLAY it - scale_mask()
           reads the global - the picker just cannot show which one it is. */
    }
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

    /* Voice N plays preset N. Deliberately not configurable. */
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
        int old_voice = allocator.find_voice(sid, LIFE_SEQ_VOICE_FIRST, LIFE_SEQ_VOICE_END);
        int new_voice = allocator.voice_allocate(sid, LIFE_VOICE_PRIO,
                                                 LIFE_SEQ_VOICE_FIRST, LIFE_SEQ_VOICE_END);
        if (new_voice < 0) return;
        if (new_voice != old_voice && old_voice >= 0) synth_note_up(old_voice);
        play_synth(new_voice, preset_for(v), velocity, note << 8, true);
    }

    void synth_note_off(int v, int note) {
        uint32_t sid = source_id_for(v, note);
        int voice = allocator.find_voice(sid, LIFE_SEQ_VOICE_FIRST, LIFE_SEQ_VOICE_END);
        if (voice >= 0) {
            synth_note_up(voice);
            allocator.voice_deallocate(sid, LIFE_SEQ_VOICE_FIRST, LIFE_SEQ_VOICE_END);
        }
    }

    /* The single exit path for a voice. Mute, rate change, transport stop,
       solo change and panel unload all come through here - stuck MIDI notes are
       the classic failure mode for a sequencer with an external sink, and one
       release function is what makes them impossible. */
    void release_voice(int v) {
        uint8_t released[LIFE_MAX_HELD];
        rat_left[v] = 0;
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
        for (int i = 0; i < LIFE_CELLS; ++i) prev_world.cell[i] = world.cell[i];
        life_step_rule(&world, &scratch, &st, rule_set);
        for (int i = 0; i < LIFE_CELLS; ++i) world.cell[i] = scratch.cell[i];
        last_stats = st;

        if (life_respawn_tick(&respawn, &st, (int)respawn_floor, (int)respawn_stable))
            life_respawn_apply(&world, &respawn, (int)respawn_amount);

        if (pref_send_cc) send_simulation_ccs();
        send_simulation_cv();
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
        /* Every Nth crossing. Checked before anything else - a voice sitting
           out this pass should not even consult the world. */
        if (!chance_pass_ok(v_cond[v], &chance[v])) { last_played_mask[v] = 0; return; }

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
        rat_left[v] = 0;

        /* Lowest pitch first (row 15 up), so a chord that has to be trimmed
           keeps its roots and loses the top rather than the other way round. */
        for (int y = LIFE_H - 1; y >= 0; --y) {
            if (!(chosen & (1u << y))) continue;
            if (sounded >= budget) break;
            int degree = (15 - y) + (int)v_pitch[v];
            int note = life_degree_to_note(degree, root_note(), scale_mask());

            /* The world drives dynamics: a cell in a crowded neighbourhood hits
               harder than a lone one. ACCENT is how much of that gets through -
               at 0 every note is the same weight, at 100 the crowding owns the
               dynamics. */
            int n = life_neighbours(&world, col, y);

            /* Lone cells drop out, crowded ones are certain. */
            if (!chance_should_fire(v_prob[v], n, &chance[v])) continue;

            /* A cell that was already alive last generation can hold the
               previous note instead of striking a new one. */
            int survived = prev_world.cell[y * LIFE_W + col] ? 1 : 0;
            if (voice_num_held(&notes[v]) && chance_should_tie(v_tie[v], survived, &chance[v])) {
                for (int i = 0; i < LIFE_MAX_HELD; ++i)
                    if (notes[v].held[i].active) notes[v].held[i].ticks_left = ticks;
                ++sounded;
                continue;
            }

            int depth = v_accent[v];
            int velocity = 100 - (depth * 32) / 100 + (n * 7 * depth) / 100;
            if (velocity < 1) velocity = 1;
            if (velocity > 127) velocity = 127;

            if (voice_arm(&notes[v], note, velocity, ticks) >= 0) {
                synth_note_on(v, note, velocity);
                ++sounded;

                /* Crowding rolls the note. Only for the single-note rules: a
                   ratcheting 16-note chord is not a musical idea. */
                if (v_rule[v] != SEL_ALL) {
                    int reps = chance_ratchets(v_ratchet[v], n);
                    if (reps > 1) {
                        rat_left[v] = (uint8_t)(reps - 1);
                        rat_gap_us[v] = step_us[v] / (uint32_t)reps;
                        rat_next_us[v] = time_us() + rat_gap_us[v];
                        rat_note[v] = (uint8_t)note;
                        rat_vel[v] = (uint8_t)velocity;
                    }
                }
            }
        }
    }

    /* Swing warps the clock the dividers read, so every voice shuffles together
       and the world keeps its own straight time. Pattern 0 is plain 16th-note
       swing; 1..7 are the stolper patterns, which are the same seven the Chords
       manual documents. */
    int64_t swung_phase(int64_t phase) const {
        if (!swing) return phase;
        float amount = (float)swing / 100.0f;
        if (swing_pattern == 0) return warp_clock_swing(phase, amount);
        return warp_clock_stolper(swing_pattern - 1, phase, amount);
    }

    void on_sequence(int delta_time_us) override {
        int64_t raw_phase = get_clock_phase();
        int64_t phase = swung_phase(raw_phase);
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
        int gen_edges = gen_div.update(raw_phase, gr->num, gr->den, UPDATE_DIV_ON_QUARTER_NOTE);
        if (!freeze_life && gen_edges > 0 && sequencer_should_advance_playhead()) {
            /* A long stall can report many crossed edges at once. Cap the catch
               up so a seek cannot burn hundreds of generations inside an IRQ. */
            if (gen_edges > 4) gen_edges = 4;
            for (int i = 0; i < gen_edges; ++i) step_generation();
        }

        uint32_t now = time_us();

        /* Ratchets land between divider edges, so they are their own pass. */
        for (int v = 0; v < LIFE_NUM_VOICES; ++v) {
            if (!rat_left[v]) continue;
            if (!voice_is_audible(v)) { rat_left[v] = 0; continue; }
            if ((int32_t)(now - rat_next_us[v]) < 0) continue;
            int ticks = voice_length_ticks((int)rat_gap_us[v], (int)v_length[v]);
            if (voice_arm(&notes[v], rat_note[v], rat_vel[v], ticks) >= 0)
                synth_note_on(v, rat_note[v], rat_vel[v]);
            rat_next_us[v] += rat_gap_us[v];
            --rat_left[v];
        }

        for (int v = 0; v < LIFE_NUM_VOICES; ++v) {
            const life_rate_t *r = &life_rates[v_rate[v]];
            int edges = voice_div[v].update(phase, r->num, r->den, UPDATE_DIV_ON_QUARTER_NOTE);
            if (edges <= 0) continue;

            /* Everything above the audibility check keeps running while a voice
               is muted. A mute silences a voice, it does not park it: if the
               playhead stopped here it would resume wherever it froze, shifted
               against the other three for good, and the every-Nth counter would
               stall with it. Unmuting should drop the voice back into the
               pattern where it would have been. */
            if (last_edge_us[v]) {
                uint32_t dt = now - last_edge_us[v];
                if (dt > 1000 && dt < 30000000u) step_us[v] = dt;
            }
            last_edge_us[v] = now;

            if (sequencer_should_advance_playhead()) {
                int before = traversal_position(&trav[v], LIFE_W);
                int after = traversal_advance(&trav[v], (traversal_order_t)v_order[v], LIFE_W);
                /* Wrapping counts as one crossing of the world. */
                if (after <= before) ++chance[v].pass;
            }

            if (!voice_is_audible(v)) {
                if (voice_num_held(&notes[v])) release_voice(v);
                rat_left[v] = 0;
                last_played_mask[v] = 0;   /* nothing sounding, so nothing marked */
                continue;
            }

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

    /* Plinky has two CV outputs. The world's own shape is the most interesting
       modulation this panel produces, so it goes out as voltage too, not only as
       MIDI: A follows how full the world is, B follows how much is changing.
       0..5V, updated once per generation like the CCs. */
    void send_simulation_cv(void) {
        if (!pref_cv_out) return;
        int density = last_stats.alive * 5000 / LIFE_CELLS;
        int motion = (last_stats.births + last_stats.deaths) * 5000 / 64;
        if (motion > 5000) motion = 5000;
        set_cv_out_mv(0, density);
        set_cv_out_mv(1, motion);
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
            /* Lowest-numbered voice owns the column when two are on it. */
            int owner = 0;
            for (int v = 0; v < LIFE_NUM_VOICES; ++v)
                if (on_col & (1u << v)) { owner = v; break; }

            /* The cell a voice took. Checked first: it is the one thing here
               that is about a note rather than about the world. */
            for (int v = 0; v < LIFE_NUM_VOICES; ++v) {
                if (!(on_col & (1u << v))) continue;
                if (last_played_mask[v] & (1u << y))
                    return dim ? LIFE_COL_TRIGGER_DIM : LIFE_COL_TRIGGER;
            }

            /* A playhead is ON TOP of the world, so a live cell inside a column
               takes the column's colour and gets brighter for being alive. It
               used to return plain white here, which broke the column wherever
               there were cells - the thing you notice as white cells sitting on
               a red column. */
            uint32_t col = alive ? life_voice_bright[owner] : life_voice_dim[owner];
            return dim ? life_scale_col(col, 72) : col;
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

    /* Swing is 0..100 in tens, like the behaviour rows. */
    int swing_index(void) const { return (swing + 5) / 10; }

    bool is_action_pad(int x, int y) const {
        if (y == 10) return x < 11;                /* swing, 0..100 in tens */
        if (y == 11) return x < LIFE_NUM_RATES;    /* how fast the world evolves */
        if (y == 12) return x < LIFE_NUM_RULESETS; /* which law the world lives by */
        if (y == 13) return x < 4;                 /* 0-3 edit that voice */
        if (y == 14) return x < 8;                 /* 0-3 mute, 4-7 solo */
        if (y == 15) return x < 4;                 /* clear seed freeze step */
        return false;
    }

    uint32_t action_colour(int x, int y) const {
        /* The world's own controls - its law, its tempo and its feel - live
           together here rather than four and fourteen clicks into the settings.
           None of them belongs to a voice. */
        if (y == 10 && x < 11)
            return x == swing_index() ? LED_RGB(0, 20, 22)
                                      : (x == 0 ? LIFE_COL_OFF : LED_RGB(0, 5, 6));
        if (y == 11 && x < LIFE_NUM_RATES)
            return x == gen_rate ? LED_RGB(22, 14, 0) : LED_RGB(5, 3, 0);
        if (y == 12 && x < LIFE_NUM_RULESETS)
            return x == rule_set ? LED_RGB(26, 6, 24) : LED_RGB(6, 1, 6);
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
        if (y == 10 && x < 11) return "Swing - every voice shuffles; the world stays straight";
        if (y == 11 && x < LIFE_NUM_RATES) return "How often the world evolves one generation";
        if (y == 12 && x < LIFE_NUM_RULESETS) return life_rule_help[x];
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
        if (y == 10 && x < 11) { swing = (uint8_t)(x * 10); settings_dirty = true; return; }
        if (y == 11 && x < LIFE_NUM_RATES) { gen_rate = (uint8_t)x; settings_dirty = true; return; }
        if (y == 12 && x < LIFE_NUM_RULESETS) {
            rule_set = (uint8_t)x;
            settings_dirty = true;
            return;
        }
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

    const life_param_row_t *rows(void) const {
        return voice_page ? life_chance_rows : life_param_rows;
    }
    int num_rows(void) const {
        return voice_page ? LIFE_NUM_CHANCE_ROWS : LIFE_NUM_PARAM_ROWS;
    }

    int param_row_for_y(int y) const {
        for (int i = 0; i < num_rows(); ++i)
            if (rows()[i].y == y) {
                /* Row index 0 is the editor's own VOICE row. On a printed plate
                   the shared selector on row 0 already does that job, and two
                   sets of voice pads a row apart is just confusing. */
                if (i == 0 && plate_is_printed()) return -1;
                return i;
            }
        return -1;
    }

    int get_param(int row) const {
        int v = edit_voice;
        if (row == 0) return edit_voice;

        if (voice_page) switch (row) {
        case 1: return (v_prob[v] + 5) / 10;
        case 2: return (v_ratchet[v] + 5) / 10;
        case 3: return (v_tie[v] + 5) / 10;
        case 4: return (v_cond[v] + 5) / 10;
        case 5: return (v_channel[v] >= 1 && v_channel[v] <= 16) ? v_channel[v] - 1 : v;
        default: return 0;
        }

        switch (row) {
        case 1: return v_rate[v];
        case 2: return v_rule[v];
        case 3: return v_order[v];
        case 4: return v_pitch[v] + LIFE_PITCH_CENTRE;
        case 5: return (v_length[v] - 10) / 10;
        case 6: return (v_accent[v] + 5) / 10;
        default: return 0;
        }
    }

    void set_param(int row, int i) {
        int v = edit_voice;
        if (row == 0) { edit_voice = (uint8_t)i; return; }

        if (voice_page) switch (row) {
        case 1: v_prob[v] = (uint8_t)(i * 10); return;
        case 2: v_ratchet[v] = (uint8_t)(i * 10); return;
        case 3: v_tie[v] = (uint8_t)(i * 10); return;
        case 4: v_cond[v] = (uint8_t)(i * 10); return;
        case 5: release_voice(v); v_channel[v] = (int8_t)(i + 1); return;
        default: return;
        }

        switch (row) {
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
        case 4: v_pitch[v] = (int8_t)(i - LIFE_PITCH_CENTRE); return;
        case 5: v_length[v] = (uint8_t)(10 + i * 10); return;
        case 6: v_accent[v] = (uint8_t)(i * 10); return;
        default: return;
        }
    }

    uint32_t voice_colour(int x, int y) const {
        int row = param_row_for_y(y);
        if (row < 0) return 0;                       /* the blank separator rows */
        if (x >= rows()[row].n) return 0;

        /* the voice selector paints each pad in its own voice's colour */
        if (row == 0) return (x == edit_voice) ? life_voice_bright[x] : life_voice_dim[x];

        int v = edit_voice;
        if (x == get_param(row)) return life_voice_bright[v];
        /* the zero end of a chance row, and the PITCH centre, stay findable */
        if (voice_page && row < 5 && x == 0) return LIFE_COL_OFF;
        if (!voice_page && row == 4 && x == LIFE_PITCH_CENTRE) return LIFE_COL_OFF;
        return life_voice_dim[v];
    }

    /* Per-pad help, not per-row. Touching a RULE pad should say what that rule
       DOES - it is the most opaque part of the panel and the second screen is
       the only place words can appear.

       Everything returned here is a static string. A shared formatting buffer
       would be wrong: this runs for all 256 pads each frame and the widget layer
       keeps the pointer, so a later pad would overwrite the touched one's text. */
    const char *voice_help(int x, int y) const {
        /* Row 15 x0..x3 belongs to the shared nav strip, which supplies its own
           help - see draw_voice_nav. */
        int row = param_row_for_y(y);
        if (row < 0 || x >= rows()[row].n) return nullptr;

        if (voice_page) switch (row) {
        case 0: return "which voice you are editing";
        case 1: return "Chance - how much lone cells get skipped. Left is off";
        case 2: return "Ratchet - how much a crowded cell repeats inside its step";
        case 3: return "Tie - how often a cell that survived holds instead of striking";
        case 4: return "Every - play only every 2nd, 3rd or 4th crossing of the world";
        case 5: return "MIDI channel for this voice";
        default: return rows()[row].name;
        }

        switch (row) {
        case 0: return "which voice you are editing";
        case 1: return life_rate_names[x];
        case 2: return life_sel_help[x];
        case 3: return life_trav_help[x];
        case 4: return "pitch offset in scale degrees; the dim centre pad is 0";
        case 5: return "note length, as a share of this voice's step";
        case 6: return "Accent - how much a crowded cell hits harder than a lone one";
        default: return rows()[row].name;
        }
    }

    void do_voice_edit(int x, int y) {
        int row = param_row_for_y(y);
        if (row < 0 || x >= rows()[row].n) return;
        set_param(row, x);
    }

    /* One line describing the whole voice, for the second-screen help view -
       the only place this panel can show words. */
    /* --- the readout ------------------------------------------------------

       These rows are pads with no numbers on them, so while you hold one the
       value is spelled out in a 4-row font. The catch is that your hand covers
       whatever is under it, so it is drawn in the zone FURTHEST from the row you
       are touching: hold something in the top half and the value appears at the
       bottom, and the other way round. You never read through your own fingers.

       Colour is the voice's own, so the number needs no label. */
    static void trim4(char *dst, const char *src) {
        int n = 0;
        for (int i = 0; i < 4 && src[i]; ++i)
            if (src[i] != ' ') dst[n++] = src[i];
        dst[n] = 0;
    }

    /* A bare "30" says nothing - thirty of what? So a number gets a two-letter
       tag. Values that already name themselves - 8th, WALK, FWD - do not.

       "AC 60" fits across the sixteen columns - confirmed on hardware, not
       guessed. Narrower values keep the space; if a wider one ever needs the
       room it loses the space first, then the tag, rather than being clipped.
       leds_string_width() makes that call at runtime. */
    const char *readout_text(int row) {
        int v = edit_voice;
        char value[8];
        const char *tag = 0;
        value[0] = 0;
        readout[0] = 0;

        if (row == 0) { readout[0] = 'V'; readout[1] = (char)('1' + v); readout[2] = 0;
                        return readout; }

        if (voice_page) switch (row) {
            case 1: tag = "SK"; snprintf(value, sizeof(value), "%d", v_prob[v]); break;
            case 2: tag = "RT"; snprintf(value, sizeof(value), "%d", v_ratchet[v]); break;
            case 3: tag = "TI"; snprintf(value, sizeof(value), "%d", v_tie[v]); break;
            case 4: tag = "EV";
                    snprintf(value, sizeof(value), "%d", chance_pass_divisor(v_cond[v]));
                    break;
            case 5: tag = "CH";
                    snprintf(value, sizeof(value), "%d",
                             (v_channel[v] >= 1 && v_channel[v] <= 16) ? v_channel[v] : v + 1);
                    break;
            default: break;
        } else switch (row) {
            case 1: trim4(value, life_rate_names[v_rate[v]]); break;
            case 2: trim4(value, life_sel_names[v_rule[v]]); break;   /* names itself */
            case 3: trim4(value, life_trav_names[v_order[v]]); break;
            case 4: tag = "PT"; snprintf(value, sizeof(value), "%+d", (int)v_pitch[v]); break;
            case 5: tag = "LN"; snprintf(value, sizeof(value), "%d", (int)v_length[v]); break;
            case 6: tag = "AC"; snprintf(value, sizeof(value), "%d", (int)v_accent[v]); break;
            default: break;
        }

        if (!value[0]) return readout;
        if (tag) {
            snprintf(readout, sizeof(readout), "%s %s", tag, value);
            if (leds_string_width(readout, FONT_4) <= LIFE_W) return readout;
            snprintf(readout, sizeof(readout), "%s%s", tag, value);
            if (leds_string_width(readout, FONT_4) <= LIFE_W) return readout;
        }
        snprintf(readout, sizeof(readout), "%s", value);
        return readout;
    }

    /* Top zone is rows 0..3, bottom is 11..14 - never row 15, which carries the
       transport and must not be covered by anything. */
    void draw_readout(int touched_y) {
        int row = param_row_for_y(touched_y);
        if (row < 0) return;

        const char *txt = readout_text(row);
        if (!txt[0]) return;

        int zone_y = (touched_y <= 7) ? 11 : 0;

        /* Clear the zone first. Text drawn straight over lit pads is unreadable
           - the pads win, because they are solid and the glyph is a few pixels
           of the same brightness. */
        for (int y = zone_y; y < zone_y + 4 && y < LIFE_H; ++y)
            for (int x = 0; x < LIFE_W; ++x) {
                if (y == LIFE_TRANSPORT_Y) continue;   /* never cover transport */
                set_led(x, y, 0);
            }

        int w = leds_string_width(txt, FONT_4);
        int x = (LIFE_W - w) / 2;
        if (x < 0) x = 0;
        leds_draw_string(x, zone_y, FONT_4, life_voice_bright[edit_voice], txt);
    }

    void set_voice_help_text(void) {
        int v = edit_voice;
        if (voice_page) {
            set_help_text("#fc2#*Voice %d behaviour#. - skip %d%%, ratchet %d%%, tie %d%%, "
                          "every %d crossing%s, ch %d", v + 1, v_prob[v], v_ratchet[v],
                          v_tie[v], chance_pass_divisor(v_cond[v]),
                          chance_pass_divisor(v_cond[v]) == 1 ? "" : "s",
                          (v_channel[v] >= 1 && v_channel[v] <= 16) ? v_channel[v] : v + 1);
            return;
        }
        set_help_text("#fc2#*Voice %d#. every %s, %s going %s. pitch %+d, len %d%%, accent %d%%",
                      v + 1, life_rate_names[v_rate[v]], life_sel_help[v_rule[v]],
                      life_trav_names[v_order[v]], (int)v_pitch[v], (int)v_length[v],
                      (int)v_accent[v]);
        (void)0;
    }

    /* --- the play surface -------------------------------------------------

       An instrument laid over the sequencer, not another way to edit it. Notes
       played here sound and nothing else: they do not draw cells, because the
       point is to perform over the world rather than to disturb it.

       Rows are strings and row 0 is the highest, the same way the world reads,
       so a row here is the degree it would be over there. Moving right along a
       row climbs the scale from that row's root, which is the layout a Plinky
       player already knows.

       It is always in key: the scale and root come from the instrument's
       globals, so it follows KEY and SCAL like everything else. */
    static void play_surface_note(void *user, int voice, int note, uint8_t velocity,
                                  finger_t finger) {
        (void)finger;
        life_panel *self = (life_panel *)user;
        if (voice < 0 || voice >= 16) return;

        self->play_seen |= (uint16_t)(1u << voice);
        /* Strike on a new finger, or when a slide lands on a different note.
           Otherwise this is the same note being re-reported with fresh
           pressure, which should bend and swell rather than restrike. */
        bool retrigger = !(self->play_down & (1u << voice)) || self->play_note[voice] != note;
        self->play_note[voice] = (uint8_t)note;
        play_synth(voice, self->preset_for(self->edit_voice), velocity, note << 8, retrigger);
    }

    /* Stop any surface voice that is no longer being played. */
    void release_play_voices(uint16_t keep) {
        uint16_t gone = (uint16_t)(play_down & ~keep);
        for (int v = 0; v < 16; ++v)
            if (gone & (1u << v)) synth_note_up(v);
        play_down = keep;
    }

    void draw_play_surface(void) {
        /* Chords and Drums print pad circles on rows 2..13 only, so the surface
           sits on them rather than spilling onto the control rows. On a blank
           plate it takes everything above the transport row. */
        int sy = plate_is_printed() ? 2 : 0;
        int sh = plate_is_printed() ? 12 : 15;

        /* The top row is the highest note, so the roots descend. Two degrees
           between rows spans about four octaves. */
        for (int i = 0; i < sh; ++i)
            string_roots[i] = (uint8_t)life_degree_to_note((sh - 1 - i) * 2, root_note(),
                                                           scale_mask());

        play_seen = 0;
        play_surface.do_play_surface(0, sy, LIFE_W, sh, LIFE_PLAY_VOICES,
                                     LED_RGB(0, 2, 4), life_voice_bright[edit_voice],
                                     string_roots, play_surface_note, this,
                                     HORIZONTAL | SHOW_BACKGROUND | STRINGOPHONIC_MONO,
                                     scale_mask(), current_key);
        release_play_voices(play_seen);

        draw_voice_selector();
        draw_voice_nav(LIFE_UI_PLAY);
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
    /* The synth page printed on the CHORDS and DRUMS overlays. Both print the
       same one - checked against panel_art/chords.png and panel_art/drums.png,
       label for label - and both give pad circles only on rows 2..13, with
       printed control rows at 0,1 and 14,15.

       Regions: upper sliders 0,2..16,7 - lower 0,7..8,14 - XY pad 9,7..16,14.
       That leaves column 8 rows 7..13 as the XY button column, which is where
       MOD and XY are printed, hence XY_BUTTONS_ON_LEFT.

       The payoff is that every printed label on the overlay is CORRECT: the
       order below is read straight off the silkscreen, and passing col = 0 lets
       each slider take its colour from the parameter's own metadata, so the
       printed colour groups line up for free.

       Rows 0..1 and 14..15 are the printed control rows on Chords, so nothing
       goes there - which conveniently leaves our transport row alone. */
    void draw_printed_sound_page(int preset) {
        static const int top[16] = {            /* rows 2..6, 16 sliders 5 high */
            VOICE_PARAM_ATTACK,                 /* A     */
            VOICE_PARAM_DECAY,                  /* D     */
            VOICE_PARAM_SUSTAIN,                /* S     */
            VOICE_PARAM_RELEASE,                /* R     */
            VOICE_PARAM_STEREO,                 /* PAN   */
            VOICE_PARAM_SUBOSC,                 /* SUB   */
            VOICE_PARAM_CUTOFF_HP,              /* HP    */
            VOICE_PARAM_CUTOFF_LP,              /* LP    */
            VOICE_PARAM_RESONANCE,              /* RESO  */
            VOICE_PARAM_DELAY_SEND,             /* DELAY */
            MIX_PARAM_DELAY_TIME + 128,         /* TIME  */
            MIX_PARAM_DELAY_FEEDBACK + 128,     /* FBK   */
            VOICE_PARAM_REVERB_SEND,            /* VERB  */
            MIX_PARAM_REVERB_FEEDBACK + 128,    /* TAIL  */
            MIX_PARAM_REVERB_SHIMMER + 128,     /* GLOW  */
            VOICE_PARAM_VOLUME,                 /* VOL   */
        };
        static const int bot[8] = {             /* rows 7..13, 8 sliders 7 high */
            VOICE_PARAM_GLIDE,                  /* GLIDE  */
            VOICE_PARAM_PITCH,                  /* PITCH  */
            VOICE_PARAM_OCTAVE,                 /* OCT    */
            VOICE_PARAM_CHORUS,                 /* CHORUS */
            VOICE_PARAM_WAVEFOLD,               /* FOLD   */
            VOICE_PARAM_SAMPLE_START,           /* START  */
            VOICE_PARAM_SAMPLE_LENGTH,          /* END    */
            VOICE_PARAM_TIMESTRETCH,            /* SPEED  */
        };
        synth_param_sliders_block(preset_pages.slider_banks[0], 0, 2, 16, 5, preset, top);
        synth_param_sliders_block(preset_pages.slider_banks[1], 0, 7, 8, 7, preset, bot);
        synth_xy_block(&preset_pages.the_xy_pad, preset, 8, 7, 7, 7,
                       WHITE, BLUE, false, 0, XY_BUTTONS_ON_LEFT);

        draw_voice_selector();
        draw_voice_nav(LIFE_UI_PRESET);
    }

    /* What the second screen calls the sound page's layout. */
    /* Chords and Drums print the same synth page. Tested with a mask rather
       than equality: the codes are distinct bits (TOADSTEP 0x2, DRUMS 0x10,
       CHORDS 0x20), so a unit that reports extra bits alongside the plate still
       matches. */
    /* Read every frame, NOT cached at load.

       Caching it was a mistake: if the straps are not readable yet when the
       panel loads, or the value settles late, a wrong answer sticks forever and
       the printed layout simply never appears. Reading it live costs nothing
       and cannot go stale. */
    bool plate_is_printed(void) const {
        if (pref_plate == LIFE_PLATE_CHORDS || pref_plate == LIFE_PLATE_DRUMS) return true;
        if (pref_plate == LIFE_PLATE_BLANK) return false;
        return (plate_at_construct & (FRONT_PANEL_CODE_CHORDS | FRONT_PANEL_CODE_DRUMS)) != 0;
    }

    const char *plate_layout_name(void) const {
        switch (pref_plate) {
        case LIFE_PLATE_CHORDS: return "Chords";
        case LIFE_PLATE_DRUMS: return "Drums";
        case LIFE_PLATE_BLANK: return "standard";
        default: break;
        }
        int code = plate_at_construct;
        if (code & FRONT_PANEL_CODE_CHORDS) return "Chords";
        if (code & FRONT_PANEL_CODE_DRUMS) return "Drums";
        if (code & FRONT_PANEL_CODE_TOADSTEP) return "Toadstep";
        return "standard";
    }

    /* Report the front panel every two seconds.

       Logging it once at load, or only on change, is useless here: Device Logs
       cannot be attached during boot, and a value that starts equal to a zeroed
       member never counts as a change. A heartbeat can be read whenever you get
       round to connecting. Every thirty seconds once the question is settled -
       often enough to catch a strap read that starts working, quiet enough to
       leave in.

       Printed in decimal as well as hex because it is not established that the
       firmware's printf handles %x - and if it does not, a hex-only readout is
       a lie about the value rather than a report of it. */
    void log_plate_periodically(void) {
        uint32_t now = time_us();
        if (plate_log_us && (uint32_t)(now - plate_log_us) < 30000000u) return;
        plate_log_us = now ? now : 1;

        int code = get_frontpanel_code();
        printf("life: frontpanel now=%d at_construct=%d orient=%d chords=%d drums=%d -> %s\n",
               code, plate_at_construct, get_frontpanel_orientation(),
               (code & FRONT_PANEL_CODE_CHORDS) ? 1 : 0,
               (code & FRONT_PANEL_CODE_DRUMS) ? 1 : 0,
               plate_layout_name());
    }

    void draw_preset_editor(void) {
        int preset = preset_for(edit_voice);

        /* The firmware reads the mounted overlay off resistor straps, so we do
           not have to ask.

           Chords and Drums print the same synth page, so they share a layout.
           Everything else gets preset_pages_t::edit(), and that is not a
           fallback: its column order IS Toadstep's printed fader order, top
           bank and bottom bank both, just at 5 rows high instead of 8. Blocks
           prints no labels at all, so nothing can be wrong there. */
        if (plate_is_printed()) {
            draw_printed_sound_page(preset);
            return;
        }

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
        draw_voice_nav(LIFE_UI_PRESET);
    }

    /* The preset picker: folders on the left, 64 slots on the right, with its
       own cancel and OK buttons.

       It places those buttons at (14, y+15) and (15, y+15) - exactly where our
       stop and play pads live. So this is the ONE mode that does not draw
       transport: the picker owns row 15 while it is up. That is the right call
       for a modal file dialog, and x still escapes because the picker leaves
       (13,15) alone. */
    /* Built from the picker's own pieces rather than saveload_action().

       That wrapper hardcodes its cancel and OK to (14, y+15) and (15, y+15),
       which is why it looked as though the picker could not be moved. It can:
       preset_picker() takes its own y range and the save and load buttons are
       placed separately. So on a printed plate the file grid sits on the pad
       circles and the buttons land under the printed SAVE and LOAD at (12,14)
       and (13,14).

       It also frees the transport corner, so play and stop stay live here like
       everywhere else. x cancels, as it does in every other mode. */
    /* Scenes, placed the same way as the preset picker rather than through
       panel_page_t::saveload(), for the same reason: that wrapper fixes its
       buttons to the transport corner.

       panel_load_button only STAGES a load. Finalising in the same frame races
       the deserialise, so the documented shape is to poll is_panel_load_staged()
       and finalise once it reports complete. */
    void draw_scene_page(void) {
        int grid_y = plate_is_printed() ? 2 : 0;
        scene_picker.panel_picker(grid_y, grid_y + 8, FLAG_PICKER_ENABLE_DELETE);
        draw_voice_selector();

        if (scene_picker.panel_save_button(LIFE_SAVE_X, LIFE_FILE_BTN_Y))
            scroll_to_page(0);
        if (scene_picker.panel_load_button(LIFE_LOAD_BTN_X, LIFE_FILE_BTN_Y))
            scene_commit_wait = 250;      /* about a second of frames */
    }

    /* Returns true if the panel is about to be replaced, in which case the
       caller must stop touching anything: an accepted request queues a swap of
       this whole object. */
    bool poll_staged_scene(void) {
        if (scene_commit_wait <= 0) return false;
        if (is_panel_load_staged()) {
            scene_commit_wait = 0;
            if (scene_picker.request_panel_load_finalise()) {
                release_all_voices();
                return true;
            }
        } else if (--scene_commit_wait == 0) {
            printf("life: scene load never staged, commit abandoned\n");
        }
        return false;
    }

    /* Four voice selectors at the top left, on printed plates only.

       Chords row 0 reads ALL / TREBLE / BASS / MELODY and is a control strip
       rather than playable pads, so the selector can live there permanently and
       every page that acts on "the current voice" shows the same four pads in
       the same place. A blank plate has no spare row - every pad there is world
       - so it keeps the selector inside the voice editor instead. */
    bool voice_selector_pad(int x, int y) const {
        return plate_is_printed() && y == 0 && x < LIFE_NUM_VOICES;
    }

    /* Row 15, x0..x3: where you can go from any per-voice page.

       These used to live only in the voice editor, so reaching the sound page
       from the preset picker meant going back first. They are the same four
       pads in the same place on the editor, the picker, the sound page and the
       play surface, and the one you are on is lit. */
    bool nav_pad(int x, int y) const { return y == LIFE_SOUND_Y && x < 4; }

    void draw_voice_nav(int mode) {
        static const uint32_t on[4] = { LED_RGB(12, 9, 26), LIFE_COL_ACTION,
                                        LED_RGB(26, 13, 0), LED_RGB(0, 26, 10) };
        static const uint32_t off[4] = { LED_RGB(4, 3, 9), LED_RGB(0, 4, 5),
                                         LED_RGB(7, 4, 0), LED_RGB(0, 7, 3) };
        static const char *help[4] = {
            "load a preset into this voice",
            "edit this voice's sound",
            "this voice's settings - tap again to flip the page",
            "play this voice by hand",
        };
        const int here[4] = { LIFE_UI_LOAD, LIFE_UI_PRESET, LIFE_UI_VOICE, LIFE_UI_PLAY };

        for (int i = 0; i < 4; ++i) {
            bool current = (mode == here[i]);
            if (!button(i, LIFE_SOUND_Y, current ? on[i] : off[i], NOT_ISOLATED, help[i]))
                continue;
            if (i == 2 && mode == LIFE_UI_VOICE) voice_page = voice_page ? 0 : 1;
            else ui_mode = (uint8_t)here[i];
        }
    }

    void draw_voice_selector(void) {
        if (!plate_is_printed()) return;
        for (int v = 0; v < LIFE_NUM_VOICES; ++v)
            if (button(v, 0, v == edit_voice ? life_voice_bright[v] : life_voice_dim[v],
                       NOT_ISOLATED, "which voice the editors and the play surface act on"))
                edit_voice = (uint8_t)v;
    }

    void draw_preset_loader(void) {
        int preset = preset_for(edit_voice);
        int grid_y = plate_is_printed() ? 2 : 0;

        preset_pages.picker.preset_picker(preset, grid_y, grid_y + 8,
                                          FLAG_PICKER_ENABLE_DELETE);
        draw_voice_selector();

        bool done = preset_pages.picker.preset_save_button(preset, LIFE_SAVE_X, LIFE_FILE_BTN_Y);
        if (done) printf("life: preset SAVE ch=%d\n", preset);
        if (preset_pages.picker.preset_load_button(preset, LIFE_LOAD_BTN_X, LIFE_FILE_BTN_Y)) {
            printf("life: preset LOAD ch=%d\n", preset);
            done = true;
        }
        if (done) ui_mode = LIFE_UI_VOICE;
        draw_voice_nav(LIFE_UI_LOAD);
    }

    /* --- settings pages -------------------------------------------------

       Rendered with the system helpers, which return the left-button edit
       delta. The right pair page; the left pair adjust. Both are system
       territory on every faceplate, so the panel never touches them. */

    /* Global only. Per-voice config moved onto the grid, where it is one tap
       deep and visible all at once instead of fourteen side-button clicks. */
    int settings_page_count(void) { return 18; }

    /* Page 1 is the scene save/load picker. on_serialise() has always described
       the world and every voice setting, but without this there was no way to
       trigger a save - the state was serialisable and unreachable. */
    int get_num_pages(void) override { return 2; }
    int get_num_panel_settings_pages(void) override { return settings_page_count(); }

    static int clamp_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    /* Every settings page emits help text.

       The 4-character label on the grid is all the hardware can show, and "SIM"
       or "STAB" tells you nothing on its own. The second-screen view is the only
       place this panel can use words, so each page says what it does, what the
       value means right now, and what it affects.

       Markup per ide_api.md: `#fc2` is a theme-rotated 3-digit hex colour, `#*`
       is bold, and `#.` resets - styles are sticky until reset. */
    /* on_serialise_settings() describes the preferences, but nothing writes
       them on its own - the panel has to ask. Called after the settings page has
       had its go, so an edit survives a power cycle. */
    void flush_settings(void) {
        if (!settings_dirty) return;
        settings_dirty = false;
        (void)save_settings_to_sd(false);
    }

    /* life_rulesets holds name and masks together; the settings page wants the
       names on their own. */
    static const char *const *life_ruleset_names(void) {
        static const char *names[LIFE_NUM_RULESETS];
        for (int i = 0; i < LIFE_NUM_RULESETS; ++i) names[i] = life_rulesets[i].name;
        return names;
    }

    void draw_settings(int page) {
        int idx = -1 - page;                       /* page -1 is our page 0 */
        char buf[8];

        /* Grouped: musical, then the world, then feel, then output, then the
           control surface. The set-once I/O pages sit at the end so nothing you
           reach for often is behind them. */
        switch (idx) {
        case 0: {
            int d = draw_system_style_enum_settings_page("KEY ", pref_root, life_note_names, 12);
            if (d) settings_dirty = true;
            if (d) { pref_root = (uint8_t)((pref_root + d + 120) % 12); apply_scale_to_system(); }
            set_help_text("#fc2#*Key#. - root note, now #fc2#*%s#.. Sets the whole instrument's "
                          "key, not just this panel.", life_note_names[pref_root]);
            break;
        }
        case 1: {
            int d = draw_system_style_enum_settings_page("SCAL", pref_scale, life_scale_names,
                                                        LIFE_NUM_SCALES);
            if (d) settings_dirty = true;
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
            if (d) settings_dirty = true;
            if (d) pref_octave = (uint8_t)clamp_int(pref_octave + d, 1, 7);
            set_help_text("#fc2#*Octave#. - base octave #fc2#*%d#. of 7. The bottom grid row "
                          "plays %s%d; higher rows climb the scale from there.",
                          pref_octave, life_note_names[pref_root], pref_octave);
            break;
        }
        case 3: {
            int d = draw_system_style_enum_settings_page("GEN ", gen_rate, life_rate_names,
                                                        LIFE_NUM_RATES);
            if (d) settings_dirty = true;
            if (d) gen_rate = (uint8_t)clamp_int(gen_rate + d, 0, LIFE_NUM_RATES - 1);
            set_help_text("#fc2#*Generation rate#. - the world evolves every #fc2#*%s#.. This is "
                          "separate from the voice rates: slow it down for a palette that breathes.",
                          life_rate_names[gen_rate]);
            break;
        }
        case 4: {
            int d = draw_system_style_enum_settings_page("RULE", rule_set, life_ruleset_names(),
                                                        LIFE_NUM_RULESETS);
            if (d) { settings_dirty = true;
                     rule_set = (uint8_t)clamp_int(rule_set + d, 0, LIFE_NUM_RULESETS - 1); }
            set_help_text("#fc2#*Rule#. - %s", life_rule_help[rule_set]);
            break;
        }
        case 5: {
            snprintf(buf, sizeof(buf), "%d", respawn_floor);
            int d = draw_system_style_settings_page("FLOR", buf, respawn_floor * 100 / 64);
            if (d) settings_dirty = true;
            if (d) respawn_floor = (uint8_t)clamp_int(respawn_floor + d, 0, 64);
            set_help_text("#fc2#*Respawn floor#. - if fewer than #fc2#*%d#. of 256 cells are "
                          "alive, sprinkle new ones in. 0 disables it and lets the world die.",
                          respawn_floor);
            break;
        }
        case 6: {
            snprintf(buf, sizeof(buf), "%d", respawn_amount);
            int d = draw_system_style_settings_page("SEED", buf, respawn_amount * 100 / 64);
            if (d) settings_dirty = true;
            if (d) respawn_amount = (uint8_t)clamp_int(respawn_amount + d, 1, 64);
            set_help_text("#fc2#*Respawn amount#. - how many cells to sprinkle, now #fc2#*%d#.. "
                          "Also what the SEED pad in the action layer drops in.", respawn_amount);
            break;
        }
        case 7: {
            snprintf(buf, sizeof(buf), "%d", respawn_stable);
            int d = draw_system_style_settings_page("STAB", buf, respawn_stable * 100 / 32);
            if (d) settings_dirty = true;
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
        case 8: {
            snprintf(buf, sizeof(buf), "%d", swing);
            int d = draw_system_style_settings_page("SWNG", buf, swing);
            if (d) { settings_dirty = true; swing = (uint8_t)clamp_int(swing + d, 0, 100); }
            if (swing)
                set_help_text("#fc2#*Swing#. - #fc2#*%d%%#. of %s. Every voice shuffles together; "
                              "the world keeps its own straight time.", swing,
                              swing_pattern ? "the chosen pattern" : "plain 16th swing");
            else
                set_help_text("#fc2#*Swing#. - #fc2#*straight#.. Turn it up to shuffle every "
                              "voice together.");
            break;
        }
        case 9: {
            int d = draw_system_style_enum_settings_page("SWPT", swing_pattern,
                                                        life_swing_names, 8);
            if (d) { settings_dirty = true;
                     swing_pattern = (uint8_t)clamp_int(swing_pattern + d, 0, 7); }
            set_help_text("#fc2#*Swing feel#. - %s. Only audible with SWNG above zero.",
                          swing_pattern ? "one of the seven shuffle patterns" : "plain 16th swing");
            break;
        }
        case 10: {
            int d = draw_system_style_enum_settings_page("PLTE", pref_plate, life_plate_names, 4);
            if (d) { settings_dirty = true;
                     pref_plate = (uint8_t)clamp_int(pref_plate + d, 0, 3); }
            if (pref_plate == LIFE_PLATE_AUTO)
                set_help_text("#fc2#*Faceplate#. - #fc2#*auto#., panel reads %d now, %d at "
                              "startup. If your Chords or Drums plate is not recognised, set "
                              "it here.", get_frontpanel_code(), plate_at_construct);
            else
                set_help_text("#fc2#*Faceplate#. - forced to #fc2#*%s#.. The sound page uses "
                              "that layout whatever the panel reports.",
                              life_plate_names[pref_plate]);
            break;
        }
        case 11: {
            int d = draw_system_style_enum_settings_page("OUT ", pref_sink, life_sink_names, 3);
            if (d) settings_dirty = true;
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
        case 12: {
            int d = draw_system_style_enum_settings_page("PORT", pref_port, life_port_names, 5);
            if (d) settings_dirty = true;
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
        case 13: {
            bool on = pref_send_cc != 0;
            int d = draw_system_style_bool_settings_page("CC  ", on);
            if (d) settings_dirty = true;
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
        case 14: {
            if (pref_cc_in) snprintf(buf, sizeof(buf), "%d", pref_cc_in);
            else snprintf(buf, sizeof(buf), "OFF");
            int d = draw_system_style_settings_page("CIN ", buf, pref_cc_in * 100 / 92);
            if (d) settings_dirty = true;
            if (d) pref_cc_in = (uint8_t)clamp_int(pref_cc_in + d, 0,
                                                  127 - (CC_PARAM_COUNT - 1));
            if (pref_cc_in)
                set_help_text("#fc2#*CC control#. - %d controllers from #fc2#*CC%d#. to "
                              "#fc2#*CC%d#. on the system channel drive key, scale, the world "
                              "and every voice.", CC_PARAM_COUNT, pref_cc_in,
                              cc_last_number(pref_cc_in));
            else
                set_help_text("#fc2#*CC control#. - #fc2#*off#.. Turn on to drive key, scale, "
                              "the world and every voice from a controller or a DAW.");
            break;
        }
        case 15: {
            bool on = pref_cc_out != 0;
            int d = draw_system_style_bool_settings_page("COUT", on);
            if (d) { pref_cc_out = on ? 0 : 1; settings_dirty = true; cc_out_primed = false; }
            if (pref_cc_out)
                set_help_text("#fc2#*CC mirror#. - #fc2#*on#.. Changes made here are sent back "
                              "out on the same controllers, so a DAW or a controller stays in "
                              "step with the panel.");
            else
                set_help_text("#fc2#*CC mirror#. - #fc2#*off#.. The panel listens but does not "
                              "report its own changes.");
            break;
        }
        case 16: {
            bool on = pref_cv_out != 0;
            int d = draw_system_style_bool_settings_page("CV  ", on);
            if (d) { pref_cv_out = on ? 0 : 1; settings_dirty = true; }
            set_help_text("#fc2#*CV out#. - %s. A follows how full the world is, B how much is "
                          "changing. 0 to 5 volts.", pref_cv_out ? "#fc2#*on#." : "#fc2#*off#.");
            break;
        }
        case 17: {
            bool on = pref_note_in != 0;
            int d = draw_system_style_bool_settings_page("NIN ", on);
            if (d) { pref_note_in = on ? 0 : 1; settings_dirty = true; }
            if (pref_note_in)
                set_help_text("#fc2#*Note input#. - #fc2#*on#.. Played notes draw cells into the "
                              "world, one column per note, left to right. Off-scale notes snap "
                              "to the nearest degree.");
            else
                set_help_text("#fc2#*Note input#. - #fc2#*off#.. Turn on to draw into the world "
                              "from a keyboard.");
            break;
        }
        default:
            break;
        }
    }

    void on_ui(int delta_time_us) override {
        (void)delta_time_us;

        if (poll_staged_scene()) return;

        log_plate_periodically();
        follow_musical_state();
        apply_pending_cc();
        apply_note_input();

        int page = get_scroll_page();

        /* Leaving the surface with a finger down would strand that note: the
           callback that would have released it only fires while the surface is
           being drawn. Checked before the page dispatch so it also covers
           paging away to the settings or scene pages. */
        bool on_surface = (page == 0 && ui_mode == LIFE_UI_PLAY);
        if (play_active && !on_surface) release_play_voices(0);
        play_active = on_surface;

        /* "call this when you close the file picker" - and we can leave one by
           routes it never sees: the nav strip, x, or a page change. The picker
           PREVIEWS by loading into the live preset slot, so without this the
           preview state outlives the visit and quietly overwrites an edit made
           afterwards. The stock wrappers get away with it because their own
           cancel and OK are the only ways out.

           reset_which_slot_is_selected() is the matching call on the way in:
           "it snaps the current slection to the caller's state". Without it the
           picker opens still holding the LAST voice's selection, and since it
           previews by loading, opening it for one voice could overwrite
           another. */
        /* Tracked as WHICH CHANNEL it is open for, not merely whether it is
           open. The voice selector sits on the picker page, so the channel can
           change while it is up - and the picker treats a channel it was never
           closed on as an abandoned preview and restores that voice to how it
           was before you opened it. Switching voice therefore has to close the
           old channel and open the new one, exactly as leaving and returning
           would. */
        int want_channel = (page == 0 && ui_mode == LIFE_UI_LOAD)
                               ? preset_for(edit_voice) : -1;
        if (want_channel != picker_channel) {
            printf("life: picker %d -> %d (mode=%d page=%d voice=%d)\n",
                   picker_channel, want_channel, ui_mode, page, edit_voice);
            if (picker_channel >= 0) preset_pages.picker.on_done();
            if (want_channel >= 0) preset_pages.picker.reset_which_slot_is_selected();
            picker_channel = want_channel;
        }

        bool on_scene_picker = (page == 1);
        if (on_scene_picker && !scene_picker_open) scene_picker.reset_which_slot_is_selected();
        if (scene_picker_open && !on_scene_picker) scene_picker.on_done();
        scene_picker_open = on_scene_picker;

        if (page < 0) {
            draw_settings(page);
            draw_voice_selector();   /* the per-voice pages act on this */
            flush_settings();
            send_param_ccs();
            return;
        }
        if (page == 1) {
            leds_clear();
            draw_scene_page();
            set_help_text("#fc2#*Scenes#. - save or load the world and every voice setting");
            return;
        }
        if (page > 1) {
            scroll_to_page(1);
            return;
        }

        leds_clear();

        /* SYSTEM_NOTES.md section 4, and read_modifiers() in plinky-ambiotica:
           emit the modifier ONCE, at the top, BEFORE anything that tests it,
           NOT_ISOLATED because being held while another pad is tapped IS the
           gesture. Read its edge immediately - is_last_widget_*() refers to the
           most recently emitted widget, so nothing may come between. */
        modifier_held = shift_button(LIFE_MODIFIER_X, LIFE_MODIFIER_Y, LIFE_COL_MODIFIER,
                                     NOT_ISOLATED, "actions - tap again to leave");
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

        /* The play surface owns the grid the same way the synth editor does. */
        if (mode == LIFE_UI_PLAY) {
            draw_play_surface();
            set_help_text("#fc2#*Voice %d#. - play over the world. Notes sound, cells are "
                          "untouched.", edit_voice + 1);
            return;
        }

        /* The hosted synth editor owns the grid: it emits sliders and an XY pad
           rather than one button per pad, so the per-pad loop must not run over
           the top of it. */
        if (mode == LIFE_UI_PRESET) {
            draw_preset_editor();
            set_help_text("V%d #fc2#*sound#. - %s layout, panel %d - x to leave",
                          edit_voice + 1, plate_layout_name(), get_frontpanel_code());
            return;
        }

        /* ONE widget per pad, in the SAME order, EVERY frame, in every mode.
           The mode changes only each pad's colour and what a press does, which
           is why the voice editor lives inside this loop rather than drawing
           itself alongside it. */
        if (mode == LIFE_UI_VOICE) { draw_voice_selector(); draw_voice_nav(LIFE_UI_VOICE); }

        int touched_y = -1;
        for (int y = 0; y < LIFE_H; ++y) {
            for (int x = 0; x < LIFE_W; ++x) {
                if (x == LIFE_MODIFIER_X && y == LIFE_MODIFIER_Y) continue;
                if (is_transport_pad(x, y)) continue;   /* emitted once, above */
                if (mode == LIFE_UI_VOICE && (voice_selector_pad(x, y) || nav_pad(x, y)))
                    continue;

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

                bool pressed = button(x, y, col, NOT_ISOLATED, help);
                /* Read the level immediately: is_last_widget_held() refers to
                   the most recently emitted widget, so nothing may come between. */
                if (mode == LIFE_UI_VOICE && is_last_widget_held()) touched_y = y;
                if (!pressed) continue;

                if (mode == LIFE_UI_VOICE) {
                    do_voice_edit(x, y);
                } else if (mode == LIFE_UI_ACTION) {
                    /* The layer STAYS until you press x again. It used to close
                       after every action, which made auditioning rules or
                       setting up a mix a rhythm of x-tap-x-tap. Only the edit
                       pads move you elsewhere, and that is a mode change rather
                       than a dismissal. */
                    if (is_action_pad(x, y)) do_action(x, y);
                } else {
                    /* on_ui and on_sequence share the world, and on_sequence can
                       interrupt this at any instruction. */
                    on_sequence_lock_guard_t guard;
                    life_toggle(&world, x, y);
                }
            }
        }

        send_param_ccs();

        /* After the pads, so it lands on top of them. */
        if (mode == LIFE_UI_VOICE && touched_y >= 0) draw_readout(touched_y);

        if (mode == LIFE_UI_VOICE)
            set_voice_help_text();
        else if (mode == LIFE_UI_ACTION)
            set_help_text("#fc2#*Actions#. - %s, gen %s, swing %d%%, edit, mute, solo, clear, "
                          "seed, freeze, step", life_rulesets[rule_set].name,
                          life_rate_names[gen_rate], swing);
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
    /* Incoming MIDI                                                      */
    /* ================================================================== */

    void on_midi(uint32_t midimsg) override {
        /* Play a note, draw a cell. The note picks the row - it snaps to the
           nearest degree, so off-scale notes still draw instead of vanishing -
           and a write head moves one column per note, so a played phrase is
           written across the world left to right. */
        if (pref_note_in && IS_NOTE_ON(midimsg)) {
            int degree = life_note_to_degree(NOTE_NUMBER(midimsg), root_note(), scale_mask());
            int row = 15 - degree;
            if (row >= 0 && row < LIFE_H && note_in_count < 16) {
                note_in_row[note_in_count++] = (uint8_t)row;
            }
            return;
        }
        if (!IS_CC(midimsg)) return;
        if (CHANNEL_BYTE(midimsg) != (get_system_midi_channel() - 1)) return;

        int p = cc_param_for_number(CC_NUMBER(midimsg), pref_cc_in);
        if (p < 0) return;

        cc_in_value[p] = CC_VALUE(midimsg);
        cc_in_pending |= (uint64_t)1 << p;
    }

    void apply_cc(int p) {
        int v = cc_in_value[p];
        int prev = cc_in_last[p];
        cc_in_last[p] = (uint8_t)v;

        int voice = cc_voice_for_param(p);
        if (voice >= 0) {
            switch (cc_group_for_param(p)) {
            case 0: {                                   /* mute */
                uint8_t want = v >= 64 ? 1 : 0;
                if (v_muted[voice] != want) {
                    v_muted[voice] = want;
                    if (!voice_is_audible(voice)) release_voice(voice);
                }
                return;
            }
            case 1: {                                   /* rate */
                uint8_t r = (uint8_t)cc_to_index(v, LIFE_NUM_RATES);
                if (v_rate[voice] != r) {
                    release_voice(voice);               /* the step length changed */
                    v_rate[voice] = r;
                    last_edge_us[voice] = 0;
                    step_us[voice] = LIFE_DEFAULT_STEP_US;
                }
                return;
            }
            case 2: v_rule[voice] = (uint8_t)cc_to_index(v, SEL_COUNT); return;
            case 3: v_order[voice] = (uint8_t)cc_to_index(v, TRAV_COUNT); return;
            case 4: v_pitch[voice] = (int8_t)cc_to_range(v, -LIFE_PITCH_CENTRE,
                                                         LIFE_PITCH_CENTRE); return;
            case 5: v_length[voice] = (uint8_t)(cc_to_range(v, 1, 10) * 10); return;
            default: return;
            }
        }

        switch (p) {
        case CC_KEY:
            pref_root = (uint8_t)cc_to_index(v, 12);
            apply_scale_to_system();
            settings_dirty = true;
            break;
        case CC_SCALE:
            pref_scale = (uint8_t)cc_to_index(v, LIFE_NUM_SCALES);
            apply_scale_to_system();
            settings_dirty = true;
            break;
        case CC_OCTAVE:
            pref_octave = (uint8_t)cc_to_range(v, 1, 7);
            settings_dirty = true;
            break;
        case CC_GEN_RATE: gen_rate = (uint8_t)cc_to_index(v, LIFE_NUM_RATES); break;
        case CC_FLOOR: respawn_floor = (uint8_t)cc_to_range(v, 0, 64); break;
        case CC_SEED_AMOUNT: respawn_amount = (uint8_t)cc_to_range(v, 1, 64); break;
        case CC_STALL: respawn_stable = (uint8_t)cc_to_range(v, 0, 32); break;
        case CC_FREEZE: freeze_life = v >= 64 ? 1 : 0; break;
        case CC_CLEAR:
            if (cc_is_press(prev, v)) { on_sequence_lock_guard_t g; life_clear(&world); }
            break;
        case CC_SEED_NOW:
            if (cc_is_press(prev, v)) {
                on_sequence_lock_guard_t g;
                life_respawn_apply(&world, &respawn, (int)respawn_amount);
            }
            break;
        case CC_STEP:
            if (cc_is_press(prev, v)) step_once_request = true;
            break;
        default: break;
        }
    }

    /* The canonical controller value for a parameter's CURRENT setting. Mirror
       of apply_cc, and the two must agree or two-way CC drifts. */
    int cc_current_value(int p) const {
        int voice = cc_voice_for_param(p);
        if (voice >= 0) {
            switch (cc_group_for_param(p)) {
            case 0: return v_muted[voice] ? 127 : 0;
            case 1: return cc_from_index(v_rate[voice], LIFE_NUM_RATES);
            case 2: return cc_from_index(v_rule[voice], SEL_COUNT);
            case 3: return cc_from_index(v_order[voice], TRAV_COUNT);
            case 4: return cc_from_range(v_pitch[voice], -LIFE_PITCH_CENTRE,
                                         LIFE_PITCH_CENTRE);
            case 5: return cc_from_range(v_length[voice] / 10, 1, 10);
            default: return 0;
            }
        }
        switch (p) {
        case CC_KEY: return cc_from_index(pref_root, 12);
        case CC_SCALE: return cc_from_index(pref_scale, LIFE_NUM_SCALES);
        case CC_OCTAVE: return cc_from_range(pref_octave, 1, 7);
        case CC_GEN_RATE: return cc_from_index(gen_rate, LIFE_NUM_RATES);
        case CC_FLOOR: return cc_from_range(respawn_floor, 0, 64);
        case CC_SEED_AMOUNT: return cc_from_range(respawn_amount, 1, 64);
        case CC_STALL: return cc_from_range(respawn_stable, 0, 32);
        case CC_FREEZE: return freeze_life ? 127 : 0;
        /* the momentary three have no state to report */
        default: return -1;
        }
    }

    /* Mirror any parameter whose value the outside world has not been told.

       Runs on core0 after touch has been handled, so a knob turned on the grid
       reaches the DAW in the same frame. There is no feedback guard beyond this
       and the round-trip stability in ccmap.h, and none is needed: an echo of
       what we sent decodes to what we already hold, so nothing changes and
       nothing is sent back. */
    void send_param_ccs(void) {
        if (!pref_cc_out || !pref_cc_in) return;
        uint8_t ports = midi_ports();
        if (!ports) return;
        int channel = get_system_midi_channel() - 1;

        for (int p = 0; p < CC_PARAM_COUNT; ++p) {
            int v = cc_current_value(p);
            if (v < 0) continue;
            if (cc_out_primed && cc_out_last[p] == (uint8_t)v) continue;
            cc_out_last[p] = (uint8_t)v;
            midi_write_cc(ports, channel, pref_cc_in + p, v);
        }
        cc_out_primed = true;
    }

    /* Paint whatever arrived as notes. Core0, under the lock, because
       on_sequence is reading the same world. */
    void apply_note_input(void) {
        int n = note_in_count;
        if (!n) return;
        note_in_count = 0;
        on_sequence_lock_guard_t guard;
        for (int i = 0; i < n; ++i) {
            life_set(&world, note_write_x, note_in_row[i], 1);
            note_write_x = (uint8_t)((note_write_x + 1) & (LIFE_W - 1));
        }
    }

    /* Drain on core0. on_midi can add bits while we are here, so take the whole
       word and clear it in one go rather than testing and clearing per bit. */
    void apply_pending_cc(void) {
        uint64_t pending = cc_in_pending;
        if (!pending) return;
        cc_in_pending &= ~pending;
        for (int p = 0; p < CC_PARAM_COUNT; ++p) {
            if (!(pending & ((uint64_t)1 << p))) continue;
            apply_cc(p);
            /* The sender already knows: do not echo it back at them. */
            int now = cc_current_value(p);
            if (now >= 0) cc_out_last[p] = (uint8_t)now;
        }
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
        FIELD("rule", rule_set);
        FIELD("swing", swing);
        FIELD("swpat", swing_pattern);
        FIELD("vacc", v_accent);
        FIELD("vprb", v_prob);
        FIELD("vrat", v_ratchet);
        FIELD("vtie", v_tie);
        FIELD("vcnd", v_cond);
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
            v_accent[v] = 60;
            v_prob[v] = 0;
            v_ratchet[v] = 0;
            v_tie[v] = 0;
            v_cond[v] = 0;
            v_channel[v] = (int8_t)(v + 1);   /* a different MIDI channel each */
        }
        gen_rate = 8;
        respawn_floor = 12;
        respawn_amount = 10;
        respawn_stable = 4;
        freeze_life = 0;
        rule_set = 0;             /* Conway */
        swing = 0;
        swing_pattern = 0;
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
            pref_cc_in = CC_IN_DEFAULT_BASE;
            pref_cc_out = 1;
            pref_cv_out = 1;
            pref_note_in = 1;
            pref_plate = LIFE_PLATE_AUTO;
        }

        OBJECT_BEGIN(s);
        FIELD("root", pref_root);
        FIELD("scale", pref_scale);
        FIELD("oct", pref_octave);
        FIELD("sink", pref_sink);
        FIELD("port", pref_port);
        FIELD("cc", pref_send_cc);
        FIELD("ccin", pref_cc_in);
        FIELD("ccout", pref_cc_out);
        FIELD("cvout", pref_cv_out);
        FIELD("notein", pref_note_in);
        FIELD("plate", pref_plate);
        OBJECT_END(s);

        clamp_settings();
        return true;
    }
};
