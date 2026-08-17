/* plinky-life - minimal stand-ins for the Plinky panel API.

   This is NOT the SDK and it is NOT authoritative. It exists so the generated
   plinky_life.cpp can be compiled locally and type-checked: wrong argument
   counts, misspelled members, bad types and dead code get caught here instead
   of in a flash-and-see cycle on the device.

   Every declaration below is transcribed from
   https://plinky12.com/docs/ide_api/llm.txt. If the real firmware disagrees,
   the real firmware is right and this file is the bug.

   Passing this check does NOT mean the panel is correct on hardware. It means
   the panel is self-consistent with the published API surface. */

#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* --- LEDs ---------------------------------------------------------------- */
#define LED_RGB(r, g, b) (uint32_t)((((uint32_t)(g)) << 24) | (((uint32_t)(r)) << 16) | (((uint32_t)(b)) << 8))
#define WHITE LED_RGB(15, 15, 15)
void set_led(int x, int y, uint32_t rgb15);
void leds_clear(void);
enum { FONT_4, FONT_4_BOLD, FONT_5 };
void leds_draw_string(int x, int y, uint8_t font, uint32_t rgb15, const char *c);
int leds_string_width(const char *c, uint8_t fontidx);

/* --- widgets ------------------------------------------------------------- */
#define ISOLATED true
#define NOT_ISOLATED false
bool button(int x, int y, int colour, bool isolated = ISOLATED, const char *help_text = nullptr);
bool invisible_button(int x, int y, bool isolated = ISOLATED, const char *help_text = nullptr);
bool shift_button(int x, int y, int colour, bool isolated = ISOLATED, const char *help_text = nullptr);
bool is_last_widget_held(void);
bool is_last_widget_pressed(void);
bool is_last_widget_released(void);
void set_help_text(const char *str, ...);

/* --- pages --------------------------------------------------------------- */
int get_scroll_page(void);
void scroll_to_page(int page);
void scroll_to_y_q16(int y_q16);
int draw_system_style_settings_page(const char *name4, const char *value4 = nullptr,
                                    int bar_percent = -1, int highlight_x1 = 0,
                                    int highlight_x2 = 0, uint32_t value_color = 0);
int draw_system_style_bool_settings_page(const char *name4, bool value);
int draw_system_style_enum_settings_page(const char *name4, int value, const char *const *options,
                                         int num_options, uint32_t value_color = 0);

/* --- transport and clock ------------------------------------------------- */
bool is_transport_playing(void);
bool has_transport_just_started(void);
bool has_sequencer_just_loaded(void);
bool has_transport_just_seeked(void);
void start_transport(int midi_ports = -1);
void stop_transport(int midi_ports = -1);
int64_t get_clock_phase(void);
uint32_t time_us(void);

static inline bool sequencer_should_advance_playhead(void) {
    if (!is_transport_playing()) return false;
    if (has_transport_just_started()) return false;
    if (has_sequencer_just_loaded()) return false;
    if (has_transport_just_seeked()) return false;
    return true;
}

enum divider_update_policy_t : uint8_t {
    UPDATE_DIV_NOW,
    UPDATE_DIV_NOW_AND_SNAP_CLOCK_BASE_TO_QUARTER_NOTE,
    UPDATE_DIV_ON_QUARTER_NOTE,
    UPDATE_DIV_ON_HALF_NOTE,
    UPDATE_DIV_ON_BAR,
};
#define DONT_UNWRAP_CLOCK 0
#define PLEASE_UNWRAP_CLOCK 1
#define CLOCK_DIVIDER_QUARTER_NOTE_Q16 65536u

