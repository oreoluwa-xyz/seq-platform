/* Read-only command 25. Count through the project codec without touching the
 * shared transfer/TEMP buffer or pruning authored state. */
#define SEQ_PROJECT_HEALTH_SIZE 42

static bool seq_temp_gesture_ready(void) {
    return seq_state.active && seq_live_can_select() && !record_state.armed && !cc_session.active && !cc_mode.held && !cc_mode.latched_temporary && !seq_track_confirmation_active();
}

static void seq_project_health(uint8_t track, uint8_t *out) {
    memset(out, 0, SEQ_PROJECT_HEALTH_SIZE);
    uint16_t saved_lanes = seq_codec_lanes, saved_cc = seq_codec_cc, saved_chords = seq_codec_chords;
    uint16_t bytes = 26, chords = 0, selected_bytes = 0;
    bool known = true;
    for (uint8_t object = 0; object <= SEQ_TRACK_COUNT; object++) {
        uint16_t size = UINT16_MAX;
        if (!seq_project_object(object, NULL, &size, false, false)) {
            known = false;
            break;
        }
        bytes += size;
        chords += seq_codec_chords;
        if (object == track + 1) selected_bytes = size;
    }
    seq_codec_lanes = saved_lanes;
    seq_codec_cc = saved_cc;
    seq_codec_chords = saved_chords;
    uint16_t notes = 0, mods = 0;
    for (uint8_t t = 0; t < SEQ_TRACK_COUNT; t++) notes += mel_track_event_count(t);
    uint8_t lane = drum_active_note[track];
    const seq_drum_lane_storage_t *storage = seq_drum_pool_lane(track, lane);
    if (storage) for (uint8_t i = 0; i < SEQ_DRUM_STEP_MOD_CAP; i++) mods += storage->mods[i].step != UINT16_MAX;
    uint16_t temp_bytes = known ? bytes + sizeof(seq_temp_context_t) : 0;
    out[0] = 1;
    out[1] = (seq_dirty ? 1 : 0) | (seq_temp_active ? 2 : 0) | (seq_temp_restore_pending ? 4 : 0) | (seq_transfer_active ? 8 : 0) |
             (known ? 16 : 0) | (known && !seq_temp_active && !seq_pending_erase && temp_bytes <= SEQ_TEMP_CAPACITY && seq_temp_gesture_ready() ? 32 : 0) |
             (seq_saved_valid ? 64 : 0);
    out[2] = seq_save_status;
    out[3] = track;
    out[4] = lane;
    out[5] = seq_drum_pool_used();
    out[6] = SEQ_DRUM_LANE_POOL_CAPACITY;
    out[7] = MEL_TRACK_MAX_EVENTS;
    seq_live_u16(out + 8, mel_track_event_count(track));
    seq_live_u16(out + 10, notes);
    seq_live_u16(out + 12, known ? chords : 0);
    seq_live_u16(out + 14, SEQ_CHORD_EVENT_CAPACITY);
    seq_live_u16(out + 16, seq_automation_count());
    seq_live_u16(out + 18, SEQ_CC_EVENT_CAPACITY);
    seq_live_u16(out + 20, seq_automation_track_count(track));
    seq_live_u16(out + 22, SEQ_CC_TRACK_EVENT_CAPACITY);
    seq_live_u16(out + 24, mods);
    seq_live_u16(out + 26, SEQ_DRUM_STEP_MOD_CAP);
    seq_live_u16(out + 28, known ? bytes + 10 : 0);
    seq_live_u16(out + 30, EECONFIG_USER_DATA_SIZE);
    seq_live_u16(out + 32, temp_bytes);
    seq_live_u16(out + 34, SEQ_TEMP_CAPACITY);
    seq_live_u16(out + 36, seq_temp_active ? seq_temp_length : 0);
    seq_live_u16(out + 38, known ? selected_bytes : 0);
    seq_live_u16(out + 40, SEQ_TRANSFER_CAPACITY);
}
