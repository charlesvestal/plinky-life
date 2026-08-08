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

struct clock_divider_t {
    int numerator, denominator;
    int64_t clock_base;
    uint16_t phase;
    int update(int64_t clock_phase = -1, int requested_numerator = -1, int requested_denominator = -1,
               divider_update_policy_t update_when = UPDATE_DIV_ON_HALF_NOTE,
               bool freerunning = false, bool unwrap_clock = DONT_UNWRAP_CLOCK);
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
                       uint8_t subset_end = DEFAULT_VOICE_ALLOCATOR_VOICES);
    int voice_deallocate(uint32_t source_id, uint8_t subset_start = 0, uint8_t subset_end = MAX_VOICES);
    int find_voice(uint32_t source_id, uint8_t subset_start = 0, uint8_t subset_end = MAX_VOICES) const;
};

void play_synth(int voice, int preset_idx, int velocity, int note_q8, bool retrigger);
void synth_note_up(int voice);

/* --- MIDI ---------------------------------------------------------------- */
#define MIDI_PORT_NONE 0
#define MIDI_PORT_USB1 16
#define MIDI_PORT_USB2 32
#define MIDI_PORT_TRS1 64
#define MIDI_PORT_TRS2 128
#define MIDI_PORT_1 (MIDI_PORT_USB1 | MIDI_PORT_TRS1)
#define MIDI_PORT_2 (MIDI_PORT_USB2 | MIDI_PORT_TRS2)

#define MIDIMSG(status, data1, data2)                                                              \
    ((uint32_t)(status) | ((uint32_t)(data1) << 8) | ((uint32_t)(data2) << 16))
#define MAKE_CCMSG(channel, cc, value) MIDIMSG(0xb0u | ((channel) & 0x0f), (cc), (value))
#define MAKE_NOTEONMSG(channel, note, velocity) MIDIMSG(0x90u | ((channel) & 0x0f), (note), (velocity))
#define MAKE_NOTEOFFMSG(channel, note, velocity) MIDIMSG(0x80u | ((channel) & 0x0f), (note), (velocity))

bool midi_write(uint8_t ports, uint32_t midimsg);
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int midi_write_cc(int ports, int channel, int cc, int val) {
    val = clampi(val, 0, 127);
    if (cc < 0 || cc > 127 || channel < 0 || channel > 15 || ports == 0) return val;
    midi_write((uint8_t)ports, MAKE_CCMSG(channel, cc, val));
    return val;
}
int get_system_midi_channel(void);
uint8_t get_midi_channel_for_preset_idx(int preset_idx, bool respect_system_midi_channel);

/* --- musical state ------------------------------------------------------- */
extern uint8_t current_key;
extern uint16_t current_scale;
void set_current_key_and_scale(uint8_t key, uint16_t scale);

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
};