struct clock_divider_t {
    int numerator, denominator;
    int64_t clock_base;
    uint16_t phase;
    void reset(int64_t clock_phase = 0, uint32_t grid_q16 = CLOCK_DIVIDER_QUARTER_NOTE_Q16,
               int requested_numerator = -1, int requested_denominator = -1);
    void set_clock_base(int64_t clock_phase, uint32_t grid_q16 = CLOCK_DIVIDER_QUARTER_NOTE_Q16);
    uint16_t phase_q16(void) const { return phase; }
    uint8_t phase_q7(void) const { return (uint8_t)(phase >> 9); }
    int64_t last_clock_phase(void) const { return _last_clock_phase; }
    /* Body transcribed from llm.txt, not approximated: the panel derives a
       playhead position from it after a seek, so the arithmetic matters. */
    uint32_t step_index(void) const {
        int n = numerator < 1 ? 1 : numerator;
        int d = denominator < 1 ? 1 : denominator;
        int64_t scaled = (_last_clock_phase - clock_base) * (int64_t)n;
        scaled = scaled >= 0 ? scaled / d : -(((-scaled) + d - 1) / d);
        int64_t step = scaled >= 0 ? scaled / CLOCK_DIVIDER_QUARTER_NOTE_Q16
                                   : -(((-scaled) + CLOCK_DIVIDER_QUARTER_NOTE_Q16 - 1)
                                       / CLOCK_DIVIDER_QUARTER_NOTE_Q16);
        return (uint32_t)step;
    }
    int64_t _last_clock_phase;
    int update(int64_t clock_phase = -1, int requested_numerator = -1, int requested_denominator = -1,
               divider_update_policy_t update_when = UPDATE_DIV_ON_HALF_NOTE,
               bool freerunning = false, bool unwrap_clock = DONT_UNWRAP_CLOCK);
};

/* --- play surface -------------------------------------------------------- */
#define HORIZONTAL 1
#define VERTICAL 2
#define SHOW_BACKGROUND 1024
#define TRACK_FINGERS_ACROSS_STRINGS 16
#define POLYPHONIC 0
#define MONOPHONIC 4
#define STRINGOPHONIC_MONO 8
#define STRINGOPHONIC_POLY 12

/* Transcribed from the IDE API docs, field for field - the widths matter:
   string_pos_q8 is 0..15*256, i.e. pad units with 8 fractional bits. */
struct finger_t {
    uint16_t string_pos_q8 : 12;   /* 0-15*256 */
    uint16_t string_idx : 4;       /* 0-15 */
    uint8_t pressure;              /* 0-255 */
    uint8_t voice_idx : 6;         /* 63 means no voice */
    uint8_t is_new : 1;            /* 1 = freshly allocated, i.e. retrigger */
    uint8_t is_touch : 1;          /* 1 = a real touch, 0 = poked virtual finger */
    inline int16_t fretted() const { return (string_pos_q8 + 128) >> 8; }
    inline int16_t fretted_q8() const { return (string_pos_q8 + 128) & (~255); }
    inline int16_t fretless_q8() const { return string_pos_q8; }
};

int scale_play_surface_note(int string_root_note, int string_pos, int scale_root_note,
                            uint16_t scale);

typedef void (*play_surface_note_fn)(void *user, int voice, int note, uint8_t velocity,
                                     finger_t finger);
typedef uint8_t (*scale_play_surface_brightness_fn)(void *user, int string_idx, int string_pos,
                                                    int x, int y, int note);

/* Transcribed body: the deadzone matters, it is what lets the value arrive
   exactly instead of asymptotically approaching. */
static inline int update_ema(int current, int target, int smooth_shift) {
    int deadzone = 1 << smooth_shift;
    int delta = target - current;
    if (delta <= deadzone && delta >= -deadzone) return target;
    return current + (delta >> smooth_shift);
}

/* The real body, not a declaration: it clamps to 255 and saturates at 128, and
   a stub that hid that let a rect TOTAL be passed here unnoticed. */
static inline int touch_pressure_curve_q7(int pressure) {
    if (pressure < 0) pressure = 0;
    if (pressure > 255) pressure = 255;
    return ((pressure * pressure) + (pressure << 7)) >> 8;
}
#define PLAY_SURFACE_PRESSURE_NOTE_ON 24
#define PLAY_SURFACE_PRESSURE_HOLD 16
/* Average pressure and position of all touches in a rectangle. x/y come back
   with 8 fractional bits; p is the TOTAL pressure over the rect. Returns false
   when nothing in the rectangle is firmly touched. (x1,y1) is top-left, or
   bottom-left when from_bottom is set. */
