#pragma once
/* Metadata is shared across tracks; rendered voices remain ordinary notes. */
#define SEQ_CHORD_EVENT_CAPACITY 64
#define SEQ_CHORD_TYPE_COUNT 17
#define SEQ_CHORD_CUSTOM 12
#define SEQ_CHORD_NAME_SIZE 12
#define SEQ_CHORD_NONE UINT16_MAX
typedef struct {
    uint16_t start, length;
    uint8_t  track, root, type, velocity, scale_root, scale_kind, inversion, quantize, quantize_root, quantize_kind, custom_count, scale_free;
    int8_t   intervals[8];
    char     name[SEQ_CHORD_NAME_SIZE];
} seq_chord_event_t;
static seq_chord_event_t seq_chord_events[SEQ_CHORD_EVENT_CAPACITY];
static seq_chord_event_t seq_chord_draft[12];
static bool              seq_chord_mode[12];
static uint16_t          seq_chord_selected = SEQ_CHORD_NONE;
static uint16_t          seq_chord_held, seq_chord_note_held, seq_chord_shortcut_keys;
static uint8_t           seq_chord_parameter, seq_chord_error, seq_chord_edit_track;
static bool              seq_chord_delete, seq_chord_mutating;
static void              seq_chord_cancel(void);
static void              seq_chord_prune(void);
static void              seq_chord_mode_set(uint8_t track, bool enabled);
static bool              seq_chord_key(uint16_t key, bool pressed, uint32_t now);
static bool              seq_chord_encoder(int8_t delta);
static void              seq_chord_request(uint8_t track, uint16_t offset, const uint8_t *data, uint8_t size);