int count_touches_in_area(int x1, int y1, int x2, int y2, bool previous = false);
bool get_pressure_and_pos_in_rect(int x1, int y1, int w, int h, bool from_bottom, int *p, int *x, int *y,
                                  int *minx = NULL, int *miny = NULL, int *maxx = NULL, int *maxy = NULL,
                                  int ignore_pads_below = 0, int ignore_pads_above = 16,
                                  bool ignore_pads_along_x = false);
int get_touch_pressure_xy(int x, int y);
int get_touch_origin_x(int x, int y);
int get_touch_origin_y(int x, int y);

struct play_surface_t {
    finger_t fingers[16];
    uint32_t finger_seq[16];
    uint32_t next_finger_seq;
    int update_from_touch(int x1, int y1, int w, int h, int flags, int max_voices,
                          uint16_t string_mask = 0xffff, int string_pos_offset = 0);
    /* Injects a virtual finger. If poke_finger is used instead of
       update_from_touch, the surface never sees the raw pad grid. */
    void poke_finger(int string_idx, int pos_q8, int pressure, int max_voices, bool is_touch = false,
                     bool monophonic = false, int min_pressure_for_note_on = PLAY_SURFACE_PRESSURE_NOTE_ON,
                     uint8_t origin_raw = 255);
    void update_voices(void);
    finger_t get_finger_for_voice(int voice);
    void do_play_surface(int x, int y, int w, int h, int max_voices, uint32_t bg_col,
                         uint32_t root_col, const uint8_t *string_roots,
                         play_surface_note_fn note_fn, void *note_user = nullptr,
                         int flags = VERTICAL | SHOW_BACKGROUND | STRINGOPHONIC_MONO,
                         int scale = 0, int scale_root = -1,
                         scale_play_surface_brightness_fn brightness = nullptr,
                         void *brightness_user = nullptr, int highlight_pitch_class = -1);
    /* Second overload: fills the string roots for you from a starting pitch and
       a per-string interval in scale degrees. */
    void do_play_surface(int x, int y, int w, int h, int max_voices, uint32_t bg_col,
                         uint32_t root_col, int starting_pitch, int interval_degrees,
                         play_surface_note_fn note_fn, void *note_user = nullptr,
                         int flags = VERTICAL | SHOW_BACKGROUND | STRINGOPHONIC_MONO,
                         int scale = 0, int scale_root = -1,
                         scale_play_surface_brightness_fn brightness = nullptr,
                         void *brightness_user = nullptr, int highlight_pitch_class = -1);
};

/* --- synth voices -------------------------------------------------------- */
#define MAX_VOICES 12
#define DEFAULT_VOICE_ALLOCATOR_VOICES 8

struct voice_alloc_state_t {
    uint32_t source_id;
    uint32_t seq;
    uint8_t prio;
};
struct voice_allocator_t {
    voice_alloc_state_t _state[MAX_VOICES];
    int voice_allocate(uint32_t source_id, uint8_t prio, uint8_t subset_start = 0,
                       uint8_t subset_end = DEFAULT_VOICE_ALLOCATOR_VOICES,
                       int8_t preferred_preset_idx = -1);
    int garbage_collect_voices(uint32_t max_age);
    int voice_deallocate(uint32_t source_id, uint8_t subset_start = 0, uint8_t subset_end = MAX_VOICES);
    int find_voice(uint32_t source_id, uint8_t subset_start = 0, uint8_t subset_end = MAX_VOICES) const;
};

void play_synth(int voice, int preset_idx, int velocity, int note_q8, bool retrigger);
void synth_note_up(int voice);
int get_synth_retrigger(int voice);
#define SYNTH_VOICE_XY_OVERRIDE_NONE 255
typedef struct synth_voice_note_t {
    int note_q8;
    int16_t param_override_value;
    uint8_t velocity;
    int8_t preset_idx;
    uint8_t x_override;
    uint8_t y_override;
    int8_t param_override_idx;
} synth_voice_note_t;
void play_synth(int voice, const synth_voice_note_t &note, bool retrigger = false);
void set_synth_velocity(int voice, int velocity);
int get_synth_note(int voice);

/* --- MIDI ---------------------------------------------------------------- */
#define MIDI_PORT_NONE 0
#define MIDI_PORT_USB1 16
#define MIDI_PORT_USB2 32
#define MIDI_PORT_TRS1 64
#define MIDI_PORT_TRS2 128
#define MIDI_PORT_1 (MIDI_PORT_USB1 | MIDI_PORT_TRS1)
#define MIDI_PORT_ALL (MIDI_PORT_USB1 | MIDI_PORT_USB2 | MIDI_PORT_TRS1 | MIDI_PORT_TRS2)
#define MIDI_PORT_2 (MIDI_PORT_USB2 | MIDI_PORT_TRS2)

#define MIDIMSG(status, data1, data2)                                                              \
    ((uint32_t)(status) | ((uint32_t)(data1) << 8) | ((uint32_t)(data2) << 16))
#define MAKE_CCMSG(channel, cc, value) MIDIMSG(0xb0u | ((channel) & 0x0f), (cc), (value))
#define MAKE_NOTEONMSG(channel, note, velocity) MIDIMSG(0x90u | ((channel) & 0x0f), (note), (velocity))
#define MAKE_NOTEOFFMSG(channel, note, velocity) MIDIMSG(0x80u | ((channel) & 0x0f), (note), (velocity))

/* Incoming-message accessors, from llm.txt's MIDI section. */
#define STATUS_BYTE(m) ((uint8_t)((uint32_t)(m) >> 0))
#define STATUS_NIBBLE(m) ((uint8_t)(STATUS_BYTE(m) & 0xf0))
#define CHANNEL_BYTE(m) ((uint8_t)(STATUS_BYTE(m) & 0x0f))
#define DATA1_BYTE(m) ((uint8_t)((uint32_t)(m) >> 8))
#define DATA2_BYTE(m) ((uint8_t)((uint32_t)(m) >> 16))
#define CC_NUMBER(m) DATA1_BYTE(m)
#define CC_VALUE(m) DATA2_BYTE(m)
#define IS_CC(m) (STATUS_NIBBLE(m) == 0xb0)
#define NOTE_NUMBER(m) DATA1_BYTE(m)
#define NOTE_VELOCITY(m) DATA2_BYTE(m)
#define IS_NOTE_ON(m) ((STATUS_NIBBLE(m) == 0x90) && (DATA2_BYTE(m) != 0))

bool midi_write(uint8_t ports, uint32_t midimsg);
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int midi_write_cc(int ports, int channel, int cc, int val) {
    val = clampi(val, 0, 127);
    if (cc < 0 || cc > 127 || channel < 0 || channel > 15 || ports == 0) return val;
    midi_write((uint8_t)ports, MAKE_CCMSG(channel, cc, val));
    return val;
}
/* Level-triggered MIDI note declaration: declare what should be down each
   update, then send_declared_midi_notes() as the commit marker. */
uint8_t declare_midi_note(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t midi_ports = MIDI_PORT_1);
uint8_t declare_midi_note_for_preset_idx(int preset_idx, uint8_t note, uint8_t velocity,
                                         uint8_t midi_ports = MIDI_PORT_1, int midi_channel_wire = -1);
void send_declared_midi_notes(int enable_aftertouch_on_ports = MIDI_PORT_ALL);

int get_system_midi_channel(void);
uint8_t get_midi_channel_for_preset_idx(int preset_idx, bool respect_system_midi_channel);

/* --- synth preset editing -----------------------------------------------

   preset_pages_t is panel-HOSTED, not a system page: it takes a preset_idx, so
   passing the selected voice's preset makes it edit that voice's sound. The
   real struct owns 32 slider_t plus an xy_pad_t and a file_picker_t; the stub
   only needs the call shapes, so its size here is NOT representative. */
enum {
    SYNTH_FLAG_BUTTON_SIMPLE,
    SYNTH_FLAG_BUTTON_TUNE,
    SYNTH_FLAG_BUTTON_CHOP,
    SYNTH_FLAG_BUTTON_LOOP,
    SYNTH_FLAG_BUTTON_SYNC,
    SYNTH_FLAG_BUTTON_LOWPASS_GATE,
    SYNTH_FLAG_BUTTON_TUNINGMODE,
};

int synth_flags_button(int x, int y, int preset_idx, int synth_flag_enum,
                       int preset_idx_write_copies_mask = 0, int col = 0);

/* Only the parameters this panel names; the real enums are much longer. */
enum {
    VOICE_PARAM_CUTOFF_HP = 0, VOICE_PARAM_CUTOFF_LP, VOICE_PARAM_RESONANCE,
    VOICE_PARAM_ATTACK, VOICE_PARAM_DECAY, VOICE_PARAM_SUSTAIN, VOICE_PARAM_RELEASE,
    VOICE_PARAM_GLIDE, VOICE_PARAM_PITCH, VOICE_PARAM_OCTAVE, VOICE_PARAM_SUBOSC,
    VOICE_PARAM_WAVEFOLD, VOICE_PARAM_STEREO, VOICE_PARAM_DELAY_SEND,
    VOICE_PARAM_REVERB_SEND, VOICE_PARAM_TOAD_FACTOR, VOICE_PARAM_VOLUME,
    VOICE_PARAM_TIMESTRETCH, VOICE_PARAM_SAMPLE_START, VOICE_PARAM_SAMPLE_LENGTH,
    VOICE_PARAM_CHORUS, VOICE_PARAM_SIDECHAIN_AMOUNT, VOICE_PARAM_LAST,
};
enum {
    MIX_PARAM_DELAY_TIME, MIX_PARAM_DELAY_FEEDBACK,
    MIX_PARAM_REVERB_SHIMMER, MIX_PARAM_REVERB_FEEDBACK,
};

#define BLUE LED_RGB(0, 0, 31)
#define XY_BUTTONS_ON_RIGHT false
#define XY_BUTTONS_ON_LEFT true


struct slider_t { int _pad[8]; };
struct xy_pad_t { int _pad[12]; };
struct synth_preset_t;
#define FLAG_PICKER_ENABLE_DELETE 1
struct file_picker_t {
    int _pad[32];
    /* The pieces the wrappers are built from. These take their own coordinates,
       which is how a picker gets placed on a printed faceplate. */
    int preset_picker(int channel, int main_y, int hue_y, int flags = FLAG_PICKER_ENABLE_DELETE);
    void reset_which_slot_is_selected(void);
    bool preset_save_button(int channel, int x, int y);
    bool preset_load_button(int channel, int x, int y, int preset_idx_write_copies_mask = 0,
                            const synth_preset_t **loaded_preset_out = nullptr);
    int panel_picker(int main_y, int hue_y, int flags = FLAG_PICKER_ENABLE_DELETE);
    bool panel_save_button(int x, int y);
    bool panel_load_button(int x, int y);
    bool request_panel_load_finalise(void);
    void on_done(void);   /* "call this when you close the file picker" */
};
bool is_panel_load_staged(void);

int synth_param_sliders_block(slider_t *synth_sliders, int x, int y, int width, int length,
                              int preset_idx, const int *synth_params,
                              int preset_idx_write_copies_mask = 0, int slider_flags = 0);
bool synth_xy_block(xy_pad_t *xy_pad, int preset_idx, int x, int y, int width = 7, int height = 5,
                    uint32_t col = WHITE, uint32_t bg_col = BLUE, bool is_clear = false,
                    int preset_idx_write_copies_mask = 0, bool buttons_on_left = XY_BUTTONS_ON_RIGHT,
                    bool require_record_button_down_for_speed = false, bool read_only = false,
                    int flags = 0);

enum panel_page_load_mode_t : uint8_t { PANEL_PAGE_LOAD_COMMIT_ASAP, PANEL_PAGE_LOAD_STAGE_ONLY };

struct panel_page_t {
    file_picker_t picker;
    bool saveload(int y, bool draw_title = true, int flags = FLAG_PICKER_ENABLE_DELETE,
                  panel_page_load_mode_t load_mode = PANEL_PAGE_LOAD_COMMIT_ASAP);
};

struct preset_pages_t {
    slider_t slider_banks[2][16];
    xy_pad_t the_xy_pad;
    file_picker_t picker;

    int edit(int preset_idx, int y, int preset_idx_write_copies_mask = 0,
             bool include_flag_buttons = true, int slider_flags = 0);
    void xy_pad(int preset_idx, int x = 8, int y = 10, int preset_idx_write_copies_mask = 0,
                bool require_record_button_down_for_speed = false);
    int saveload_action(int preset_idx, int y, int flags = FLAG_PICKER_ENABLE_DELETE,
                        int preset_idx_write_copies_mask = 0);
    bool saveload(int preset_idx, int y, int flags = FLAG_PICKER_ENABLE_DELETE,
                  int preset_idx_write_copies_mask = 0);
};

/* --- front panel ---------------------------------------------------------
   The mounted overlay is readable: the firmware reads resistor straps. */
#ifndef FRONT_PANEL_CODE_NONE
#define FRONT_PANEL_CODE_NONE 0x00000
#define FRONT_PANEL_CODE_BLOCKS FRONT_PANEL_CODE_NONE
#define FRONT_PANEL_CODE_TOADSTEP 0x00002
#define FRONT_PANEL_CODE_DRUMS 0x00010
#define FRONT_PANEL_CODE_CHORDS 0x00020
#endif
int get_frontpanel_code(void);
int get_frontpanel_orientation(void);   /* 0=missing, 1=normal, 2=upside down */

/* --- musical state ------------------------------------------------------- */
extern uint8_t current_key;
extern uint16_t current_scale;
void set_current_key_and_scale(uint8_t key, uint16_t scale);
uint32_t get_current_musical_state_generation(void);
int64_t warp_clock_swing(int64_t clock, float amount);
int64_t warp_clock_stolper(int pattern, int64_t clock, float amount);
void set_cv_out_mv(int channel, int mv);
int get_cv_out_mv(int channel);

/* --- serialisation -------------------------------------------------------

   These mirror the SHAPE of save_and_load.h, not just its names, because the
   shape is where the mistakes are. Specifically:

     - OBJECT_BEGIN opens a while + switch and every FIELD is a `case`
     - FIELD(key, ...) forwards to serialise(s, __VA_ARGS__), so a fixed-size
       array binds the `T (&arr)[N]` template
     - FIELD_ARRAY(key, data, length) takes `length` by NON-CONST REFERENCE,
       because it is for arrays whose element count is a stored variable

   That last one matters: an earlier version of this file declared FIELD_ARRAY
   loosely enough to accept a compile-time constant, so eight real errors
   compiled clean here and failed on the server. The stub was the bug, exactly
   as the header comment warns. */

struct serialiser_t {
    bool reading;
    const char *active_field_key;

    bool serialise(char (&str)[17]);
    bool skip_ws(void);
    bool consume(char c);
    bool skip_value(void);
    bool read_errorf(const char *fmt, ...);
    void emit_object(bool compact);
    void emit_end_object(void);
    void emit_trailing_comma_and_key(const char *key);
};

extern volatile uint8_t is_serialise_in_progress;  /* 0 idle, 1 load, 2 save */

/* Scalar overloads, matching save_and_load.h's set. */
static inline bool serialise(serialiser_t &s, bool &v) { (void)s; (void)v; return true; }
static inline bool serialise(serialiser_t &s, float &v) { (void)s; (void)v; return true; }
static inline bool serialise(serialiser_t &s, int8_t &v) { (void)s; (void)v; return true; }
static inline bool serialise(serialiser_t &s, uint8_t &v) { (void)s; (void)v; return true; }
static inline bool serialise(serialiser_t &s, int16_t &v) { (void)s; (void)v; return true; }
static inline bool serialise(serialiser_t &s, uint16_t &v) { (void)s; (void)v; return true; }
static inline bool serialise(serialiser_t &s, int &v) { (void)s; (void)v; return true; }
static inline bool serialise(serialiser_t &s, uint32_t &v) { (void)s; (void)v; return true; }
static inline bool serialise(serialiser_t &s, int64_t &v) { (void)s; (void)v; return true; }

/* Fixed-size array: what FIELD("key", arr) binds to. */
template <typename T, int N> static inline bool serialise(serialiser_t &s, T (&arr)[N]) {
    for (int i = 0; i < N; ++i)
        if (!serialise(s, arr[i])) return false;
    return true;
}

/* Variable-length array: what FIELD_ARRAY binds to. `length` is a NON-CONST
   REFERENCE on purpose - passing a constant must not compile. */
template <typename T, int N, typename LT>
static inline bool serialise(serialiser_t &s, T (&arr)[N], LT &length, int inner_dimension_2d = 0) {
    (void)inner_dimension_2d;
    for (int i = 0; i < (int)length && i < N; ++i)
        if (!serialise(s, arr[i])) return false;
    return true;
}

template <int N> static inline bool serialise(serialiser_t &s, char (&str)[N]) {
    (void)s; (void)str; return true;
}

/* Compile-time string hash so each FIELD really is a distinct switch case, the
   way HASH(key) is in the firmware. */
static constexpr unsigned literal_hash(const char *p, unsigned h = 2166136261u) {
    return *p ? literal_hash(p + 1, (h ^ (unsigned)(unsigned char)*p) * 16777619u) : h;
}
#define HASH(key) literal_hash(key)

#define OBJECT_BEGIN(s)                                                                            \
    char keybuf[17];                                                                               \
    bool _plinky_object_first = true;                                                              \
    (void)keybuf;                                                                                  \
    (void)_plinky_object_first;                                                                    \
    while (!(s).reading || !(s).consume('}')) {                                                    \
        switch ((s).reading ? literal_hash(keybuf) : 1u) {                                         \
        default:                                                                                   \
            if (!(s).skip_value()) return false;                                                   \
            break;                                                                                 \
        case 1u:

#define OBJECT_END(s)                                                                              \
    }                                                                                              \
    if (!(s).reading) {                                                                            \
        (s).emit_end_object();                                                                     \
        break;                                                                                     \
    } else {                                                                                       \
        _plinky_object_first = false;                                                              \
    }                                                                                              \
    }

#define FIELD(key, ...)                                                                            \
    static_assert(sizeof(key) <= 16, "FIELD key must be shorter than 16 characters");              \
    case (HASH(key)):                                                                              \
        s.emit_trailing_comma_and_key(key);                                                        \
        s.active_field_key = key;                                                                  \
        if (!serialise(s, __VA_ARGS__)) return false;                                              \
        s.active_field_key = NULL;                                                                 \
        if (s.reading) break;

#define FIELD_ARRAY(key, data, length)                                                             \
    case (HASH(key)):                                                                              \
        s.emit_trailing_comma_and_key(key);                                                        \
        if (!serialise(s, data, length)) return false;                                             \
        if (s.reading) break;

/* --- concurrency --------------------------------------------------------- */
struct on_sequence_lock_guard_t {
    on_sequence_lock_guard_t();
    ~on_sequence_lock_guard_t();
};

/* --- panel_t ------------------------------------------------------------- */
struct panel_t {
    char song_name[17];   /* user-facing save/controller name (llm.txt:370) */
    virtual ~panel_t() {}
    virtual void setup_default_panel_state() {}
    virtual void on_load_finished(void) {}
    virtual int get_version(void) { return 1; }
    virtual int get_num_pages(void) { return 1; }
    virtual int get_num_panel_settings_pages(void) { return 0; }
    virtual void on_ui(int delta_time_us) { (void)delta_time_us; }
    virtual void on_sequence(int delta_time_us) { (void)delta_time_us; }
    virtual void on_click(uint8_t button_mask) { (void)button_mask; }
    virtual void on_touch(int x, int y, int down) { (void)x; (void)y; (void)down; }
    virtual void on_midi(uint32_t midimsg) { (void)midimsg; }
    virtual bool on_serialise(serialiser_t &s, int version) { (void)s; (void)version; return true; }
    virtual bool on_serialise_settings(serialiser_t &s, int version) { (void)s; (void)version; return true; }
    bool load_settings_from_sd(bool include_serial_number);
    bool save_settings_to_sd(bool include_serial_number);
};
