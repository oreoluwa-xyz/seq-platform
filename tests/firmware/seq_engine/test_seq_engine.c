#include <assert.h>
#include <stdio.h>
#include "../../config.h"
#define SEQ_ENGINE_TEST 1
#include "../../sequencer.c"

static uint8_t  test_eeprom[EECONFIG_USER_DATA_SIZE];
static bool     test_eeprom_valid;
static unsigned test_eeprom_writes;
bool            eeconfig_is_user_datablock_valid(void) {
    return test_eeprom_valid;
}
void eeconfig_read_user_datablock(void *data) {
    memcpy(data, test_eeprom, sizeof(test_eeprom));
}
void eeconfig_update_user_datablock(const void *data) {
    __wrap_eeprom_update_block(data, (void *)0, sizeof(test_eeprom));
    test_eeprom_valid = true;
}
void eeprom_read_block(void *buf, const void *addr, size_t len) {
    assert(len <= 64); /* Save comparison must never use a project-sized stack buffer. */
    assert((uintptr_t)addr + len <= sizeof(test_eeprom));
    memcpy(buf, test_eeprom + (uintptr_t)addr, len);
}
void eeprom_write_block(const void *buf, void *addr, size_t len) {
    assert((uintptr_t)addr + len <= sizeof(test_eeprom));
    memcpy(test_eeprom + (uintptr_t)addr, buf, len);
    test_eeprom_writes++;
}
static bool    protocol_bridge;
static uint8_t backup_status, backup_data[64], backup_size;
void           virtual_midi_backup_reply(uint8_t command, uint8_t object, uint16_t offset, uint8_t status, const uint8_t *data, uint8_t size) {
    if (protocol_bridge) {
        uint8_t prefix[] = {0xF0, 0x7D, 'S', 'E', 'Q', 1, (uint8_t)(command | 64), object, (uint8_t)(offset & 127), (uint8_t)(offset >> 7), status};
        for (unsigned i = 0; i < sizeof(prefix); i++)
            printf("%02x", prefix[i]);
        for (uint8_t i = 0; i < size; i++)
            printf("%02x%02x", data[i] & 15, data[i] >> 4);
        puts("f7");
        fflush(stdout);
    }
    backup_status = status;
    backup_size   = size;
    memcpy(backup_data, data, size);
}
static uint32_t        test_now = 1000;
static uint8_t         test_mods;
static uint16_t        host_tap;
const uint16_t PROGMEM keymaps[1][MATRIX_ROWS][MATRIX_COLS] = {{{0}}};
uint32_t               timer_read32(void) {
    return test_now;
}
uint8_t get_mods(void) {
    return test_mods;
}
void tap_code16(uint16_t keycode) {
    host_tap = keycode;
}
static bool key(uint16_t code, bool pressed) {
    keyrecord_t record = {.event.pressed = pressed};
    return seq_pre_process(code, &record);
}
static void reset(void) {
    test_now += 1000;
    test_eeprom_valid = false;
    seq_mode_on_load();
    seq_state.active  = false;
    seq_state.running = false;
    bpm               = 120;
    init_done         = true;
    host_tap          = 0;
    test_mods         = 0;
}
static void test_inactive_keys(void) {
    reset();
    assert(key(KC_SPC, true));
    assert(key(KC_SPC, false));
    assert(key(KC_PGDN, true));
    assert(key(KC_PGDN, false));
    assert(host_tap == 0);
}

static unsigned events_of(virtual_midi_event_kind_t kind) {
    const virtual_midi_state_t *state = virtual_midi_state();
    unsigned                    count = 0;
    for (uint8_t i = 0; i < state->event_count; i++)
        count += state->events[i].kind == kind;
    return count;
}
static void test_capacity_and_duplication(void) {
    reset();
    seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    for (uint8_t i = 0; i < 16; i++)
        assert(mel_track_add_note(0, i, i, 60, 100));
    seq_track_double_length(0, true);
    assert(track_length_steps[0] == 32 && mel_track_event_count(0) == 32);
    for (uint16_t i = 32; i < MEL_TRACK_MAX_EVENTS; i++)
        assert(mel_track_add_note(0, i, i, 61, 100));
    mel_event_snapshot_t first, after;
    mel_track_event_at(0, 0, &first);
    assert(!mel_track_add_note(0, 180, 180, 60, 100));
    mel_track_event_at(0, 0, &after);
    assert(!memcmp(&first, &after, sizeof(first)));
    seq_track_double_length(0, true);
    assert(track_length_steps[0] == 32 && mel_track_event_count(0) == MEL_TRACK_MAX_EVENTS);
    mel_track_edit_begin(0);
    mel_track_remove_event(0, 0);
    assert(mel_track_add_note(0, 140, 140, 60, 100));
    assert(!mel_track_add_note(0, 141, 141, 60, 100));
    assert(!mel_track_edit_end());
    mel_track_event_at(0, 0, &after);
    assert(!memcmp(&first, &after, sizeof(first)));
}
static void test_playback_and_polymeter(void) {
    reset();
    seq_enter(test_now);
    track_length_steps[0] = 3;
    track_length_steps[1] = 5;
    seq_slots[1].kind     = SEQ_SLOT_KIND_MELODIC;
    seq_drum_step_set(0, 0, 0, true);
    for (uint8_t i = 0; i < 32; i++)
        assert(mel_track_add_note(1, 0, 0, i + 40, 100));
    virtual_midi_clear_events();
    seq_transport_start(test_now, 0);
    assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == 33);
    virtual_midi_clear_events();
    test_now += 125;
    seq_matrix_scan();
    assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_OFF) == 33);
    for (uint8_t i = 1; i < 15; i++) {
        test_now += 125;
        seq_matrix_scan();
    }
    assert(seq_track_step_position[0] == 0 && seq_track_step_position[1] == 0);
    assert(seq_step_position == 15);
    seq_transport_stop(test_now);
    seq_transport_start(test_now, 0);
    assert(seq_track_step_position[0] == 0 && seq_track_step_position[1] == 0);
}
static void test_timing_and_external(void) {
    reset();
    seq_enter(test_now);
    bpm              = 137;
    seq_timing_swing = 25;
    uint32_t start   = test_now;
    seq_transport_start(test_now, 0);
    for (uint32_t elapsed = 1; elapsed <= 60000; elapsed++) {
        test_now = start + elapsed;
        seq_matrix_scan();
    }
    assert(seq_step_position == 548);
    assert(virtual_midi_state()->clock_tick_count == 3289); /* Includes pulse at time zero. */
    seq_transport_stop(test_now);
    seq_external_clock = true;
    seq_timing_swing = 0; /* External swing is covered by the timing suite. */
    seq_midi_realtime(0xFA, test_now);
    assert(seq_first_step);
    seq_midi_realtime(0xF8, test_now);
    seq_matrix_scan();
    assert(!seq_first_step && seq_track_step_position[0] == 0);
    for (uint8_t i = 0; i < 6; i++) {
        test_now += 20;
        seq_midi_realtime(0xF8, test_now);
        seq_matrix_scan();
    }
    assert(seq_track_step_position[0] == 1);
    seq_midi_realtime(0xFC, test_now);
    assert(!seq_state.running);
    seq_midi_song_position(7);
    seq_midi_realtime(0xFB, test_now);
    seq_midi_realtime(0xF8, test_now);
    seq_matrix_scan();
    assert(seq_track_step_position[0] == 7);
    test_now += 2001;
    seq_matrix_scan();
    assert(!seq_state.running);
}
static void test_harmony_capture(void) {
    reset();seq_enter(test_now);seq_slots[0].kind=SEQ_SLOT_KIND_MELODIC;
    euclid_tracks[0].enabled=true;euclid_tracks[0].steps=16;euclid_tracks[0].fills=4;euclid_tracks[0].rotate=0;
    euclid_tracks[0].pitch_mode=0;euclid_tracks[0].note_count=2;euclid_tracks[0].note_pool[0]=0;euclid_tracks[0].note_pool[1]=7;euclid_tracks[0].palette_pin=255;
    assert(mel_track_add_note(0,4,4,65,100));
    uint8_t source[SEQ_TRANSFER_CAPACITY];uint16_t size=sizeof(source);
    assert(seq_project_object(25,source,&size,false,false));assert(size==40 && source[0]==1);
    uint8_t base=seq_melodic_note_pitch(0,euclid_tracks[0].palette_root);
    assert(source[8]==base && source[9]==1 && source[10]==255 && source[16]==65 && source[32]==base+7);
    seq_backup_request(1,25,0,NULL,0);assert(!backup_status);uint8_t frozen=seq_transfer_buffer[32];
    euclid_tracks[0].note_pool[1]=12;
    seq_backup_request(1,25,32,NULL,0);assert(!backup_status && seq_transfer_buffer[32]==frozen);
    seq_backup_request(1,0,0,NULL,0);seq_backup_request(1,25,32,NULL,0);assert(backup_status==3);
    seq_backup_request(7,0,0,NULL,0);
    track_length_steps[0]=2048;size=sizeof(source);assert(seq_project_object(25,source,&size,false,false)&&size==4104);
    assert(!seq_project_restore_object(25,source,size));
}
static void test_cc_live_snapshot(void) {
    reset();seq_enter(test_now);test_now+=500;
    key(KC_BSLS,true);key(KC_BSLS,false);
    seq_backup_request(21,0,0,NULL,0);assert(!backup_status);
    assert(seq_visual_image[0]==1 && (seq_visual_image[1]&3)==3 && seq_visual_image[4]==16);
    assert(seq_visual_image[3]==255 && seq_visual_image[64]==(KC_1&255) && seq_visual_image[66]==16 && seq_visual_image[68]==64);
    key(KC_1,true);seq_cc_adjust_value(1,test_now);
    seq_backup_request(21,0,0,NULL,0);assert(seq_visual_image[3]==0 && seq_visual_image[68]==67 && seq_visual_image[69]==1);
    uint8_t token=seq_visual_image[62];seq_cc_adjust_value(1,test_now);
    seq_backup_request(21,token,64,NULL,0);assert(!backup_status && seq_visual_image[68]==67 && cc_values[0][0]==70);
    key(KC_1,false);
    cc_context.hold_override_active=true;cc_context.hold_track=3;
    seq_cc_start_session(3,1,true,test_now);seq_cc_adjust_value(1,test_now);
    seq_backup_request(21,0,0,NULL,0);
    assert(seq_visual_image[2]==3 && seq_visual_image[3]==1 && (seq_visual_image[1]&20)==20 && seq_visual_image[74]==67);
    assert(seq_visual_image[73]==midi_channel_for_track(3));
    seq_cc_end_session(true,test_now);seq_backup_request(21,0,0,NULL,0);assert(seq_visual_image[74]==64);
    cc_context.hold_override_active=false;key(KC_BSLS,true);key(KC_BSLS,false);
    seq_backup_request(21,0,0,NULL,0);assert(!(seq_visual_image[1]&3));
    seq_backup_request(21,0,192,NULL,0);assert(backup_status==3);
}
static void test_cc_manual_mode(void) {
    reset();seq_enter(test_now);test_now+=500;
    key(KC_BSLS,true);key(KC_BSLS,false);
    seq_state.running=true;
    seq_track_step_position[0]=2;
    seq_cc_set_value(0,0,70,test_now);
    assert(seq_automation_track_count(0)==1);
    key(KC_T,true);key(KC_T,false);
    assert(cc_automation_manual[0] && !cc_automation_manual[1]);
    seq_backup_request(21,0,0,NULL,0);
    assert(seq_visual_image[160]==1 && seq_visual_image[161]==1);
    virtual_midi_clear_events();
    seq_cc_set_value(0,0,95,test_now);
    assert(events_of(VIRTUAL_MIDI_EVENT_CC)==1 && cc_values[0][0]==95);
    assert(seq_automation_count()==1 && seq_automation_event(0)->value==70);
    seq_cc_automation_apply(0,2,test_now);
    assert(cc_values[0][0]==95);
    assert(seq_automation_record(1,0,2,45));
    seq_cc_automation_apply(1,2,test_now);
    assert(cc_values[1][0]==45);
    key(KC_BSLS,true);key(KC_BSLS,false);
    assert(cc_automation_manual[0]);
    seq_cc_automation_apply(0,2,test_now);assert(cc_values[0][0]==95);
    uint8_t saved[SEQ_TRANSFER_CAPACITY];uint16_t size=sizeof(saved);
    assert(seq_project_object(1,saved,&size,false,false));
    cc_automation_manual[0]=false;
    assert(seq_project_restore_object(1,saved,size) && cc_automation_manual[0]);
    key(KC_BSLS,true);key(KC_BSLS,false);
    key(KC_T,true);key(KC_T,false);
    assert(!cc_automation_manual[0]);
    seq_cc_automation_apply(0,2,test_now);assert(cc_values[0][0]==70);
    seq_state.running=true;seq_track_step_position[0]=3;seq_cc_set_value(0,0,80,test_now);
    assert(seq_automation_track_count(0)==2);
    /* Legacy/default objects omit the extension and restore Automation. */
    size=sizeof(saved);assert(seq_project_object(1,saved,&size,false,false));
    cc_automation_manual[0]=true;
    assert(seq_project_restore_object(1,saved,size) && !cc_automation_manual[0]);
    cc_context.hold_override_active=true;cc_context.hold_track=3;
    key(KC_T,true);key(KC_T,false);
    assert(cc_automation_manual[3] && !cc_automation_manual[0]);
}
static void test_cc_shift_pgdn(void) {
    reset();seq_enter(test_now);test_now+=500;
    key(KC_BSLS,true);key(KC_BSLS,false);
    key(KC_PGDN,true);key(KC_PGDN,false);
    assert(!record_state.armed);
    key(KC_LSFT,true);key(KC_PGDN,true);
    key(KC_LSFT,false);key(KC_PGDN,false);
    assert(cc_automation_manual[0] && !record_state.armed);
    seq_backup_request(21,0,0,NULL,0);assert(seq_visual_image[162]&1);
    /* The modified gesture must break the ordinary double-tap sequence. */
    test_now+=20;key(KC_PGDN,true);key(KC_PGDN,false);
    assert(!record_state.armed);
    test_now+=20;key(KC_PGDN,true);key(KC_PGDN,false);
    assert(record_state.armed && cc_automation_manual[0]);
    key(KC_RSFT,true);key(KC_PGDN,true);key(KC_PGDN,false);key(KC_RSFT,false);
    assert(record_state.armed && !cc_automation_manual[0]);
    cc_context.hold_override_active=true;cc_context.hold_track=4;
    key(KC_LSFT,true);key(KC_PGDN,true);key(KC_PGDN,false);key(KC_LSFT,false);
    assert(cc_automation_manual[4] && !cc_automation_manual[0] && record_state.armed);
}
static void test_cc_and_record(void) {
    reset();
    seq_enter(test_now);
    track_length_steps[0] = 512;
    assert(seq_automation_record(0, 0, 1, 10));
    assert(seq_automation_record(0, 0, 300, 90));
    assert(seq_automation_record(0, 0, 300, 91));
    assert(seq_automation_count() == 2);
    seq_cc_automation_apply(0, 1, test_now);
    assert(cc_values[0][0] == 10);
    seq_cc_automation_apply(0, 300, test_now);
    assert(cc_values[0][0] == 91);
    seq_slots[0].kind     = SEQ_SLOT_KIND_MELODIC;
    track_length_steps[0] = 5;
    seq_transport_start(test_now, 0);
    seq_step_position          = 100;
    seq_track_step_position[0] = 4;
    record_state.armed         = true;
    seq_record_try_begin(0, true, 1, 100);
    seq_record_flush(102);
    assert(track_length_steps[0] == 5);
    assert(mel_track_event_count(0) == 2);
    mel_event_snapshot_t ev;
    mel_track_event_at(0, 0, &ev);
    assert(ev.start_step == 0 && ev.end_step == 1);
    mel_track_event_at(0, 1, &ev);
    assert(ev.start_step == 4 && ev.end_step == 4);
}
static void test_storage(void) {
    reset();
    bpm                = 137;
    seq_external_clock = true;
    seq_timing_swing   = 22;
    seq_drum_step_set(0, 2, 9, true);
    seq_drum_step_mod_t *mod = seq_drum_step_mod_ensure(0, 2, 9);
    mod->config              = 7;
    drum_lane_muted[0][2]    = true;
    seq_slots[1].kind        = SEQ_SLOT_KIND_MELODIC;
    for (uint8_t i = 0; i < 40; i++)
        mel_track_add_note(1, i * 2, i * 2 + 1, 40 + i, 80);
    seq_automation_record(1, 1, 300, 92);
    seq_mark_dirty();
    seq_storage_save_if_dirty();
    assert(seq_save_status == 0 && !seq_dirty);
    seq_drum_pool_init();
    mel_tracks_init();
    seq_automation_init();
    bpm = 120;
    seq_storage_load();
    assert(bpm == 137 && seq_external_clock && seq_timing_swing == 22);
    assert(seq_drum_step_get(0, 2, 9) && drum_lane_muted[0][2]);
    assert(seq_drum_step_mod_lookup(0, 2, 9)->config == 7);
    assert(mel_track_event_count(1) == 40 && seq_automation_count() == 1);
    mel_event_snapshot_t unordered[2] = {{.pitch = 60, .velocity = 100, .start_step = 12, .end_step = 14}, {.pitch = 64, .velocity = 90, .start_step = 2, .end_step = 4}}, restored_notes[2];
    assert(mel_track_restore(0, unordered, 2));
    assert(mel_track_snapshot(0, restored_notes, 2) == 2);
    assert(!memcmp(unordered, restored_notes, sizeof(unordered)));
    unsigned writes = test_eeprom_writes;
    for (uint8_t t = 2; t < 12; t++)
        for (uint8_t i = 0; i < MEL_TRACK_MAX_EVENTS; i++)
            mel_track_add_note(t, i * 2, i * 2 + 1, 60, 100);
    seq_storage_save_if_dirty();
    assert(seq_save_status == 2 && seq_dirty && test_eeprom_writes == writes);
    uint8_t  object[SEQ_TRANSFER_CAPACITY], roundtrip[SEQ_TRANSFER_CAPACITY];
    uint16_t size = sizeof(object);
    assert(seq_project_object(3, object, &size, false, false));
    mel_track_reset(2);
    assert(seq_project_restore_object(3, object, size));
    assert(mel_track_event_count(2) == MEL_TRACK_MAX_EVENTS);
    uint16_t second_size = sizeof(roundtrip);
    assert(seq_project_object(3, roundtrip, &second_size, false, false));
    assert(size == second_size && !memcmp(object, roundtrip, size));
    object[0] = object[1] = 255;
    assert(!seq_project_restore_object(3, object, size));
    assert(mel_track_event_count(2) == MEL_TRACK_MAX_EVENTS);
    test_eeprom[60] ^= 1;
    seq_storage_load();
    assert(seq_save_status == 3);
    assert(mel_track_event_count(2) == MEL_TRACK_MAX_EVENTS);
}
static void test_gestures(void) {
    reset();
    seq_enter(test_now);
    assert(!key(KC_SPC, true));
    assert(!key(KC_SPC, false));
    assert(host_tap == 0);
    seq_slots[0].kind    = SEQ_SLOT_KIND_MELODIC;
    melodic_last_mask[0] = 1;
    seq_handle_melodic_step(0, 0, 0, 16, test_now);
    seq_flush_pending_erase();
    assert(mel_track_event_count(0) == 0);
    melodic_key_mask[0] = 1;
    seq_handle_melodic_step(0, 0, 0, 16, test_now);
    assert(mel_track_event_count(0) == 1);
    test_now += 400;
    seq_handle_melodic_step(0, 0, 0, 16, test_now);
    assert(mel_track_event_count(0) == 0);
    melodic_key_mask[0] = 0;
    mel_track_add_note(0, 0, 0, 60, 100);
    test_now += 400;
    key(seq_step_key(0), true);
    key(seq_step_key(0), false);
    test_now += 100;
    key(seq_step_key(0), true);
    assert(step_edit.active && mel_track_event_count(0) == 1);
    seq_step_edit_reset();
    seq_apply_layout(SEQ_LAYOUT_GRID_4X4);
    assert(seq_step_key(0) == SEQ_STEP_KEYS_DEFAULT[0]);
    euclid_mode_active = true;
    assert(seq_handle_escape(test_now));
    assert(!euclid_mode_active);
}
static void test_mod_duplication_and_mutes(void) {
    reset();
    seq_enter(test_now);
    track_length_steps[0] = 5;
    seq_drum_step_set(0, 1, 2, true);
    seq_drum_step_mod_ensure(0, 1, 2)->config = 9;
    seq_automation_record(0, 0, 2, 91);
    seq_track_double_length(0, true);
    assert(track_length_steps[0] == 10 && seq_drum_step_get(0, 1, 7));
    assert(seq_drum_step_mod_lookup(0, 1, 7)->config == 9 && seq_automation_count() == 2);
    drum_lane_muted[0][1] = true;
    seq_exit();
    assert(drum_lane_muted[0][1] && seq_drum_step_mod_lookup(0, 1, 7)->config == 9);
}

static void test_backup_protocol(void) {
    reset();
    for (uint8_t n = 0; n < 12; n++) {
        seq_drum_pool_reserve(0, 1u << n);
        seq_drum_lane_storage_t *lane = seq_drum_pool_lane(0, n);
        memset(lane->data, 0xAA, sizeof(lane->data));
        for (uint8_t m = 0; m < SEQ_DRUM_STEP_MOD_CAP; m++)
            lane->mods[m] = (seq_drum_step_mod_t){.step = m * 2 + 1, .config = 7, .spacing = 15};
    }
    for (uint8_t n = 0; n < MEL_TRACK_MAX_EVENTS; n++)
        mel_track_add_note(0, n, n + 1, 60, 100);
    for (uint16_t n = 0; n < SEQ_CC_TRACK_EVENT_CAPACITY; n++)
        seq_automation_record(0, 0, n, n & 127);
    uint8_t  object[SEQ_TRANSFER_CAPACITY];
    uint16_t length = 0;
    for (uint16_t offset = 0; !length || offset < length; offset += 32) {
        seq_backup_request(1, 1, offset, NULL, 0);
        assert(!backup_status && backup_size >= 2);
        length = backup_data[0] | ((uint16_t)backup_data[1] << 8);
        assert(length <= sizeof(object));
        memcpy(object + offset, backup_data + 2, backup_size - 2);
    }
    assert(length > EECONFIG_USER_DATA_SIZE);
    seq_backup_request(2, 2, length, NULL, 0);
    assert(!backup_status);
    seq_backup_request(3, 2, 1, object, 32);
    assert(backup_status);
    for (uint16_t offset = 0; offset < length; offset += 32) {
        uint8_t size = length - offset > 32 ? 32 : length - offset;
        seq_backup_request(3, 2, offset, object + offset, size);
        assert(!backup_status);
    }
    uint32_t hash = seq_project_checksum(object, length);
    uint8_t  check[4];
    for (uint8_t i = 0; i < 4; i++)
        check[i] = hash >> (i * 8);
    seq_backup_request(4, 2, 0, check, 4);
    assert(!backup_status);
    assert(seq_drum_pool_used() == 24 && mel_track_event_count(1) == MEL_TRACK_MAX_EVENTS && seq_automation_count() == 256);
    uint8_t  restored[SEQ_TRANSFER_CAPACITY];
    uint16_t size = sizeof(restored);
    assert(seq_project_object(2, restored, &size, false, false));
    assert(size == length && !memcmp(object, restored, size));
    for (uint16_t truncated = 0; truncated < length; truncated += 101)
        assert(!seq_project_restore_object(2, object, truncated));
    seq_backup_request(7, 0, 0, NULL, 0);
    assert(!seq_transfer_active);
    /* Exercise QMK's 3-byte SysEx fragment boundaries through the real parser. */
    uint8_t packet[] = {0xF0, 0x7D, 'S', 'E', 'Q', 1, 9, 0, 0, 0, 0xF7};
    for (uint8_t start = 0; start < sizeof(packet); start += 3)
        virtual_midi_receive_sysex(start, sizeof(packet) - start > 3 ? 3 : sizeof(packet) - start, packet + start);
    assert(seq_transfer_active && !backup_status && backup_size == 6 && backup_data[2] == 24);
    seq_transfer_deadline = test_now;
    seq_housekeeping();
    assert(!seq_transfer_active);
}
static void test_euclid_and_capacity(void) {
    reset();
    seq_enter(test_now);
    seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    seq_euclid_enable(0, true);
    euclid_tracks[0].probability     = 100;
    euclid_tracks[0].template_active = true;
    euclid_tracks[0].template_mask   = (1u << 0) | (1u << 2) | (1u << 3);
    euclid_tracks[0].fills           = 3;
    for (uint8_t step = 0; step < 16; step++)
        assert(seq_euclid_step_hit(0, step, NULL, NULL) == (step == 0 || step == 2 || step == 3));
    virtual_midi_clear_events();
    seq_transport_start(test_now, 0);
    assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == 1);
    seq_euclid_adjust_fills(0, 1);
    assert(!euclid_tracks[0].template_active);
    seq_automation_init();
    for (uint8_t t = 0; t < 4; t++)
        for (uint16_t i = 0; i < 128; i++)
            assert(seq_automation_record(t, 0, i, 100));
    assert(!seq_automation_record(4, 0, 1, 10));
    assert(seq_automation_record(0, 0, 1, 10));
    assert(seq_automation_count() == SEQ_CC_EVENT_CAPACITY);
}

static void test_melodic_euclid_retriggers(void) {
    reset();
    seq_enter(test_now);
    seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    for (uint8_t pitch = 60; pitch <= 67; pitch += pitch == 60 ? 4 : 3)
        assert(mel_track_add_note(0, 0, 15, pitch, 100));
    seq_euclid_enable(0, true);
    euclid_tracks[0].steps = 16;
    euclid_tracks[0].fills = 4;
    euclid_tracks[0].rotate = 0;
    euclid_tracks[0].probability = 100;
    seq_transport_start(test_now, 0);
    for (uint8_t step = 1; step < 16; step++) {
        virtual_midi_clear_events();
        test_now += 125;
        seq_play_step(test_now);
        assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == (step % 4 == 0 ? 3 : 0));
        assert(melodic_active_any(0) == (step % 4 == 0));
    }
    assert(seq_slot_playback_mask(0, 0) == 0x1111);
    assert(slot_step_mask(0, 0) == 0xffff); /* Capturing/editing retains the source notes. */
    seq_backup_request(11, 0, 0, NULL, 0);
    assert((backup_data[56] | backup_data[57] << 8) == 0x1111);
    /* Fills/rotation changes apply to sustained notes, without re-entering them. */
    seq_transport_stop(test_now);
    euclid_tracks[0].fills = 8;
    euclid_tracks[0].rotate = 1;
    seq_transport_start(test_now, 0);
    for (uint8_t step = 1; step < 16; step++) {
        virtual_midi_clear_events();
        test_now += 125;
        if (step == 7) seq_euclid_adjust_fills(0, -3);
        seq_play_step(test_now);
        assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == (seq_euclid_step_hit(0, step, NULL, NULL) ? 3 : 0));
    }
    /* Probability is shared by every voice of a chord, not rolled per voice. */
    euclid_tracks[0].fills = 16;
    euclid_tracks[0].probability = 50;
    for (uint8_t step = 0; step < 32; step++) {
        virtual_midi_clear_events();
        seq_play_step(test_now += 125);
        unsigned notes = events_of(VIRTUAL_MIDI_EVENT_NOTE_ON);
        assert(notes == 0 || notes == 3);
    }
    euclid_tracks[0].probability = 0;
    virtual_midi_clear_events();
    seq_play_step(test_now += 125);
    assert(!melodic_active_any(0) && !events_of(VIRTUAL_MIDI_EVENT_NOTE_ON));
    /* Disabling Euclid restores ordinary sustained-note playback. */
    seq_euclid_enable(0, false);
    seq_transport_start(test_now, 0);
    virtual_midi_clear_events();
    seq_play_step(test_now += 125);
    assert(melodic_active_any(0) && !events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) && !events_of(VIRTUAL_MIDI_EVENT_NOTE_OFF));
    /* Generated hits fill empty steps even when other steps have recorded notes. */
    seq_transport_stop(test_now);
    mel_track_reset(0);
    mel_track_add_note(0, 4, 6, 60, 100);
    seq_euclid_enable(0, true);
    euclid_tracks[0].fills = 4;
    euclid_tracks[0].rotate = 0;
    euclid_tracks[0].probability = 100;
    assert(seq_slot_playback_mask(0, 0) == 0x1111);
    seq_transport_start(test_now, 0);
    for (uint8_t step = 1; step < 16; step++) {
        virtual_midi_clear_events();
        seq_play_step(test_now += 125);
        assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == (step % 4 == 0 ? 1 : 0));
    }
    mel_track_reset(0);
    assert(seq_slot_playback_mask(0, 0) == 0x1111);
    assert(seq_slot_playback_mask(0, 1) == 0);
}
static void test_modules(void) {
    reset(); seq_enter(test_now);
    seq_module_t module, decoded = {0};
    uint8_t bytes[SEQ_MODULE_WIRE_MAX]; uint16_t size = sizeof(bytes);
    seq_module_default(&module, SEQ_MODULE_EUCLID);
    assert(seq_module_encode(&module, bytes, &size));
    assert(seq_module_decode(&decoded, bytes, size));
    for (uint16_t i = 0; i < size; i++) assert(!seq_module_decode(&decoded, bytes, i));
    bytes[2] = SEQ_MODULE_CHORD_BANK;
    assert(!seq_module_decode(&decoded, bytes, size));
    seq_module_default(&module, SEQ_MODULE_CHORD_BANK);
    assert(!seq_module_valid(&module));
    assert(sizeof(seq_module_t) == 50);
}
static void test_live_state(void) {
    reset();
    seq_enter(test_now);
    seq_drum_step_set(0, 0, 0, true);
    seq_transport_start(test_now, 0);
    uint32_t position = seq_step_position;
    seq_backup_request(11, 0, 0, NULL, 0);
    assert(backup_status == 0 && backup_size == SEQ_LIVE_SIZE);
    assert(backup_data[0] == 1 && (backup_data[1] & 3) == 3);
    assert(backup_data[2] == 0 && backup_data[3] == 0);
    assert(backup_data[24 + 36 / 8] & (1u << (36 % 8)));
    assert(backup_data[40 + 36 / 8] & (1u << (36 % 8)));
    assert(seq_state.running && !seq_transfer_active && seq_step_position == position);
    seq_backup_request(12, 3, 0, NULL, 0);
    assert(!backup_status && view_slot == 3 && seq_state.running);
    note_hold_mask = 1;
    seq_backup_request(12, 2, 0, NULL, 0);
    assert(backup_status == 2 && view_slot == 3);
    note_hold_mask = 0;
    seq_backup_request(12, 12, 0, NULL, 0);
    assert(backup_status == 3 && view_slot == 3);
    seq_transfer_active = true;
    seq_backup_request(12, 2, 0, NULL, 0);
    assert(backup_status == 2 && view_slot == 3);
    seq_backup_request(11, 0, 0, NULL, 0);
    assert(!backup_status && (backup_data[1] & 16));
    seq_transfer_active = false;
    seq_transport_stop(test_now);
    seq_slots[3].kind = SEQ_SLOT_KIND_MELODIC;
    seq_slots[3].wraps_drum = true;

}
static void test_bounded_save(void) {
    uint8_t data[EECONFIG_USER_DATA_SIZE];
    memset(test_eeprom, 0, sizeof(test_eeprom));
    memset(data, 0, sizeof(data));
    unsigned before = test_eeprom_writes;
    __wrap_eeprom_update_block(data, (void *)0, sizeof(data));
    assert(test_eeprom_writes == before);
    data[sizeof(data) - 1] = 73;
    __wrap_eeprom_update_block(data, (void *)0, sizeof(data));
    assert(test_eeprom_writes == before + 1 && !memcmp(data, test_eeprom, sizeof(data)));
    __wrap_eeprom_update_block(data, (void *)0, sizeof(data));
    assert(test_eeprom_writes == before + 1);
    data[64] = 22;
    __wrap_eeprom_update_block(data, (void *)11, 65);
    assert(!memcmp(data, test_eeprom + 11, 65));
    before = test_eeprom_writes;
    __wrap_eeprom_update_block(NULL, (void *)0, 0);
    assert(test_eeprom_writes == before);
}
static void test_transport_restart(void) {
    reset();
    seq_enter(test_now);
    seq_drum_step_set(0, 0, 0, true);
    seq_mark_dirty();
    for (uint8_t cycle = 0; cycle < 5; cycle++) {
        key(KC_HOME, true);
        key(KC_HOME, false);
        assert(seq_state.running && !seq_external_clock);
        test_now += 250;
        seq_matrix_scan();
        seq_backup_request(13, 0, 0, NULL, 0);
        assert(!backup_status && (backup_data[1] & 2));
        key(KC_HOME, true);
        key(KC_HOME, false);
        assert(!seq_state.running && seq_storage_save_pending);
        seq_housekeeping();
        assert(!seq_storage_save_pending && !seq_transfer_active);
        seq_backup_request(11, 0, 0, NULL, 0);
        assert(!backup_status && !(backup_data[1] & 2));
    }
}
static void test_visual_state(void) {
    reset();
    seq_enter(test_now);
    seq_transport_start(test_now, 0);
    seq_backup_request(13, 0, 0, NULL, 0);
    assert(!backup_status && backup_size == 64 && backup_data[0] == 2);
    assert(backup_data[60] == 1 && backup_data[63] == 1);
    uint8_t token = backup_data[62];
    assert(token && token < 128 && seq_state.running && !seq_transfer_active);
    for (uint16_t offset = 64; offset < SEQ_VISUAL_SIZE; offset += 64) {
        seq_backup_request(13, token, offset, NULL, 0);
        assert(!backup_status && backup_size == (offset == 320 ? 32 : 64));
    }
    seq_backup_request(13, token, 65, NULL, 0);
    assert(backup_status == 3);
    seq_backup_request(13, token, SEQ_VISUAL_SIZE, NULL, 0);
    assert(backup_status == 3);
    seq_backup_request(13, 0, 0, NULL, 1);
    assert(backup_status == 3);
    seq_backup_request(13, 0, 0, NULL, 0);
    assert(!backup_status && backup_data[62] != token);
    seq_backup_request(13, token, 64, NULL, 0);
    assert(backup_status == 2); /* Another reader replaced the frozen image. */
    token = seq_visual_token;
    test_now += 2001;
    seq_backup_request(13, token, 64, NULL, 0);
    assert(backup_status == 2);
    assert(seq_state.running && !seq_transfer_active);
    current_layout = SEQ_LAYOUT_GRID_4X4;
    switch_state.held = true;
    seq_backup_request(13, 0, 0, NULL, 0);
    assert(backup_data[60] == 6 && (backup_data[61] & 1));
    switch_state.held = false;
    cc_context.active = true;
    assert(seq_visual_mode() == 5);
    cc_context.active = false;
    step_edit.active = true;
    step_edit.is_drum = true;
    assert(seq_visual_mode() == 7);
    step_edit.is_drum = false;
    assert(seq_visual_mode() == 8);
    step_edit.active = false;
    cmd_state.held = true;
    seq_module_default(&seq_modules[0], SEQ_MODULE_EUCLID);
    assert(seq_visual_mode() == 3);
    cmd_state.held = false;
    seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    assert(seq_visual_mode() == 2);
    seq_state.active = false;
    assert(seq_visual_mode() == 0);
}
static void test_step_note_state(void) {
    reset();
    seq_enter(test_now);
    seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    track_length_steps[0] = 32;
    assert(mel_track_add_note(0, 15, 17, 60, 100));
    assert(mel_track_add_note(0, 16, 16, 64, 100));
    assert(mel_track_add_note(0, 16, 16, 0, 100));
    assert(mel_track_add_note(0, 16, 16, 127, 100));
    seq_transport_start(test_now, 0);
    view_bar = 1;
    seq_backup_request(14, 0, 0, NULL, 0);
    assert(!backup_status && backup_size == 64 && backup_data[0] == 3);
    assert(backup_data[14] == 1 && (backup_data[1] & 2) && !seq_transfer_active);
    uint8_t token = backup_data[62], image[SEQ_DETAIL_SIZE];
    memcpy(image, backup_data, 64);
    /* Edits and track changes between pages cannot mix snapshot generations. */
    mel_track_reset(0);
    view_slot = 1;
    view_bar = 0;
    for (uint16_t offset = 64; offset < SEQ_DETAIL_SIZE; offset += 64) {
        seq_backup_request(14, token, offset, NULL, 0);
        assert(!backup_status && backup_size == (offset == 576 ? 32 : 64));
        memcpy(image + offset, backup_data, backup_size);
    }
    for (uint8_t step = 0; step < 16; step++) {
        for (uint8_t pitch = 0; pitch < 128; pitch++) {
            bool expected = (step <= 1 && pitch == 60) || (!step && (pitch == 0 || pitch == 64 || pitch == 127));
            assert(!!(image[SEQ_VISUAL_SIZE + step * 16 + pitch / 8] & (1u << (pitch % 8))) == expected);
        }
    }
    seq_backup_request(13, token, 64, NULL, 0);
    assert(backup_status == 2); /* A different snapshot format cannot reuse the token. */
    seq_backup_request(14, token, SEQ_DETAIL_SIZE, NULL, 0);
    assert(backup_status == 3);
    seq_backup_request(14, 0, 0, NULL, 0);
    for (uint16_t i = SEQ_VISUAL_SIZE; i < SEQ_DETAIL_SIZE; i++) assert(!seq_visual_image[i]);
    view_slot = 0;
    assert(mel_track_add_note(0, 31, 35, 72, 100));
    view_bar = 1;
    seq_backup_request(14, 0, 0, NULL, 0);
    assert(seq_visual_image[SEQ_VISUAL_SIZE + 15 * 16 + 9] == 1);
    view_bar = 2;
    seq_backup_request(14, 0, 0, NULL, 0);
    for (uint16_t i = SEQ_VISUAL_SIZE; i < SEQ_DETAIL_SIZE; i++) assert(!seq_visual_image[i]);
    view_bar = 1;
    seq_slots[0].wraps_drum = true;
    view_child_active = true;
    view_child_slot = 0;
    seq_backup_request(14, 0, 0, NULL, 0);
    for (uint16_t i = SEQ_VISUAL_SIZE; i < SEQ_DETAIL_SIZE; i++) assert(!seq_visual_image[i]);
    assert(seq_state.running && !seq_transfer_active);
}
static void test_euclid_picked_notes(void) {
    reset();
    euclid_tracks[0].pitch_mode = 1; /* Legacy transpose contract. */
    seq_enter(test_now);
    seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    track_length_steps[0] = 16;
    seq_euclid_enable(0, true);
    euclid_tracks[0].fills = 16;
    euclid_tracks[0].steps = 16;
    euclid_tracks[0].probability = 100;
    euclid_tracks[0].note_count = 1;
    euclid_tracks[0].note_pool[0] = 0;
    seq_accumulators[0].enabled = false;
    melodic_last_mask[0] = (1u << 2) | (1u << 9);
    assert(mel_track_add_note(0, 4, 6, 90, 100));
    for (uint8_t step = 0; step < 16; step++) {
        if (step == 2) melodic_key_mask[0] = 1u << 7; /* Held notes override the last pick. */
        if (step == 3) melodic_key_mask[0] = 0;
        if (step == 8) {
            melodic_last_mask[0] = 1u << 5;
            euclid_tracks[0].note_pool[0] = 3;
        }
        virtual_midi_clear_events();
        if (!step) seq_transport_start(test_now, 0);
        else seq_play_step(test_now += 125);
        bool source = step >= 4 && step <= 6;
        uint8_t offset = step >= 8 ? 3 : 0;
        uint16_t picked = step == 2 ? 1u << 7 : melodic_last_mask[0];
        for (uint8_t pitch = 0; pitch < 128; pitch++) {
            bool expected = source && pitch == 90;
            for (uint8_t semitone = 0; !source && semitone < 12; semitone++)
                if ((picked & (1u << semitone)) && pitch == seq_melodic_note_pitch(0, semitone) + offset) expected = true;
            assert(melodic_active_contains(0, pitch) == expected);
        }
        seq_backup_request(16, 0, 0, NULL, 0);
        assert(!memcmp(seq_visual_image + 640 + step * 16, melodic_active_map[0], 16));
    }
    assert(mel_track_event_count(0) == 1); /* Generated notes never become stored events. */
    seq_transport_stop(test_now);
    assert(!melodic_active_any(0));
    seq_euclid_enable(0, false);
    seq_transport_start(test_now, 0);
    assert(!melodic_active_any(0));
    seq_transport_stop(test_now);
    seq_euclid_enable(0, true);
    euclid_tracks[0].probability = 0;
    seq_transport_start(test_now, 0);
    assert(!melodic_active_any(0));
    euclid_tracks[0].probability = 100;
    melodic_track_muted[0] = true;
    seq_play_step(test_now += 125);
    assert(!melodic_active_any(0));
    /* Composed track-note modules share the same fallback; banks retain theirs. */
    euclid_tracks[0].note_pool[0] = 3;
    melodic_track_muted[0] = false;
    seq_module_t module;
    seq_module_export(0, &module);
    module.rhythm = seq_module_rhythm(&module);
    module.composed = true;
    seq_module_install(0, &module);
    seq_transport_start(test_now, 0);
    assert(melodic_active_contains(0, seq_melodic_note_pitch(0, 5) + 3));
    seq_transport_stop(test_now);
}
static void test_palette_pitch_mode(void) {
    reset();
    seq_enter(test_now);
    seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    seq_euclid_enable(0, true);
    euclid_tracks[0].steps = 16;
    euclid_tracks[0].fills = 16;
    euclid_tracks[0].palette_root = 5; /* F, despite the old C pick. */
    euclid_tracks[0].note_count = 2;
    euclid_tracks[0].note_pool[0] = 0;
    euclid_tracks[0].note_pool[1] = 7;
    melodic_last_mask[0] = 1;
    seq_accumulators[0].enabled = false;
    assert(!euclid_tracks[0].pitch_mode);
    assert(mel_track_add_note(0, 4, 6, 60, 100));
    for (uint8_t step = 0; step < 16; step++) {
        if (!step) seq_transport_start(test_now, 0);
        else seq_play_step(test_now += 125);
        uint8_t expected = step >= 4 && step <= 6 ? 60 : seq_melodic_note_pitch(0, 5) + (step % 2 ? 7 : 0);
        for (uint8_t n = 0; n < 128; n++) assert(melodic_active_contains(0, n) == (n == expected));
        seq_backup_request(17, 0, 0, NULL, 0);
        assert(!memcmp(seq_visual_image + 640 + step * 16, melodic_active_map[0], 16));
        assert(seq_visual_image[954] == 2 && !seq_visual_image[955]);
        assert(seq_visual_image[952] == 0x70 && seq_visual_image[940] == 255);
    }
    uint8_t clear[] = {1}, mode[] = {0, 1};
    seq_backup_request(18, 1, 5, clear, 1); /* Stale track cannot mutate. */
    assert(backup_status == 2 && mel_track_event_count(0) == 1);
    seq_backup_request(18, 0, 5, clear, 1);
    assert(!backup_status && mel_track_event_count(0) == 2 && !melodic_active_any(0));
    seq_backup_request(17, 0, 0, NULL, 0);
    assert(seq_visual_image[952] == 0x50 && seq_visual_image[941] == 1);
    uint8_t palette_pitch = seq_melodic_note_pitch(0, 5) + 7;
    assert(seq_visual_image[640 + 5 * 16 + palette_pitch / 8] & (1 << (palette_pitch % 8)));
    seq_backup_request(18, 0, 0, mode, 2);
    assert(!backup_status && euclid_tracks[0].pitch_mode == 1);
    seq_backup_request(18, 0, 4, clear, 1);
    assert(backup_status == 2 && mel_track_event_count(0) == 2);
    mode[1] = 3;
    seq_backup_request(18, 0, 0, mode, 2);
    assert(backup_status == 3 && euclid_tracks[0].pitch_mode == 1);
    seq_transport_stop(test_now);
    mode[1] = 0;
    seq_backup_request(18, 0, 0, mode, 2);
    seq_euclid_mode_enter(test_now);
    key(KC_LALT, true); key(KC_LSFT, true);
    seq_palette_replace[0] = true;
    key(KC_R, true); key(KC_R, false); /* Capture only F, replacing both entries. */
    key(KC_LSFT, false); key(KC_LALT, false);
    assert(euclid_tracks[0].note_count == 1 && euclid_tracks[0].palette_root == 5);
    assert(melodic_last_mask[0] == 1);
    seq_transport_start(test_now, 0);
    assert(melodic_active_contains(0, seq_melodic_note_pitch(0, 5)));
    assert(!melodic_active_contains(0, seq_melodic_note_pitch(0, 0)));
    seq_transport_stop(test_now);
    mel_track_reset(0);
    assert(mel_track_add_note(0, 0, 2, 60, 100));
    for (uint8_t i = 1; i < MEL_TRACK_MAX_EVENTS; i++) assert(mel_track_add_note(0, 10 + i, 10 + i, 61, 100));
    seq_backup_request(18, 0, 1, clear, 1);
    assert(backup_status == 4 && mel_track_event_count(0) == MEL_TRACK_MAX_EVENTS);
    mel_event_snapshot_t unchanged;
    assert(mel_track_event_at(0, 0, &unchanged) && unchanged.start_step == 0 && unchanged.end_step == 2);
    mel_track_reset(0);
    /* Both pitch modes survive module and full-track storage, including old v1. */
    for (uint8_t composed = 0; composed < 2; composed++) for (uint8_t pitch = 0; pitch < 2; pitch++) {
        seq_module_t m, decoded;
        seq_module_export(0, &m);
        m.rhythm = seq_module_rhythm(&m); m.composed = composed; m.config.euclid.pitch_mode = pitch;
        uint8_t bytes[SEQ_MODULE_WIRE_MAX]; uint16_t size = sizeof(bytes);
        assert(seq_module_encode(&m, bytes, &size) && seq_module_decode(&decoded, bytes, size));
        assert(decoded.config.euclid.pitch_mode == pitch && decoded.composed == composed);
        seq_module_install(0, &decoded);
        uint8_t project[SEQ_TRANSFER_CAPACITY]; size = sizeof(project);
        assert(seq_project_object(1, project, &size, false, false));
        euclid_tracks[0].pitch_mode = !pitch;
        assert(seq_project_restore_object(1, project, size));
        assert(euclid_tracks[0].pitch_mode == pitch);
    }
}
static void test_scale_quantizer(void) {
    const uint8_t expected[SEQ_SCALE_COUNT][7] = {{0,2,4,5,7,9,11},{0,2,3,5,7,9,10},{0,1,3,5,7,8,10},{0,2,4,6,7,9,11},{0,2,4,5,7,9,10},{0,2,3,5,7,8,10},{0,1,3,5,6,8,10},{0,2,3,5,7,8,11},{0,2,4,5,7,8,11},{0,2,4,7,9},{0,3,5,7,10}};
    for (uint8_t kind=0;kind<SEQ_SCALE_COUNT;kind++) {
        uint16_t mask=0;for(uint8_t i=0;i<(kind>=9?5:7);i++)mask|=1u<<expected[kind][i];assert(seq_scale_mask(kind)==mask);
        for(uint8_t root=0;root<12;root++)for(uint8_t note=0;note<128;note++)for(uint8_t direction=0;direction<3;direction++) {
            uint8_t q=seq_scale_quantize(note,root,kind,direction);assert(q<=127 && seq_scale_contains(q,root,kind));
            if(seq_scale_contains(note,root,kind))assert(q==note);
        }
    }
    assert(seq_scale_quantize(61,0,0,0)==60 && seq_scale_quantize(61,0,0,2)==62);
    reset();seq_enter(test_now);seq_slots[0].kind=SEQ_SLOT_KIND_MELODIC;seq_slots[1].kind=SEQ_SLOT_KIND_MELODIC;
    seq_scale_set_global((seq_scale_context_t){1,0,0});
    seq_accumulators[0].enabled=false;seq_accumulators[1].enabled=false;
    seq_scale_tracks[1].mode=2;
    assert(mel_track_add_note(0,0,2,60,100));assert(mel_track_add_note(0,0,1,61,100));
    seq_transport_start(test_now,0);
    assert(melodic_active_contains(0,60) && !melodic_active_contains(0,61));
    seq_play_step(test_now+=125);seq_play_step(test_now+=125);assert(melodic_active_contains(0,60)); /* Shared snapped pitch stays until its final owner ends. */
    assert(seq_scale_apply(1,61)==61);
    seq_scale_tracks[1]=(seq_scale_track_t){1,2,0,0};assert(seq_scale_apply(1,60)==59);
    seq_chord_mode[1]=true;assert(seq_scale_apply(1,60)==60);
    seq_scale_set_global((seq_scale_context_t){1,2,1});assert(!melodic_active_any(0));
    assert(mel_track_event_count(0)==2);
    seq_transport_stop(test_now);assert(!melodic_active_any(0));
}
static void test_global_tempo(void) {
    reset();seq_enter(test_now);key(KC_LCTL,true);
    assert(seq_cc_encoder_update(0,true));assert(bpm==121); /* Keymap stops after this handler. */
    key(KC_LSFT,true);assert(!seq_encoder_update(0,true));assert(bpm==126);key(KC_LSFT,false);
    seq_slots[0].kind=SEQ_SLOT_KIND_MELODIC;seq_chord_mode_set(0,true);
    seq_chord_event_t chord=seq_chord_draft[0];assert(!seq_encoder_update(0,false));assert(bpm==125);assert(!memcmp(&chord,&seq_chord_draft[0],sizeof(chord)));
    cc_session.active=true;cc_session.track=0;cc_session.mapping_index=0;uint8_t cc=cc_values[0][0];
    assert(seq_cc_encoder_update(0,true));assert(bpm==126 && cc_values[0][0]==cc);cc_session.active=false;
    switch_state.held=true;uint8_t swing=seq_timing_swing;seq_encoder_update(0,true);assert(bpm==127 && seq_timing_swing==swing);switch_state.held=false;
    seq_scale_editing=true;seq_scale_pending=(seq_scale_context_t){1,2,3};seq_encoder_update(0,true);assert(bpm==128 && seq_scale_pending.root==2 && seq_scale_pending.kind==3);seq_scale_editing=false;
    euclid_param_mode=EUCLID_PARAM_MODE_FILLS;uint16_t fills=euclid_tracks[0].fills;seq_encoder_update(0,true);assert(bpm==129 && euclid_tracks[0].fills==fills);euclid_param_mode=EUCLID_PARAM_MODE_NONE;
    record_state.armed=true;seq_encoder_update(0,true);assert(bpm==130 && record_state.armed);record_state.armed=false;
    seq_external_clock=true;seq_encoder_update(0,true);assert(bpm==130);seq_external_clock=false;
    seq_transfer_active=true;seq_encoder_update(0,true);assert(bpm==130);seq_transfer_active=false;
    bpm=400;seq_encoder_update(0,true);assert(bpm==400);bpm=10;seq_encoder_update(0,false);assert(bpm==10);
    key(KC_LCTL,false);assert(!seq_cc_encoder_update(0,true));
    reset();assert(!seq_cc_encoder_update(0,true));assert(seq_encoder_update(0,true));
}
static void test_custom_chords(void) {
    reset();seq_enter(test_now);seq_slots[0].kind=SEQ_SLOT_KIND_MELODIC;seq_chord_mode_set(0,true);
    seq_chord_event_t c={.root=64,.type=SEQ_CHORD_CUSTOM,.length=4,.velocity=94,.custom_count=4,.intervals={-4,0,3,10},.name="Colour"};
    uint8_t notes[8];assert(seq_chord_render(&c,notes)==4 && notes[0]==60 && notes[3]==74);
    assert(seq_chord_write(0,0,c,false));uint16_t step=seq_step_key(1);key(step,true);
    key(KC_P,true);key(KC_P,false);seq_encoder_update(0,false);assert(seq_chord_draft[0].velocity==93 && seq_chord_parameter==4);key(step,false);
    for(uint8_t i=0;i<4;i++){mel_event_snapshot_t e;mel_track_event_at(0,i,&e);assert(e.velocity==93 && e.end_step==3);}
    c.quantize=1;c.inversion=2;c.quantize_root=1;c.quantize_kind=9;assert(seq_chord_write(0,0,c,false));seq_chord_draft[0]=c;
    uint8_t bytes[SEQ_TRANSFER_CAPACITY],bad[SEQ_TRANSFER_CAPACITY];uint16_t size=sizeof(bytes);
    assert(seq_project_object(1,bytes,&size,false,false));uint16_t extension=0;
    for(uint16_t i=0;i+20<size;i++)if(bytes[i]=='C'&&bytes[i+1]==3&&bytes[i+2]==1&&bytes[i+3]==1){extension=i;break;}assert(extension);
    assert(seq_project_restore_object(1,bytes,size));assert(seq_chord_draft[0].root==64&&seq_chord_draft[0].custom_count==4&&seq_chord_draft[0].quantize&&seq_chord_draft[0].intervals[0]==-4&&!memcmp(seq_chord_draft[0].name,"Colour",6));
    memcpy(bad,bytes,size);bad[extension+12]=2;assert(!seq_project_restore_object(1,bad,size));
    memcpy(bad,bytes,size);bad[extension+15]=9;assert(!seq_project_restore_object(1,bad,size));
    memcpy(bad,bytes,size);bad[extension+1]=4;assert(!seq_project_restore_object(1,bad,size));
    for(uint16_t truncated=extension;truncated<size;truncated++)assert(!seq_project_restore_object(1,bytes,truncated));
    seq_dirty=true;seq_storage_save_if_dirty();assert(!seq_dirty);mel_track_reset(0);seq_chord_mode[0]=false;seq_storage_load();assert(seq_chord_mode[0]&&seq_chord_draft[0].quantize&&seq_chord_count(0)==1);
    seq_backup_request(22,0,0,NULL,0);assert(!backup_status&&backup_data[0]==4&&backup_data[31]==1&&backup_data[32]==4&&backup_data[53]==94);
    assert(seq_visual_image[64+9]==4 && !memcmp(seq_visual_image+64+10,"Colour",6));
    uint8_t token=backup_data[62];seq_backup_request(22,token,384,NULL,0);assert(!backup_status&&backup_size==64);seq_backup_request(22,token,448,NULL,0);assert(backup_status==3);
    /* Rendering all inversions/scale contexts always yields unique in-scale voices. */
    for(uint8_t root=0;root<12;root++)for(uint8_t kind=0;kind<SEQ_SCALE_COUNT;kind++)for(uint8_t inversion=0;inversion<4;inversion++) {
        c.quantize_root=root;c.quantize_kind=kind;c.inversion=inversion;uint8_t count=seq_chord_render(&c,notes);assert(count&&count<=4);
        for(uint8_t i=0;i<count;i++){assert(seq_scale_contains(notes[i],root,kind));if(i)assert(notes[i]>notes[i-1]);}
    }
    c=(seq_chord_event_t){.root=61,.type=SEQ_CHORD_CUSTOM,.length=4,.velocity=100,.custom_count=3,.intervals={-1,0,1},.name="Cluster",.quantize=1};
    assert(seq_chord_render(&c,notes)==2&&notes[0]==60&&notes[1]==62);c.quantize=0;assert(seq_chord_render(&c,notes)==3&&notes[1]==61);
    /* Octave doublings move to the next unoccupied octave when inverted. */
    c.intervals[0]=-1;c.intervals[1]=3;c.intervals[2]=11;c.inversion=1;assert(seq_chord_render(&c,notes)==3&&notes[0]==64&&notes[1]==72&&notes[2]==84);
    c.root=0;assert(!seq_chord_render(&c,notes));c.root=127;assert(!seq_chord_render(&c,notes));c.root=61;c.custom_count=9;assert(!seq_chord_render(&c,notes));
    /* Importing an older recipe resets custom data and quantization. */
    c=(seq_chord_event_t){.root=60,.type=0,.length=4,.velocity=100};assert(seq_chord_write(0,0,c,false));seq_chord_draft[0]=c;size=sizeof(bytes);assert(seq_project_object(1,bytes,&size,false,false));
    seq_chord_draft[0].quantize=1;seq_chord_draft[0].custom_count=8;assert(seq_project_restore_object(1,bytes,size));assert(!seq_chord_draft[0].quantize&&!seq_chord_draft[0].custom_count);
}
static void test_chord_inversions(void) {
    reset();seq_enter(test_now);seq_slots[0].kind=SEQ_SLOT_KIND_MELODIC;seq_chord_mode_set(0,true);
    uint8_t notes[8];
    seq_chord_event_t c={.root=60,.type=0,.length=4,.velocity=97,.inversion=1};
    assert(seq_chord_render(&c,notes)==3 && notes[0]==64 && notes[1]==67 && notes[2]==72);
    assert(seq_chord_write(0,0,c,false));
    uint16_t step=seq_step_key(2);key(step,true);key(KC_LBRC,true);key(KC_LBRC,false);
    assert(seq_chord_parameter==3 && seq_chord_selected==0 && seq_chord_held==step);
    seq_encoder_update(0,true);assert(seq_chord_draft[0].inversion==2 && seq_chord_draft[0].root==60);
    seq_encoder_update(0,true);assert(seq_chord_draft[0].inversion==2);
    key(step,false);assert(seq_chord_selected==SEQ_CHORD_NONE);
    mel_event_snapshot_t ev;mel_track_event_at(0,0,&ev);assert(ev.pitch==67 && ev.velocity==97 && ev.end_step==3);
    uint8_t bytes[SEQ_TRANSFER_CAPACITY],bad[SEQ_TRANSFER_CAPACITY];uint16_t size=sizeof(bytes);
    assert(seq_project_object(1,bytes,&size,false,false));
    uint16_t extension=0;for(uint16_t i=0;i+12<size;i++)if(bytes[i]=='C'&&bytes[i+1]==2&&bytes[i+2]==1&&bytes[i+3]==1){extension=i;break;}
    assert(extension);assert(bytes[extension+11]==2 && bytes[extension+21]==2);
    assert(seq_project_restore_object(1,bytes,size));assert(seq_chord_draft[0].inversion==2 && seq_chord_events[seq_chord_find(0,0)].inversion==2);
    memcpy(bad,bytes,size);bad[extension+21]=3;assert(!seq_project_restore_object(1,bad,size));
    memcpy(bad,bytes,size);bad[extension+21]=1;assert(!seq_project_restore_object(1,bad,size));
    seq_dirty=true;seq_storage_save_if_dirty();assert(!seq_dirty);seq_chord_draft[0].inversion=0;mel_track_reset(0);seq_storage_load();
    assert(seq_chord_draft[0].inversion==2 && seq_chord_events[seq_chord_find(0,0)].inversion==2);
    seq_backup_request(22,0,0,NULL,0);assert(!backup_status && backup_data[0]==4 && backup_data[30]==2);
    assert(seq_visual_image[64+7]==2);
    /* A v1 restore must reset a pre-existing v2 draft inversion. */
    c.inversion=0;assert(seq_chord_write(0,0,c,false));seq_chord_draft[0]=c;size=sizeof(bytes);assert(seq_project_object(1,bytes,&size,false,false));
    assert(bytes[extension+1]==1);seq_chord_draft[0].inversion=2;assert(seq_project_restore_object(1,bytes,size));assert(!seq_chord_draft[0].inversion);
    /* Reducing voice count clamps the inversion and updates every owned voice. */
    c.type=7;c.inversion=3;assert(seq_chord_write(0,0,c,false));key(step,true);key(KC_RBRC,true);key(KC_RBRC,false);
    seq_encoder_update(0,false);seq_encoder_update(0,false);assert(seq_chord_draft[0].type==5 && seq_chord_draft[0].inversion==2 && mel_track_event_count(0)==3);key(step,false);
    /* MIDI-boundary failure retains both recipes and baked notes. */
    c=(seq_chord_event_t){.root=116,.type=0,.length=4,.velocity=97};assert(seq_chord_write(0,0,c,false));key(step,true);key(KC_LBRC,true);key(KC_LBRC,false);seq_encoder_update(0,true);
    assert(seq_chord_error==1 && !seq_chord_draft[0].inversion);key(step,false);
    /* All fixed types and all scale contexts, including wide pentatonic sevenths. */
    for(uint8_t type=0;type<12;type++)for(uint8_t kind=0;kind<SEQ_SCALE_COUNT;kind++)for(uint8_t root=0;root<12;root++) {
        c=(seq_chord_event_t){.root=60+root,.type=type,.length=4,.velocity=100,.scale_root=root,.scale_kind=kind};
        uint8_t expected[8],count=seq_chord_render(&c,expected);assert(count==(type<6||type==10?3:4));
        for(uint8_t inversion=0;inversion<count;inversion++) {
            c.inversion=inversion;assert(seq_chord_render(&c,notes)==count);assert(!memcmp(notes,expected,count));
            uint8_t raised=expected[0]+12;for(uint8_t j=1;j<count;j++)expected[j-1]=expected[j];expected[count-1]=raised;
            for(uint8_t j=0;j<count;j++)for(uint8_t k=j+1;k<count;k++)if(expected[j]>expected[k]){uint8_t tmp=expected[j];expected[j]=expected[k];expected[k]=tmp;}
        }
        c.inversion=count;assert(!seq_chord_render(&c,notes));
    }
}
static void test_chord_hold_length(void) {
    reset();seq_enter(test_now);seq_slots[0].kind=SEQ_SLOT_KIND_MELODIC;seq_chord_mode_set(0,true);
    seq_chord_event_t c={.root=60,.type=0,.length=4,.velocity=100};
    assert(seq_chord_write(0,0,c,false));uint16_t k=seq_step_key(1);
    key(k,true);seq_encoder_update(0,true);
    assert(seq_chord_parameter==2 && seq_chord_draft[0].length==5 && seq_chord_draft[0].root==60);
    key(KC_O,true);key(KC_O,false);seq_encoder_update(0,true);
    assert(seq_chord_held==k && seq_chord_draft[0].root==61 && seq_chord_draft[0].length==5);
    key(KC_I,true);key(KC_I,false);seq_encoder_update(0,false);assert(seq_chord_draft[0].length==4);
    key(k,false);assert(seq_chord_selected==SEQ_CHORD_NONE);
    key(k,true);assert(seq_chord_parameter==2);seq_encoder_update(0,true);key(k,false);
    for(uint8_t i=0;i<3;i++){mel_event_snapshot_t event;assert(mel_track_event_at(0,i,&event));assert(event.end_step==4);}
}
static void test_chord_mode(void) {
    reset();seq_enter(test_now);
    key(KC_LCTL,true);key(KC_TAB,true);key(KC_F1,true);key(KC_LCTL,false);key(KC_TAB,false);key(KC_F1,false);
    assert(track_confirm.active && track_confirm.target_kind==SEQ_SLOT_KIND_CHORD);
    key(KC_Y,true);key(KC_Y,false);assert(seq_chord_mode[0] && slot_is_melodic(0));
    seq_chord_event_t c={.track=0,.root=60,.type=1,.length=8,.velocity=100};
    assert(seq_chord_write(0,0,c,false));assert(mel_track_event_count(0)==3);
    c.root=67;assert(seq_chord_write(0,4,c,false));assert(seq_chord_events[seq_chord_find(0,0)].length==4);assert(seq_chord_events[seq_chord_find(0,6)].root==67);
    c.root=62;c.type=8;c.length=16;assert(seq_chord_write(0,0,c,false));assert(mel_track_event_count(0)==7 && seq_chord_events[seq_chord_find(0,0)].length==4);
    uint16_t step=seq_step_key(2);key(step,true);assert(seq_chord_selected==0);key(KC_RBRC,true);key(KC_RBRC,false);seq_encoder_update(0,true);
    assert(seq_chord_draft[0].type==9 && seq_chord_held==step);key(step,false);assert(seq_chord_selected==SEQ_CHORD_NONE);
    step=seq_step_key(5);key(step,true);key(KC_I,true);key(KC_I,false);seq_encoder_update(0,false);key(step,false);
    assert(seq_chord_events[seq_chord_find(0,4)].length==7);
    uint8_t bytes[SEQ_TRANSFER_CAPACITY];uint16_t size=sizeof(bytes);assert(seq_project_object(1,bytes,&size,false,false));
    assert(seq_project_restore_object(1,bytes,size));assert(seq_chord_mode[0] && seq_chord_count(0)==2);
    uint8_t bad[SEQ_TRANSFER_CAPACITY];memcpy(bad,bytes,size);
    /* Locate the recipe extension by its validated draft header. */
    uint16_t extension=0;for(uint16_t i=0;i+11<size;i++)if(bytes[i]=='C'&&bytes[i+1]==1&&bytes[i+2]==1&&bytes[i+3]==2){extension=i;break;}
    assert(extension);bad[extension+11+4]++; /* First recipe root disagrees with baked notes. */
    assert(!seq_project_restore_object(1,bad,size));assert(seq_chord_count(0)==2);
    memcpy(bad,bytes,size);bad[2]=SEQ_SLOT_KIND_DRUM;
    assert(!seq_project_restore_object(1,bad,size));assert(seq_chord_mode[0] && seq_chord_count(0)==2);
    seq_dirty=true;seq_storage_save_if_dirty();assert(!seq_dirty);
    memset(seq_chord_events,255,sizeof(seq_chord_events));seq_chord_mode[0]=false;mel_track_reset(0);seq_storage_load();assert(seq_chord_mode[0] && seq_chord_count(0)==2 && mel_track_event_count(0)==7);
    uint8_t image[8];for(uint8_t kind=0;kind<SEQ_SCALE_COUNT;kind++){c=(seq_chord_event_t){.root=seq_scale_quantize(60,2,kind,0),.type=10,.length=4,.velocity=100,.scale_root=2,.scale_kind=kind};assert(seq_chord_render(&c,image)==3);for(uint8_t i=0;i<3;i++)assert(seq_scale_contains(image[i],2,kind));}
    c.root=127;c.type=0;assert(!seq_chord_render(&c,image));
    seq_chord_mode_set(0,false);assert(mel_track_event_count(0)==7 && seq_chord_count(0)==2);
    mel_track_clear_range(0,0,4);seq_chord_prune();assert(seq_chord_count(0)==1);
    seq_chord_mode_set(0,true);step=seq_step_key(5);key(KC_DEL,true);key(step,true);key(step,false);key(KC_DEL,false);assert(seq_chord_count(0)==0 && mel_track_event_count(0)==0);
    /* Foreign polyphony is preserved and cannot be overwritten as an owned chord. */
    mel_track_add_note(0,0,4,61,100);c=(seq_chord_event_t){.root=60,.type=0,.length=4,.velocity=100};assert(!seq_chord_write(0,0,c,false)&&seq_chord_error==3&&mel_track_event_count(0)==1);
    mel_track_reset(0);
    /* No partial chord if the event budget is exhausted. */
    for(uint8_t i=0;i<126;i++)mel_track_add_note(0,15,15,i,100);
    assert(!seq_chord_write(0,0,c,false)&&seq_chord_error==2&&mel_track_event_count(0)==126);
    mel_track_reset(0);
    key(KC_W,true);assert(module_preview.active&&module_preview.count==3);key(KC_W,false);assert(!module_preview.active);
    seq_backup_request(22,0,0,NULL,0);assert(!backup_status&&backup_data[0]==4&&backup_data[2]==1);
    /* Explicit module installation exits the editor, but keeps authored notes. */
    assert(seq_chord_write(0,0,c,false));
    seq_module_t bank;seq_module_default(&bank,SEQ_MODULE_EUCLID);size=sizeof(bytes);
    assert(seq_module_encode(&bank,bytes,&size));assert(seq_project_restore_object(13,bytes,size));
    assert(!seq_chord_mode[0] && seq_modules[0].kind==SEQ_MODULE_EUCLID && mel_track_event_count(0)==3);
    /* The shared recipe pool fails atomically across tracks. */
    reset();seq_enter(test_now);c=(seq_chord_event_t){.root=60,.type=0,.length=1,.velocity=100};
    for(uint8_t t=0;t<4;t++){seq_slots[t].kind=SEQ_SLOT_KIND_MELODIC;seq_chord_mode_set(t,true);for(uint8_t i=0;i<16;i++)assert(seq_chord_write(t,i,c,false));}
    seq_slots[4].kind=SEQ_SLOT_KIND_MELODIC;seq_chord_mode_set(4,true);
    assert(!seq_chord_write(4,0,c,false)&&seq_chord_error==2&&!mel_track_event_count(4));
    c.root=62;assert(seq_chord_write(0,0,c,false));assert(seq_chord_count(0)==16 && mel_track_event_count(0)==48);
}
static void test_scale_bound_chords(void) {
    reset();seq_enter(test_now);seq_slots[0].kind=SEQ_SLOT_KIND_MELODIC;
    seq_scale_global=(seq_scale_context_t){1,5,5}; /* F natural minor */
    seq_chord_mode_set(0,true);
    seq_chord_event_t c=seq_chord_draft[0];c.root=67;seq_chord_context(0,&c);
    uint8_t notes[8];assert(c.type==10 && seq_chord_render(&c,notes)==3);
    assert(notes[0]==67 && notes[1]==70 && notes[2]==73);
    assert(seq_chord_write(0,0,c,false));uint16_t k=seq_step_key(0);key(k,true);
    key(KC_RBRC,true);key(KC_RBRC,false);seq_encoder_update(0,true);
    assert(seq_chord_draft[0].type==11 && seq_chord_render(&seq_chord_draft[0],notes)==4 && notes[3]==77);
    seq_encoder_update(0,true);assert(seq_chord_draft[0].type==13 && seq_chord_render(&seq_chord_draft[0],notes)==5 && notes[4]==80);
    for(uint8_t i=0;i<8;i++){seq_encoder_update(0,true);uint8_t count=seq_chord_render(&seq_chord_draft[0],notes);for(uint8_t j=0;j<count;j++)assert(seq_scale_contains(notes[j],5,5));}
    uint8_t before[8],count=seq_chord_render(&seq_chord_draft[0],before);
    key(KC_LSFT,true);key(KC_RBRC,true);key(KC_RBRC,false);key(KC_LSFT,false);
    assert(seq_chord_draft[0].scale_free && seq_chord_held==k);
    assert(seq_chord_render(&seq_chord_draft[0],notes)==count && !memcmp(before,notes,count));
    key(KC_O,true);key(KC_O,false);seq_encoder_update(0,true);assert(seq_chord_draft[0].root==68);
    key(k,false);assert(seq_chord_events[seq_chord_find(0,0)].scale_free);
    uint8_t data[SEQ_TRANSFER_CAPACITY];uint16_t size=sizeof(data);assert(seq_project_object(1,data,&size,false,false));
    assert(seq_project_restore_object(1,data,size));assert(seq_chord_events[seq_chord_find(0,0)].scale_free);
    seq_backup_request(22,0,0,NULL,0);assert(backup_data[0]==4 && backup_data[56]==1 && backup_data[57]==1 && (seq_visual_image[64]&4));
    /* A single-chord exception must never become the next placement's default. */
    k=seq_step_key(8);key(k,true);key(k,false);assert(!seq_chord_events[seq_chord_find(0,8)].scale_free && seq_chord_scale_type(seq_chord_draft[0].type));
    k=seq_step_key(0);key(k,true);key(KC_LSFT,true);key(KC_RBRC,true);key(KC_RBRC,false);key(KC_LSFT,false);
    assert(!seq_chord_draft[0].scale_free && seq_chord_scale_type(seq_chord_draft[0].type));key(k,false);
    /* Every supported scale and degree produces bounded scale-only extensions. */
    for(uint8_t kind=0;kind<SEQ_SCALE_COUNT;kind++)for(uint8_t root=48;root<60;root++)if(seq_scale_contains(root,5,kind))for(uint8_t type=10;type<16;type++)if(type!=12){
        c=(seq_chord_event_t){.root=root,.type=type,.length=4,.velocity=100,.scale_root=5,.scale_kind=kind};
        uint8_t n=seq_chord_render(&c,notes);assert(n==seq_chord_voices(&c));for(uint8_t i=0;i<n;i++)assert(seq_scale_contains(notes[i],5,kind));
    }
}
static void test_firmware_identity(void) {
    reset();
    uint8_t identity[113];
    seq_backup_request(24,0,0,NULL,0);assert(!backup_status && backup_size==64);
    memcpy(identity,backup_data,64);
    seq_backup_request(24,0,64,NULL,0);assert(!backup_status && backup_size==49);
    memcpy(identity+64,backup_data,49);
    assert(!memcmp(identity,"SEQFWID1",8));
    assert(!strcmp((char *)identity+8,SEQ_FIRMWARE_VERSION));
    assert(!strcmp((char *)identity+32,SEQ_FIRMWARE_TARGET));
    assert(!strcmp((char *)identity+80,SEQ_BUILD_ID));
    seq_backup_request(24,1,0,NULL,0);assert(backup_status==3 && !backup_size);
    seq_backup_request(24,0,63,NULL,0);assert(backup_status==3);
    seq_backup_request(24,0,113,NULL,0);assert(backup_status==3);
    seq_backup_request(24,0,0,identity,1);assert(backup_status==3);
    seq_enter(test_now);seq_transfer_active=true;
    seq_backup_request(24,0,0,NULL,0);assert(!backup_status && backup_size==64 && seq_transfer_active);
    seq_transfer_active=false;
}
static void test_step_hold(void) {
    reset();seq_enter(test_now);
    uint16_t k=seq_step_key(0);uint8_t lane=drum_active_note[0];
    seq_drum_step_set(0,lane,0,true);seq_dirty=false;
    key(k,true);assert(seq_step_hold.active && !step_edit.active);
    assert(seq_drum_step_base_hit(0,lane,0));
    seq_encoder_update(0,true);assert(step_edit.active && step_edit.is_drum);
    assert(seq_drum_mod_velocity(seq_drum_step_mod_lookup(0,lane,0))==1 && seq_dirty);
    key(k,false);assert(!step_edit.active && !seq_step_hold.active && seq_drum_step_base_hit(0,lane,0));
    /* Holding without touching the encoder keeps the hit. */
    test_now+=400;key(k,true);test_now+=SEQ_STEP_HOLD_MS;seq_matrix_scan();assert(step_edit.active);
    key(k,false);assert(seq_drum_step_base_hit(0,lane,0));
    /* A short tap still deletes, and double-tap still restores and latches. */
    test_now+=400;key(k,true);assert(seq_drum_step_base_hit(0,lane,0));key(k,false);
    assert(!seq_drum_step_base_hit(0,lane,0));test_now+=100;key(k,true);key(k,false);
    assert(step_edit.active && !seq_step_hold.active && seq_drum_step_base_hit(0,lane,0));
    key(KC_ESC,true);key(KC_ESC,false);
    /* New hits remain immediate and can be adjusted on that same hold. */
    test_now+=400;k=seq_step_key(1);key(k,true);assert(seq_drum_step_base_hit(0,lane,1));
    seq_encoder_update(0,true);key(k,false);assert(seq_drum_mod_velocity(seq_drum_step_mod_lookup(0,lane,1))==1);
    /* Velocity can modify a generated drum pulse without baking a manual hit. */
    test_now+=400;euclid_tracks[0].enabled=true;euclid_tracks[0].steps=16;euclid_tracks[0].fills=16;
    euclid_tracks[0].note_count=1;euclid_tracks[0].note_pool[0]=lane;euclid_tracks[0].palette_pin=255;
    uint16_t generated_key=seq_step_key(2);key(generated_key,true);seq_encoder_update(0,true);key(generated_key,false);
    assert(!seq_drum_step_base_hit(0,lane,2) && seq_drum_mod_velocity(seq_drum_step_mod_lookup(0,lane,2))==1);
    euclid_tracks[0].enabled=false;
    /* A track switch cannot edit the old step via a lingering hold. */
    test_now+=400;key(k,true);seq_encoder_update(0,false);key(KC_F2,true);key(KC_F2,false);key(k,false);
    assert(!seq_step_hold.active && !step_edit.active);

    reset();seq_enter(test_now);seq_slots[0].kind=SEQ_SLOT_KIND_MELODIC;
    mel_track_add_note(0,0,3,64,99);seq_scale_global=(seq_scale_context_t){1,0,0};k=seq_step_key(0);
    key(k,true);seq_encoder_update(0,true);
    assert(step_edit.param==STEP_EDIT_PARAM_LENGTH && step_edit.end_step==4 && step_edit.pitch==64);
    key(KC_O,true);key(KC_O,false);assert(seq_step_hold.active && step_edit.param==STEP_EDIT_PARAM_PITCH);
    seq_encoder_update(0,true);assert(step_edit.pitch==65);
    seq_encoder_update(0,true);assert(step_edit.pitch==67);seq_encoder_update(0,false);assert(step_edit.pitch==65);
    uint8_t live[60];seq_live_snapshot(live);assert(live[40+(65>>3)] & (1u<<(65&7)));
    key(k,false);assert(!step_edit.active && mel_track_event_count(0)==1);
    mel_event_snapshot_t event;mel_track_event_at(0,0,&event);assert(event.pitch==65 && event.end_step==4 && event.velocity==99);
    /* Each hold resets to length; an immediate parameter tap keeps the note. */
    test_now+=400;key(k,true);key(KC_P,true);key(KC_P,false);
    assert(seq_step_hold.editing && step_edit.param==STEP_EDIT_PARAM_VELOCITY);
    seq_encoder_update(0,false);assert(step_edit.velocity==98);
    key(KC_I,true);key(KC_I,false);seq_encoder_update(0,false);assert(step_edit.end_step==3);
    key(k,false);assert(!seq_step_hold.active && !step_edit.active && mel_track_event_count(0)==1);
    test_now+=400;key(k,true);test_now+=SEQ_STEP_HOLD_MS;seq_matrix_scan();
    assert(step_edit.param==STEP_EDIT_PARAM_LENGTH);key(k,false);
    /* Local and disabled scale policies, MIDI boundaries and direction. */
    for(uint8_t kind=0;kind<SEQ_SCALE_COUNT;kind++) {
        seq_scale_tracks[0]=(seq_scale_track_t){1,2,kind,0};mel_track_set_note_pitch(0,0,60);
        test_now+=400;key(k,true);key(KC_O,true);key(KC_O,false);seq_encoder_update(0,true);
        assert(step_edit.pitch>60 && seq_scale_contains(step_edit.pitch,2,kind));key(k,false);
    }
    seq_scale_tracks[0].mode=2;mel_track_set_note_pitch(0,0,65);test_now+=400;key(k,true);key(KC_O,true);key(KC_O,false);seq_encoder_update(0,true);assert(step_edit.pitch==66);key(k,false);
    for(uint8_t boundary=0;boundary<2;boundary++) {mel_track_set_note_pitch(0,0,boundary?127:0);test_now+=400;key(k,true);key(KC_O,true);key(KC_O,false);seq_encoder_update(0,boundary);assert(step_edit.pitch==(boundary?127:0));key(k,false);}
    /* Transfer and Escape cancel a pending tap without deleting notes. */
    test_now+=400;key(k,true);key(KC_ESC,true);key(KC_ESC,false);key(k,false);assert(mel_track_event_count(0)==1 && !step_edit.active);
    test_now+=400;key(k,true);seq_backup_request(1,0,0,NULL,0);key(k,false);assert(!seq_step_hold.active && mel_track_event_count(0)==1);seq_backup_request(7,0,0,NULL,0);
}
static void test_harmony_shortcut(void) {
    reset();seq_enter(test_now);
    uint8_t before = seq_harmony_request;
    key(KC_LCTL,true);key(KC_LSFT,true);key(KC_SPC,true);key(KC_SPC,true);
    assert(seq_harmony_request == (uint8_t)(before+1) && !seq_scale_editing && !switch_state.held);
    key(KC_LCTL,false);key(KC_LSFT,false);key(KC_SPC,false);
    assert(!seq_harmony_shortcut_held && !switch_state.held);
    seq_backup_request(17,0,0,NULL,0);
    assert(seq_visual_image[638] == 255 && seq_visual_image[639] == (uint8_t)(before+1));
    seq_harmony_request=255;
    test_mods=MOD_BIT(KC_RCTL)|MOD_BIT(KC_RSFT);key(KC_SPC,true);key(KC_SPC,false);
    assert(seq_harmony_request==0 && !seq_scale_editing);
    record_state.armed=true;key(KC_SPC,true);key(KC_SPC,false);
    assert(seq_harmony_request==0 && !seq_scale_editing);
    record_state.armed=false;seq_scale_editing=true;key(KC_SPC,true);key(KC_SPC,false);
    assert(seq_harmony_request==0 && seq_scale_editing);
    test_mods=0;seq_scale_editing=false;
}
static void test_scale_editor_storage(void) {
    reset();seq_enter(test_now);seq_slots[0].kind=SEQ_SLOT_KIND_MELODIC;
    assert(key(KC_LCTL,true));key(KC_SPC,true);assert(key(KC_LCTL,false));assert(!seq_ctrl_mask);
    key(KC_SPC,false);assert(seq_scale_editing && !switch_state.held);
    key(KC_W,true);key(KC_W,false);key(KC_S,true);key(KC_S,false); /* D Dorian */
    assert(seq_scale_pending.root==2 && seq_scale_pending.kind==1 && !seq_scale_global.enabled);
    seq_backup_request(17,0,0,NULL,0);assert(seq_visual_image[60]==10 && seq_visual_image[622]==1 && seq_visual_image[630]==1);
    key(KC_ESC,true);key(KC_ESC,false);assert(!seq_scale_editing && !seq_scale_global.enabled && seq_state.active);
    test_mods=MOD_BIT(KC_RCTL);key(KC_SPC,true);test_mods=0;key(KC_SPC,false);
    key(KC_W,true);key(KC_W,false);key(KC_H,true);key(KC_H,false);key(KC_ENT,true);key(KC_ENT,false);
    assert(seq_scale_global.enabled && seq_scale_global.root==2 && seq_scale_global.kind==5);
    uint8_t local[]={1,1,5,3,2,seq_scale_revision&255,seq_scale_revision>>8};
    seq_backup_request(20,0,0,local,7);assert(!backup_status && seq_scale_tracks[0].root==5);
    seq_backup_request(20,0,0,local,7);assert(backup_status==2); /* stale revision */
    uint8_t data[SEQ_TRANSFER_CAPACITY];uint16_t size=sizeof(data);
    assert(seq_project_object(1,data,&size,false,false));seq_scale_tracks[0]=(seq_scale_track_t){0};
    assert(seq_project_restore_object(1,data,size));assert(seq_scale_tracks[0].mode==1 && seq_scale_tracks[0].kind==3 && seq_scale_tracks[0].direction==2);
    seq_dirty=true;seq_storage_save_if_dirty();assert(!seq_dirty);
    seq_scale_global=(seq_scale_context_t){0};seq_scale_tracks[0]=(seq_scale_track_t){0};seq_storage_load();
    assert(seq_scale_global.root==2 && seq_scale_global.kind==5 && seq_scale_tracks[0].root==5);
    size=sizeof(data);assert(seq_project_object(0,data,&size,false,false));data[size-1]=SEQ_SCALE_COUNT;
    assert(!seq_project_restore_object(0,data,size));assert(seq_scale_global.kind==5);
    assert(seq_project_restore_object(0,data,size-5));assert(!seq_scale_global.enabled); /* old globals */
    test_mods=MOD_BIT(KC_LCTL);key(KC_SPC,true);test_mods=0;key(KC_SPC,false);assert(seq_scale_editing);
    seq_backup_request(1,0,0,NULL,0);assert(!seq_scale_editing && seq_transfer_active);
    key(KC_W,true);key(KC_W,false);assert(!seq_scale_global.enabled);
    seq_backup_request(7,0,0,NULL,0);
    const uint16_t extra_keys[] = {KC_K,KC_L,KC_SCLN,KC_QUOT};
    for (uint8_t kind=7;kind<SEQ_SCALE_COUNT;kind++) {
        key(KC_LCTL,true);key(KC_SPC,true);key(KC_SPC,false);key(KC_LCTL,false);
        key(extra_keys[kind-7],true);key(extra_keys[kind-7],false);
        seq_backup_request(17,0,0,NULL,0);
        assert(seq_visual_image[633]==kind && seq_visual_image[636]==SEQ_SCALE_COUNT);
        key(KC_ENT,true);key(KC_ENT,false);assert(seq_scale_global.kind==kind);
        uint8_t track_scale[]={1,1,9,kind,0,seq_scale_revision&255,seq_scale_revision>>8};
        seq_backup_request(20,0,0,track_scale,sizeof(track_scale));assert(!backup_status);
        seq_dirty=true;seq_storage_save_if_dirty();assert(!seq_dirty);
        seq_scale_global=(seq_scale_context_t){0};seq_scale_tracks[0]=(seq_scale_track_t){0};seq_storage_load();
        assert(seq_scale_global.kind==kind && seq_scale_tracks[0].kind==kind);
        for(uint8_t object=0;object<2;object++) {
            size=sizeof(data);assert(seq_project_object(object,data,&size,false,false));
            assert(seq_project_restore_object(object,data,size));
        }
    }
}
static void test_ordered_euclid_controls(void) {
    reset(); seq_enter(test_now); seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    seq_euclid_mode_enter(test_now);
    const uint16_t keys[] = {KC_Q, KC_W, KC_E, KC_R, KC_T};
    const seq_euclid_param_mode_t modes[] = {EUCLID_PARAM_MODE_STEPS, EUCLID_PARAM_MODE_FILLS, EUCLID_PARAM_MODE_ROTATE, EUCLID_PARAM_MODE_PROBABILITY, EUCLID_PARAM_MODE_ACCENT};
    for (uint8_t i = 0; i < 5; i++) {
        key(keys[i], true); key(keys[i], false); assert(euclid_param_mode == modes[i]);
        assert(seq_euclid_apply_encoder(i == 0 || i == 3 ? -1 : 1, test_now));
    }
    assert(euclid_tracks[0].steps == 15 && euclid_tracks[0].fills == 5 && euclid_tracks[0].rotate == 1);
    assert(euclid_tracks[0].probability == 99 && euclid_tracks[0].swing_amount == 1);
    key(KC_LBRC, true); key(KC_LBRC, false); assert(euclid_tracks[0].rotate == 0);
    key(KC_RBRC, true); key(KC_RBRC, false); assert(euclid_tracks[0].rotate == 1);
    assert(seq_euclid_param_mode_for_key(KC_P) == EUCLID_PARAM_MODE_NONE);
    key(KC_T, true); key(KC_T, false); key(KC_LSFT, true);
    seq_euclid_apply_encoder(20, test_now); key(KC_LSFT, false);
    assert(euclid_tracks[0].swing_amount == 48);
    /* Palette capture still treats Q/W/E/R/T as note keys, ahead of parameters. */
    key(KC_LALT, true); key(KC_LSFT, true);
    for (uint8_t i = 0; i < 5; i++) {key(keys[i], true); key(keys[i], false);}
    key(KC_LSFT, false); key(KC_LALT, false);
    assert(euclid_tracks[0].note_count == 5 && euclid_tracks[0].note_pool[4] == 7);
}
static void rhythm_request(uint8_t track, uint8_t field, uint16_t value, uint16_t expected) {
    uint8_t data[] = {field, value & 255, value >> 8, expected & 255, expected >> 8};
    seq_backup_request(19, track, 0, data, sizeof(data));
}
static void test_live_rhythm_edit(void) {
    reset(); seq_enter(test_now); seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    seq_euclid_enable(0, true); seq_transport_start(test_now, 0);
    euclid_tracks[0].fills = 12; euclid_tracks[0].rotate = 15; euclid_tracks[0].ratchet_threshold = 12;
    euclid_tracks[0].template_active = true; euclid_tracks[0].template_mask = 3;
    assert(mel_track_add_note(0, 0, 0, 60, 100));
    uint32_t position = seq_step_position;
    rhythm_request(0, 0, 8, 16); assert(!backup_status);
    assert(euclid_tracks[0].steps == 8 && euclid_tracks[0].fills == 8 && euclid_tracks[0].rotate == 7 && euclid_tracks[0].ratchet_threshold == 8 && !euclid_tracks[0].template_active);
    rhythm_request(0, 1, 3, 8); assert(!backup_status && euclid_tracks[0].fills == 3);
    rhythm_request(0, 2, 2, 7); assert(!backup_status && euclid_tracks[0].rotate == 2);
    rhythm_request(0, 3, 50, 100); assert(!backup_status && euclid_tracks[0].probability == 50);
    rhythm_request(0, 4, 25, 0); assert(!backup_status && euclid_tracks[0].swing_amount == 25);
    rhythm_request(0, 1, 0, 3); rhythm_request(0, 5, 0, 1); rhythm_request(0, 5, 1, 0);
    assert(!backup_status && euclid_tracks[0].enabled && euclid_tracks[0].fills == 0);
    assert(seq_state.running && seq_step_position == position && mel_track_event_count(0) == 1);
    rhythm_request(0, 3, 25, 100); assert(backup_status == 2 && euclid_tracks[0].probability == 50); /* stale value */
    rhythm_request(1, 0, 8, 16); assert(backup_status == 2 && euclid_tracks[1].steps == 16);
    const uint16_t invalid[] = {0,9,8,101,49,2};
    for(uint8_t field = 0; field < 6; field++) {
        seq_module_t module; seq_module_export(0, &module); seq_rhythm_component_t r = seq_module_rhythm(&module);
        rhythm_request(0, field, invalid[field], seq_rhythm_edit_value(&r, field)); assert(backup_status == 3);
    }
    record_state.armed = true; rhythm_request(0, 3, 40, 50); assert(backup_status == 2); record_state.armed = false;
    cmd_state.held = true; rhythm_request(0, 3, 40, 50); assert(backup_status == 2); cmd_state.held = false;
    seq_transfer_active = true; rhythm_request(0, 3, 40, 50); assert(backup_status == 2); seq_transfer_active = false;
    uint8_t data[] = {3,40,0,50,0};
    seq_backup_request(19, 0, 1, data, 5); assert(backup_status == 2); /* stale child view */
    seq_backup_request(19, 0, 0, data, 4); assert(backup_status == 3);
    seq_backup_request(17, 0, 0, NULL, 0); assert(seq_visual_image[609] == 15);
    seq_transport_stop(test_now);
    /* Both composed pitch sources retain the edited rhythm in project storage. */
    for (uint8_t kind = 0; kind < 1; kind++) {
        seq_module_t module; seq_module_default(&module, kind);
        module.composed = true; module.rhythm = (seq_rhythm_component_t){.kind=2,.enabled=1,.steps=16,.fills=4,.probability=100};
        seq_module_install(0, &module);
        rhythm_request(0, 1, 7, 4); assert(!backup_status);
        seq_module_export(0, &module); assert(seq_module_rhythm(&module).fills == 7);
        uint8_t bytes[SEQ_TRANSFER_CAPACITY]; uint16_t size = sizeof(bytes);
        assert(seq_project_object(1, bytes, &size, false, false));
        rhythm_request(0, 1, 2, 7); assert(!backup_status);
        assert(seq_project_restore_object(1, bytes, size));
        seq_module_export(0, &module); assert(seq_module_rhythm(&module).fills == 7);
    }
}
static void print_euclid_gesture(void) {
    key(KC_LCTL, true); key(KC_LALT, true); key(KC_LALT, false); key(KC_LCTL, false);
}

static void test_euclid_print(void) {
    for (uint8_t mode = 0; mode < 2; mode++) for (uint8_t composed = 0; composed < 2; composed++) {
        reset(); seq_enter(test_now); seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
        seq_euclid_mode_enter(test_now);
        track_length_steps[0] = 32;
        seq_modules[0].composed = composed; seq_modules[0].rhythm.kind = 2;
        seq_euclid_track_t *e = &euclid_tracks[0];
        e->steps = 7; e->fills = 3; e->rotate = 2; e->pitch_mode = mode;
        e->note_count = 3; e->palette_root = 0; e->note_pool[0] = 0; e->note_pool[1] = 4; e->note_pool[2] = 7;
        e->palette_pin = 255; e->swing_amount = 11; e->probability = 100;
        seq_accumulators[0].enabled = false;
        seq_scale_tracks[0].mode = 1; seq_scale_tracks[0].root = 0; seq_scale_tracks[0].kind = 1;
        assert(mel_track_add_note(0, 0, 9, 61, 80));
        assert(mel_track_add_note(0, 0, 9, 64, 90));
        assert(mel_track_add_note(1, 1, 1, 70, 100));
        uint8_t expected[32][16] = {{0}}, velocities[32][128] = {{0}};
        for (uint16_t step = 0; step < 32; step++) {
            seq_projected_step_notes(0, step % e->steps, expected[step]);
            virtual_midi_clear_events();
            seq_transport_start(test_now, step % e->steps);
            for (uint16_t i = 0; i < virtual_midi_state()->event_count; i++) {
                const virtual_midi_event_t *event = &virtual_midi_state()->events[i];
                if (event->kind == VIRTUAL_MIDI_EVENT_NOTE_ON && event->channel == midi_channel_for_track(0)) velocities[step][event->note] = event->velocity;
            }
            seq_transport_stop(test_now);
        }
        seq_transport_start(test_now, 5); uint32_t position = seq_step_position;
        e->probability = 0; seq_accumulators[0].enabled = true;
        print_euclid_gesture();
        assert(!e->enabled && !euclid_mode_active && !euclid_edit.active && !seq_accumulators[0].enabled);
        assert(seq_state.running && seq_step_position == position && mel_track_event_count(1) == 1);
        assert(e->note_count == 3 && e->palette_root == 0 && e->steps == 7 && e->fills == 3 && e->probability == 0);
        uint16_t total = 0;
        for (uint16_t step = 0; step < 32; step++) {
            uint8_t actual[16] = {0}; seq_projected_step_notes(0, step, actual);
            assert(!memcmp(actual, expected[step], sizeof(actual)));
            for (uint8_t i = 0; i < mel_track_event_count(0); i++) {
                mel_event_snapshot_t event; assert(mel_track_event_at(0, i, &event));
                if (event.start_step != step) continue;
                assert(event.end_step == step); total++;
                assert(event.velocity * melodic_track_velocity[0] / 127u == velocities[step][event.pitch]);
            }
        }
        assert(total == mel_track_event_count(0) && total > 0);
        seq_transport_stop(test_now); seq_dirty = true; seq_storage_save_if_dirty(); assert(!seq_dirty);
        uint8_t count = mel_track_event_count(0); mel_track_reset(0); seq_storage_load();
        assert(mel_track_event_count(0) == count && !euclid_tracks[0].enabled);
        /* Printed gaps accept ordinary manual notes. */
        assert(mel_track_add_note(0, 31, 31, 80, 100));
    }
}

static void test_euclid_print_capacity_and_gestures(void) {
    reset(); seq_enter(test_now); seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC; seq_euclid_mode_enter(test_now);
    track_length_steps[0] = 256; euclid_tracks[0].steps = 16; euclid_tracks[0].fills = 16;
    euclid_tracks[0].palette_pin = 0;
    assert(mel_track_add_note(0, 0, 0, 72, 90));
    seq_dirty = false; print_euclid_gesture();
    assert(euclid_mode_active && euclid_tracks[0].enabled && mel_track_event_count(0) == 1 && !seq_dirty && drum_pool_full_preview);
    mel_event_snapshot_t event; assert(mel_track_event_at(0, 0, &event) && event.pitch == 72);
    track_length_steps[0] = 128; print_euclid_gesture();
    assert(mel_track_event_count(0) == 128 && !euclid_tracks[0].enabled && !euclid_mode_active);
    seq_euclid_mode_enter(test_now); euclid_tracks[0].fills = 0;
    print_euclid_gesture(); assert(!mel_track_event_count(0) && !euclid_tracks[0].enabled);
    seq_euclid_mode_enter(test_now); record_state.armed = true;
    print_euclid_gesture(); assert(euclid_mode_active && euclid_tracks[0].enabled);
    record_state.armed = false;
    key(KC_LCTL, true); key(KC_LALT, true); key(KC_LSFT, true); key(KC_R, true); key(KC_R, false);
    key(KC_LSFT, false); key(KC_LALT, false); key(KC_LCTL, false);
    assert(euclid_mode_active && euclid_tracks[0].enabled && euclid_tracks[0].palette_root == 5);
    key(KC_LCTL, true); key(KC_LALT, true); key(KC_W, true); key(KC_W, false); key(KC_LALT, false); key(KC_LCTL, false);
    assert(euclid_mode_active && euclid_tracks[0].enabled);
    key(KC_LALT, true); key(KC_LALT, false); assert(!euclid_mode_active && euclid_tracks[0].enabled);
    /* Drum printing retains its drum type and leaves the edit session. */
    view_slot = 1; seq_euclid_mode_enter(test_now); print_euclid_gesture();
    assert(!euclid_mode_active && !slot_is_melodic(1) && !seq_euclid_selected(1, false)->enabled);
}

static void test_print_lengths(void) {
    const uint16_t lengths[] = {5, 16, 19, 32};
    const uint16_t cycles[] = {16, 16, 7, 8};
    for (uint8_t c = 0; c < 4; c++) {
        reset(); seq_enter(test_now); seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
        seq_euclid_mode_enter(test_now); track_length_steps[0] = lengths[c];
        seq_euclid_track_t *e = &euclid_tracks[0];
        e->steps = cycles[c]; e->fills = cycles[c]; e->note_count = 1; e->palette_root = 0; e->note_pool[0] = 0;
        e->palette_pin = 0;
        assert(mel_track_add_note(0, 1, 1, 77, 90));
        print_euclid_gesture();
        assert(!euclid_mode_active && mel_track_event_count(0) == lengths[c]);
        for (uint8_t i = 0; i < mel_track_event_count(0); i++) {
            mel_event_snapshot_t event; assert(mel_track_event_at(0, i, &event));
            assert(event.start_step == i && event.end_step == i);
            assert(event.pitch == (i % cycles[c] == 1 ? 77 : seq_melodic_note_pitch(0, 0)));
        }
    }
}

static void test_drum_print(void) {
    const uint16_t lengths[] = {5, 19, SEQ_MAX_STEPS};
    for (uint8_t c = 0; c < 3; c++) {
        reset(); seq_enter(test_now); seq_euclid_mode_enter(test_now);
        track_length_steps[0] = lengths[c];
        seq_euclid_track_t *e = seq_euclid_selected(0, true);
        e->steps = 8; e->fills = 3; e->rotate = 1; e->probability = 0;
        assert(seq_drum_step_set(0, 0, 2, true));
        seq_drum_step_mod_t *mod = seq_drum_step_mod_ensure(0, 0, 2); assert(mod);
        mod->config = 9;
        drum_active_note[0] = 1; seq_euclid_enable(0, true);
        seq_euclid_track_t sibling_state = *seq_euclid_selected(0, false);
        assert(seq_drum_step_set(0, 1, 3, true));
        drum_active_note[0] = 0;
        uint8_t expected[SEQ_DRUM_STORAGE_BYTES] = {0};
        for (uint16_t step = 0; step < lengths[c]; step++) if (seq_drum_step_get(0, 0, step)) expected[step >> 3] |= 1u << (step & 7);
        seq_state.running = true; seq_step_position = 3;
        ratchet_events[0] = (seq_ratchet_event_t){.active=true,.channel=0,.note=0};
        ratchet_events[1] = (seq_ratchet_event_t){.active=true,.channel=0,.note=1};
        drum_note_gate_mask[0] = 3;
        print_euclid_gesture();
        assert(!euclid_mode_active && !e->enabled && seq_state.running && seq_step_position == 3);
        assert(!ratchet_events[0].active && ratchet_events[1].active && drum_note_gate_mask[0] == 2);
        seq_drum_lane_storage_t *lane = seq_drum_pool_lane(0, 0);
        assert(!memcmp(lane->data, expected, sizeof(expected)) && seq_drum_step_mod_lookup(0, 0, 2)->config == 9);
        assert(!memcmp(seq_drum_euclid_state(0, 1), &sibling_state, sizeof(sibling_state)) && seq_drum_pool_step_get(0, 1, 3));
        assert(seq_drum_step_set(0, 0, 2, false) && !seq_drum_step_get(0, 0, 2));
        seq_state.running = false; seq_dirty = true; seq_storage_save_if_dirty(); assert(!seq_dirty);
        seq_drum_pool_clear_track(0); seq_storage_load();
        assert(!seq_drum_euclid_state(0, 0)->enabled && !seq_drum_step_get(0, 0, 2));
    }
    /* Printing an inherited generator must freeze its routed hits before making an override. */
    reset(); seq_enter(test_now); euclid_mode_active = true;
    euclid_tracks[0].enabled = true; euclid_tracks[0].steps = 16; euclid_tracks[0].fills = 8;
    euclid_tracks[0].note_count = 2; euclid_tracks[0].note_pool[0] = 0; euclid_tracks[0].note_pool[1] = 1;
    euclid_tracks[0].palette_pin = 255;
    uint16_t expected = seq_drum_window_mask(0, 0, 0);
    print_euclid_gesture();
    assert(!euclid_mode_active && !seq_drum_euclid_state(0, 0)->enabled && seq_drum_euclid_state(0, 1)->enabled);
    assert(seq_drum_window_mask(0, 0, 0) == expected);
    /* A full lane pool cannot partially commit a previously unallocated lane. */
    reset(); seq_enter(test_now); euclid_mode_active = true;
    euclid_tracks[0].enabled = true; euclid_tracks[0].steps = 16; euclid_tracks[0].fills = 4;
    for (uint8_t t = 1; t <= 4; t++) for (uint8_t n = 0; n < 12; n++) assert(seq_drum_pool_step_set(t, n, 0, true));
    seq_dirty = false; print_euclid_gesture();
    assert(euclid_mode_active && euclid_tracks[0].enabled && !seq_drum_pool_lane(0, 0) && !seq_dirty && drum_pool_full_preview);
}

static void test_palette_remove(void) {
    reset(); seq_enter(test_now); seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    seq_euclid_mode_enter(test_now); euclid_tracks[0].fills = 16;
    seq_euclid_track_t *e = &euclid_tracks[0];
    e->palette_root = 5; e->note_count = 3;
    e->note_pool[0] = 0; e->note_pool[1] = -5; e->note_pool[2] = 2; e->palette_pin = 2;
    assert(mel_track_add_note(0, 1, 1, 73, 100));
    seq_transport_start(test_now, 0); assert(melodic_active_any(0));
    key(KC_LALT, true); key(KC_DEL, true);
    key(KC_1, true); key(KC_1, false);
    assert(e->note_count == 2 && e->note_pool[0] == -5 && e->note_pool[1] == 2 && e->note_pool[2] == 0);
    assert(e->palette_root == 5 && e->palette_pin == 1 && e->palette_authored && !melodic_active_any(0));
    assert(seq_state.running && e->fills == 16 && mel_track_event_count(0) == 1 && euclid_tracks[1].note_count == 1);
    key(KC_8, true); key(KC_8, false); assert(e->note_count == 2);
    record_state.armed = true;
    key(KC_1, true); key(KC_1, false); assert(e->note_count == 2);
    record_state.armed = false;
    key(KC_DEL, false); key(KC_LALT, false); assert(euclid_mode_active);
    uint8_t remove[12] = {3, 1, 2, 5, (uint8_t)-5, 2};
    seq_backup_request(18, 1, 0, remove, sizeof(remove)); assert(backup_status == 2);
    seq_backup_request(18, 0, 0, remove, 2); assert(backup_status == 3);
    remove[4] = 0; seq_backup_request(18, 0, 0, remove, sizeof(remove)); assert(backup_status == 2);
    remove[4] = (uint8_t)-5;
    seq_backup_request(18, 0, 0, remove, sizeof(remove));
    assert(!backup_status && e->note_count == 1 && e->palette_pin == 255 && e->note_pool[0] == -5);
    seq_backup_request(18, 0, 0, remove, sizeof(remove)); assert(backup_status == 2);
    seq_transport_stop(test_now);
    seq_dirty = true; seq_storage_save_if_dirty(); assert(!seq_dirty);
    e->note_pool[0] = 0; seq_storage_load(); assert(e->note_pool[0] == -5 && e->palette_root == 5);
    seq_euclid_mode_enter(test_now);
    key(KC_LALT, true); key(KC_DEL, true); key(KC_1, true); key(KC_1, false);
    key(KC_LALT, false); key(KC_DEL, false);
    assert(!e->note_count && e->palette_pin == 255 && euclid_mode_active);
    seq_euclid_enable(0, false); seq_euclid_enable(0, true); assert(!e->note_count);
    seq_backup_request(17, 0, 0, NULL, 0);
    assert((seq_visual_image[958] & 8) && seq_visual_image[959] == 5);
}

static void test_euclid_single_tap_exit(void) {
    reset(); seq_enter(test_now);
    key(KC_LALT, true); key(KC_LALT, false); assert(!euclid_mode_active);
    test_now += 50;
    key(KC_LALT, true); key(KC_LALT, false); assert(euclid_mode_active);
    key(KC_LALT, true); key(KC_W, true); key(KC_W, false); key(KC_LALT, false);
    assert(euclid_mode_active);
    key(KC_LALT, true); assert(seq_euclid_apply_encoder(1, test_now)); key(KC_LALT, false);
    assert(euclid_mode_active);
    key(KC_LALT, true); key(KC_LALT, false);
    assert(!euclid_mode_active && !euclid_edit.active && !cmd_state.tap_count);
    key(KC_LALT, true); key(KC_LALT, false); assert(!euclid_mode_active);
    key(KC_LALT, true); key(KC_LALT, false); assert(euclid_mode_active);
    /* Existing deliberate generator reset still requires Delete + double tap. */
    key(KC_DEL, true);
    key(KC_LALT, true); key(KC_LALT, false); assert(euclid_mode_active);
    key(KC_LALT, true); key(KC_LALT, false); assert(!euclid_mode_active);
    key(KC_DEL, false);
}

static void test_palette_clear(void) {
    reset(); seq_enter(test_now); seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    seq_euclid_mode_enter(test_now); seq_euclid_enable(0, true);
    euclid_tracks[0].fills = 16; euclid_tracks[0].palette_pin = 0;
    seq_accumulators[0].enabled = false;
    assert(mel_track_add_note(0, 1, 2, 73, 100));
    seq_transport_start(test_now, 0); assert(melodic_active_any(0));
    key(KC_LALT, true); key(KC_LSFT, true);
    key(KC_R, true); key(KC_R, false);
    assert(euclid_palette_capture.active);
    virtual_midi_clear_events();
    key(KC_DEL, true); key(KC_DEL, false);
    assert(!euclid_palette_capture.active && !euclid_tracks[0].note_count);
    assert(euclid_tracks[0].palette_authored && euclid_tracks[0].palette_pin == 255);
    assert(!(seq_modifiers & SEQ_MOD_DEL) && events_of(VIRTUAL_MIDI_EVENT_NOTE_OFF) > 0 && !melodic_active_any(0));
    assert(seq_state.running && euclid_tracks[0].fills == 16 && mel_track_event_count(0) == 1 && euclid_tracks[1].note_count == 1);
    key(KC_LSFT, false); key(KC_LALT, false);
    assert(!euclid_tracks[0].note_count); /* Releasing cannot resurrect pending F. */
    seq_euclid_enable(0, false); seq_euclid_enable(0, true);
    assert(!euclid_tracks[0].note_count);
    seq_euclid_palette_pin_set(0, 0, test_now); assert(euclid_tracks[0].palette_pin == 255);
    for (uint8_t step = 1; step < 16; step++) {
        seq_play_step(test_now += 125);
        assert(melodic_active_any(0) == (step == 1 || step == 2));
        if (step == 1 || step == 2) assert(melodic_active_contains(0, 73));
    }
    seq_backup_request(17, 0, 0, NULL, 0);
    assert(seq_visual_image[897] == 0 && seq_visual_image[898] == 255 && (seq_visual_image[958] & 4));
    for (uint8_t i = 0; i < 16; i++) {
        assert(seq_visual_image[936 + i] == 255);
        for (uint8_t n = 0; n < 128; n++) assert(!!(seq_visual_image[640 + i * 16 + n / 8] & (1u << (n % 8))) == ((i == 1 || i == 2) && n == 73));
    }
    seq_transport_stop(test_now);
    for (uint8_t mode = 0; mode < 2; mode++) for (uint8_t composed = 0; composed < 2; composed++) {
        seq_module_t module, decoded; seq_module_export(0, &module);
        module.rhythm = seq_module_rhythm(&module); module.composed = composed; module.config.euclid.pitch_mode = mode;
        uint8_t data[SEQ_TRANSFER_CAPACITY]; uint16_t size = sizeof(data);
        assert(seq_module_encode(&module, data, &size) && seq_module_decode(&decoded, data, size));
        assert(!decoded.config.euclid.count && decoded.config.euclid.pin == 255);
        seq_module_install(0, &decoded);
        size = sizeof(data); assert(seq_project_object(1, data, &size, false, false));
        seq_euclid_reset_track(0);
        assert(seq_project_restore_object(1, data, size) && !euclid_tracks[0].note_count);
        uint8_t pitches[SEQ_MELODIC_KEY_COUNT]; assert(!seq_melodic_euclid_fallback(0, 0, pitches));
    }
    seq_dirty = true; seq_storage_save_if_dirty(); assert(!seq_dirty);
    seq_euclid_reset_track(0); seq_storage_load(); assert(!euclid_tracks[0].note_count);
    seq_euclid_mode_enter(test_now);
    key(KC_LALT, true); key(KC_LSFT, true);
    key(KC_T, true); key(KC_T, false); /* Can repopulate while still holding modifiers. */
    key(KC_DEL, true); key(KC_DEL, false);
    key(KC_R, true); key(KC_R, false);
    key(KC_LSFT, false); key(KC_LALT, false);
    assert(euclid_tracks[0].note_count == 1 && euclid_tracks[0].palette_root == 5);
    key(KC_LALT, true); key(KC_LSFT, true);
    record_state.armed = true;
    key(KC_DEL, true); key(KC_DEL, false);
    assert(euclid_tracks[0].note_count == 1 && !(seq_modifiers & SEQ_MOD_DEL));
    record_state.armed = false;
    key(KC_LSFT, false); key(KC_LALT, false);
    /* On drum tracks the gesture must not clear the melodic palette. */
    view_slot = 1;
    key(KC_LALT, true); key(KC_LSFT, true);
    key(KC_DEL, true); key(KC_DEL, false);
    key(KC_LSFT, false); key(KC_LALT, false);
    assert(euclid_tracks[0].note_count == 1 && euclid_tracks[1].note_count == 1);
}
static void test_palette_replace_shortcut(void) {
    reset(); seq_enter(test_now); seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    seq_euclid_mode_enter(test_now);
    assert(!euclid_tracks[0].palette_authored && !seq_palette_replace[0]);
    seq_backup_request(17, 0, 0, NULL, 0);
    assert((seq_visual_image[958] & 15) == 15);
    /* Merely holding the shortcut must not consume the starter distinction. */
    key(KC_LALT, true); key(KC_LSFT, true);
    key(KC_LSFT, false); key(KC_LALT, false);
    assert(!euclid_tracks[0].palette_authored);
    key(KC_LALT, true); key(KC_LSFT, true);
    key(KC_R, true); key(KC_R, false); /* F replaces default C. */
    key(KC_Y, true); key(KC_Y, false); /* A */
    assert(euclid_palette_capture.replace && euclid_palette_capture.retained == 0);
    assert(!euclid_tracks[0].palette_authored); /* Pending until release. */
    key(KC_LSFT, false); key(KC_LALT, false);
    assert(euclid_tracks[0].palette_authored && euclid_tracks[0].palette_root == 5);
    assert(euclid_tracks[0].note_count == 2 && euclid_tracks[0].note_pool[1] == 4);
    assert(!seq_palette_replace[0]);
    key(KC_LALT, true); key(KC_LSFT, true);
    key(KC_T, true); key(KC_T, false); /* Subsequent Add preserves F and A. */
    key(KC_LSFT, false); key(KC_LALT, false);
    assert(euclid_tracks[0].note_count == 3 && euclid_tracks[0].note_pool[2] == 2);
    /* Either Ctrl forces replacement; releasing it early cannot turn this into Add. */
    const uint8_t controls[] = {MOD_BIT(KC_LCTL), MOD_BIT(KC_RCTL)};
    for (uint8_t i = 0; i < 2; i++) {
        euclid_tracks[0].palette_pin = 0;
        key(KC_LALT, true); key(KC_LSFT, true);
        assert(key(i ? KC_RCTL : KC_LCTL, true));assert(seq_ctrl_mask == controls[i]);
        key(KC_Q, true); key(KC_Q, false);
        assert(key(i ? KC_RCTL : KC_LCTL, false));assert(!seq_ctrl_mask);
        seq_backup_request(17, 0, 0, NULL, 0);
        assert(seq_visual_image[956] == 3 && seq_visual_image[957] == 0 && (seq_visual_image[958] & 15) == 13);
        key(KC_LSFT, false); key(KC_LALT, false);
        assert(euclid_tracks[0].palette_authored && euclid_tracks[0].palette_root == 0);
        assert(euclid_tracks[0].note_count == 1 && euclid_tracks[0].palette_pin == 255 && !seq_palette_replace[0]);
        key(KC_LALT, true); key(KC_LSFT, true);
        key(KC_R, true); key(KC_R, false);
        key(KC_LSFT, false); key(KC_LALT, false);
        assert(euclid_tracks[0].note_count == 2 && euclid_tracks[0].note_pool[1] == 5); /* Deliberate C retained. */
    }
}
static void test_palette_provenance_storage(void) {
    reset(); seq_enter(test_now); seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    uint8_t bytes[SEQ_TRANSFER_CAPACITY], module_bytes[SEQ_MODULE_WIRE_MAX];
    uint16_t size = sizeof(bytes), module_size = sizeof(module_bytes);
    seq_module_t module;
    seq_module_export(0, &module);
    assert(seq_module_encode(&module, module_bytes, &module_size));
    assert(seq_project_object(1, bytes, &size, false, false));
    uint16_t marker = size - module_size - 3;
    assert(bytes[marker] == 'P' && bytes[marker + 1] == 1 && bytes[marker + 2] == 0);
    euclid_tracks[0].palette_authored = true;
    assert(seq_project_restore_object(1, bytes, size) && !euclid_tracks[0].palette_authored);
    seq_dirty = true; seq_storage_save_if_dirty(); assert(!seq_dirty);
    euclid_tracks[0].palette_authored = true;
    seq_storage_load(); assert(!euclid_tracks[0].palette_authored);
    /* Malformed provenance is rejected before any live state is changed. */
    bytes[marker + 1] = 2;
    euclid_tracks[0].palette_authored = true;
    assert(!seq_project_restore_object(1, bytes, size) && euclid_tracks[0].palette_authored);
    bytes[marker + 1] = 1; bytes[marker + 2] = 1;
    assert(!seq_project_restore_object(1, bytes, size));
    bytes[marker + 2] = 0;
    assert(!seq_project_restore_object(1, bytes, marker + 3));
    /* Legacy projects cannot distinguish defaults from intentional C: preserve them. */
    memmove(bytes + marker, bytes + marker + 3, module_size);
    assert(seq_project_restore_object(1, bytes, size - 3) && euclid_tracks[0].palette_authored);
    assert(seq_project_restore_object(1, bytes, marker) && euclid_tracks[0].palette_authored);
    seq_euclid_reset_track(0); assert(!euclid_tracks[0].palette_authored);
    seq_module_install(0, &module); assert(euclid_tracks[0].palette_authored);
    size = sizeof(bytes);
    assert(seq_project_object(1, bytes, &size, false, false));
    euclid_tracks[0].palette_authored = false;
    assert(seq_project_restore_object(1, bytes, size) && euclid_tracks[0].palette_authored);
    seq_euclid_reset_track(0);
    seq_euclid_set_melodic_pool(0, 1, 0); assert(euclid_tracks[0].palette_authored);
}
static void test_palette_append(void) {
    reset(); seq_enter(test_now); seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    seq_euclid_mode_enter(test_now);
    euclid_tracks[0].palette_authored = true;
    euclid_tracks[0].palette_root = 5;
    euclid_tracks[0].note_count = 2;
    euclid_tracks[0].note_pool[0] = 0;
    euclid_tracks[0].note_pool[1] = 4;
    euclid_tracks[0].palette_pin = 1;
    key(KC_LALT, true); key(KC_LSFT, true);
    key(KC_R, true); key(KC_R, false); /* F already saved. */
    key(KC_T, true); key(KC_T, false); /* Append G relative to F, not a new root. */
    key(KC_T, true); key(KC_T, false);
    assert(euclid_tracks[0].note_count == 2);
    seq_backup_request(17, 0, 0, NULL, 0);
    assert(seq_visual_image[956] == 1 && seq_visual_image[957] == 2);
    assert(seq_visual_image[900] == 3 && seq_visual_image[922] == 2);
    uint8_t replace[] = {2, 1};
    seq_backup_request(18, 0, 0, replace, 2);
    assert(backup_status == 2); /* Cannot change an in-progress capture. */
    key(KC_LSFT, false); key(KC_LALT, false);
    assert(euclid_tracks[0].note_count == 3 && euclid_tracks[0].palette_root == 5);
    assert(euclid_tracks[0].note_pool[0] == 0 && euclid_tracks[0].note_pool[1] == 4 && euclid_tracks[0].note_pool[2] == 2);
    assert(euclid_tracks[0].palette_pin == 1);
    /* One remaining slot: first unique pitch wins; a ninth never evicts notes. */
    euclid_tracks[0].palette_root = 0;
    euclid_tracks[0].note_count = 7;
    for (uint8_t i = 0; i < 7; i++) euclid_tracks[0].note_pool[i] = i;
    key(KC_LALT, true); key(KC_LSFT, true);
    key(KC_T, true); key(KC_T, false); /* 7 */
    key(KC_Y, true); key(KC_Y, false); /* 9, no space */
    seq_backup_request(17, 0, 0, NULL, 0);
    assert(seq_visual_image[956] == 5 && seq_visual_image[900] == 8 && seq_visual_image[957] == 7);
    key(KC_LSFT, false); key(KC_LALT, false);
    assert(euclid_tracks[0].note_count == 8 && euclid_tracks[0].note_pool[7] == 7);
    seq_accumulators[0].index = 3;
    key(KC_LALT, true); key(KC_LSFT, true);
    key(KC_Y, true); key(KC_Y, false);
    key(KC_LSFT, false); key(KC_LALT, false);
    assert(euclid_tracks[0].note_count == 8 && euclid_tracks[0].note_pool[7] == 7 && seq_accumulators[0].index == 3);
    seq_backup_request(18, 0, 0, replace, 2); assert(!backup_status);
    assert(seq_palette_replace[0] && !seq_palette_replace[1]);
    key(KC_LALT, true); key(KC_LSFT, true);
    key(KC_W, true); key(KC_W, false);
    key(KC_LSFT, false); key(KC_LALT, false);
    assert(euclid_tracks[0].note_count == 1 && euclid_tracks[0].palette_root == 2 && euclid_tracks[0].palette_pin == 255);
}
static void test_live_palette(void) {
    reset();
    seq_enter(test_now);
    seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    seq_euclid_mode_enter(test_now);
    euclid_tracks[0].steps = 16;
    euclid_tracks[0].fills = 16;
    uint8_t previous_count = euclid_tracks[0].note_count;
    int16_t previous_rotation = euclid_tracks[0].rotate;
    key(KC_LALT, true);
    key(KC_LSFT, true);
    key(KC_Q, true); key(KC_Q, false);
    key(KC_E, true); key(KC_E, false);
    key(KC_T, true); key(KC_T, false);
    key(KC_T, true); key(KC_T, false); /* Duplicate pitches are ignored. */
    seq_backup_request(17, 0, 0, NULL, 0);
    assert(!backup_status && backup_data[0] == 5);
    uint8_t *p = seq_visual_image + SEQ_RHYTHM_SIZE;
    assert((p[0] & 31) == 31 && p[1] == previous_count && p[4] == 3);
    assert(p[24] == 0 && p[25] == 4 && p[26] == 7);
    assert(euclid_tracks[0].rotate == previous_rotation);
    uint8_t token = seq_visual_token;
    key(KC_LSFT, false);
    key(KC_LALT, false);
    assert(!euclid_palette_capture.active && euclid_tracks[0].note_count == 3);
    assert(euclid_tracks[0].note_pool[1] == 4 && euclid_tracks[0].note_pool[2] == 7);
    seq_backup_request(17, token, 896, NULL, 0);
    assert(!backup_status && backup_size == 64 && backup_data[4] == 3); /* Frozen capture. */
    seq_backup_request(17, 0, 0, NULL, 0);
    assert(!(p[0] & 8) && p[1] == 3 && p[4] == 0);
    for (uint8_t i = 0; i < 16; i++) assert(p[40 + i] == i % 3);
    seq_euclid_palette_pin_set(0, 1, test_now);
    seq_backup_request(17, 0, 0, NULL, 0);
    for (uint8_t i = 0; i < 16; i++) assert(p[40 + i] == 1);
    seq_backup_request(16, seq_visual_token, 64, NULL, 0);
    assert(backup_status == 2);
    view_slot = 1;
    seq_backup_request(17, 0, 0, NULL, 0);
    assert(!p[0]);
    for (uint8_t i = 0; i < 16; i++) assert(p[40+i] == 255);
}
static void test_live_rhythm(void) {
    reset();
    euclid_tracks[0].pitch_mode = 1; /* Legacy transpose contract. */
    seq_enter(test_now);
    seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    track_length_steps[0] = 16;
    euclid_tracks[0].enabled = true;
    euclid_tracks[0].steps = 16;
    euclid_tracks[0].fills = 5;
    euclid_tracks[0].rotate = 2;
    euclid_tracks[0].note_count = 3;
    euclid_tracks[0].note_pool[0] = 0;
    euclid_tracks[0].note_pool[1] = 4;
    euclid_tracks[0].note_pool[2] = 7;
    euclid_tracks[0].palette_pin = 255;
    seq_accumulators[0].enabled = false;
    seq_backup_request(16, 0, 0, NULL, 0);
    assert(!backup_status && backup_data[0] == 4 && !seq_transfer_active);
    assert(seq_visual_image[610] == 16 && seq_visual_image[612] == 5 && seq_visual_image[614] == 2);
    uint8_t image[SEQ_RHYTHM_SIZE];
    memcpy(image, seq_visual_image, sizeof(image));
    uint8_t token = seq_visual_token;
    for (uint8_t step = 0; step < 16; step++) {
        int8_t offset = 0;
        bool hit = seq_euclid_step_hit(0, step, NULL, &offset);
        uint8_t pitch = seq_melodic_note_pitch(0, euclid_tracks[0].palette_root) + offset;
        for (uint8_t n = 0; n < 128; n++) assert(!!(image[640 + step * 16 + n / 8] & (1u << (n % 8))) == (hit && n == pitch));
    }
    euclid_tracks[0].fills = 9;
    view_slot = 1;
    seq_backup_request(16, token, 576, NULL, 0);
    assert(!backup_status && backup_data[36] == 5); /* Frozen settings despite editing. */
    seq_backup_request(14, token, 64, NULL, 0);
    assert(backup_status == 2);
    view_slot = 0;
    euclid_tracks[0].fills = 16;
    euclid_tracks[0].palette_pin = 1;
    assert(mel_track_add_note(0, 0, 3, 60, 100));
    assert(mel_track_add_note(0, 0, 3, 127, 100));
    seq_accumulators[0].enabled = true;
    seq_accumulators[0].index = 2;
    seq_backup_request(16, 0, 0, NULL, 0);
    assert(seq_visual_image[608] & 8);
    assert(seq_accumulators[0].index == 2);
    assert(seq_visual_image[640 + 64 / 8] & 1);
    assert(seq_visual_image[640 + 127 / 8] & 128);
    uint8_t fallback_pitch = seq_melodic_note_pitch(0, euclid_tracks[0].palette_root) + 4;
    for (uint8_t step = 4; step < 16; step++) assert(seq_visual_image[640 + step * 16 + fallback_pitch / 8] & (1u << (fallback_pitch % 8)));
    euclid_tracks[0].enabled = false;
    seq_backup_request(16, 0, 0, NULL, 0);
    assert(!(seq_visual_image[608] & 2));
    assert(seq_visual_image[640 + 60 / 8] & (1u << (60 % 8)));
    view_bar = 1;
    seq_backup_request(16, 0, 0, NULL, 0);
    for (uint16_t i = 640; i < SEQ_RHYTHM_SIZE; i++) assert(!seq_visual_image[i]);
}
static void test_components(void) {
    reset();
    seq_enter(test_now);
    seq_module_t module, decoded, sentinel;
    uint8_t      wire[SEQ_MODULE_WIRE_MAX], again[SEQ_MODULE_WIRE_MAX];
    for (uint8_t source = 0; source < 1; source++)
        for (uint8_t rhythm = 1; rhythm <= 2; rhythm++) {
            seq_module_default(&module, source);
            module.composed = true;
            module.rhythm   = (seq_rhythm_component_t){.kind = rhythm};
            if (rhythm == 2) module.rhythm = (seq_rhythm_component_t){.kind = 2, .enabled = 1, .steps = 16, .fills = 4, .probability = 100};
            uint16_t size = sizeof(wire), size2 = sizeof(again);
            assert(seq_module_encode(&module, wire, &size) && wire[1] == 2);
            assert(seq_module_decode(&decoded, wire, size) && decoded.composed && decoded.kind == source && decoded.rhythm.kind == rhythm);
            assert(seq_module_encode(&decoded, again, &size2) && size == size2 && !memcmp(wire, again, size));
            sentinel = decoded;
            for (uint16_t n = 0; n < size; n++) {
                assert(!seq_module_decode(&decoded, wire, n));
                assert(!memcmp(&decoded, &sentinel, sizeof(decoded)));
            }
            wire[3] = 2;
            assert(!seq_module_decode(&decoded, wire, size));
            wire[3]    = 1;
            wire[size] = 0;
            assert(!seq_module_decode(&decoded, wire, size + 1));
            assert(seq_project_restore_object(13, wire, size));
            seq_module_export(0, &decoded);
            size2 = sizeof(again);
            assert(seq_module_encode(&decoded, again, &size2) && size2 == size && !memcmp(wire, again, size));
        }
    seq_backup_request(15, 0, 0, NULL, 0);
    assert(!backup_status && backup_size == 16 && backup_data[1] == 4);
    seq_backup_request(15, 1, 0, NULL, 0);
    assert(backup_status == 3);
}
static void test_lane_euclid(void) {
    reset(); seq_enter(test_now);
    seq_euclid_enable(0, true);
    seq_euclid_adjust_fills(0, -1);
    seq_euclid_track_t *kick = seq_euclid_selected(0, false);
    assert(kick->fills == 3 && !euclid_tracks[0].enabled);
    drum_active_note[0] = 2;
    seq_euclid_enable(0, true);
    seq_euclid_adjust_steps(0, -4);
    seq_euclid_adjust_fills(0, 1);
    seq_euclid_adjust_rotate(0, 2);
    seq_euclid_adjust_probability(0, -30);
    seq_euclid_adjust_swing(0, 12);
    seq_euclid_track_t *hat = seq_euclid_selected(0, false);
    assert(hat != kick && hat->steps == 12 && hat->fills == 5 && hat->rotate == 2);
    assert(hat->probability == 70 && hat->swing_amount == 12);
    assert(kick->steps == 16 && kick->fills == 3 && kick->probability == 100 && !kick->rotate);
    assert(!seq_euclid_selected(1, false)->enabled);
    assert(seq_drum_pool_used() == 2 && seq_drum_has_activity(0) && seq_euclid_any_enabled());
    seq_drum_pool_prune(0, 2); assert(seq_drum_pool_used() == 2);
    hat->probability = 100;
    for (uint16_t step = 0; step < 16; step++) {
        virtual_midi_clear_events();
        if (!step) seq_transport_start(test_now, 0); else seq_play_step(test_now += 125);
        unsigned expected = seq_drum_euclid_hit(0, 0, step, NULL) + seq_drum_euclid_hit(0, 2, step, NULL);
        assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == expected);
    }
    seq_transport_stop(test_now);
    seq_visual_capture(17);
    assert(seq_visual_image[619] == 2 && seq_visual_image[612] == 5 && seq_visual_image[610] == 12);
    uint8_t edit[] = {1, 6, 0, 5, 0};
    seq_backup_request(19, 0, 2, edit, 5); assert(backup_status == 2);
    seq_backup_request(19, 0, 6, edit, 5); assert(!backup_status && hat->fills == 6 && kick->fills == 3);
    seq_drum_step_set(0, 2, 3, true);
    uint8_t bytes[SEQ_TRANSFER_CAPACITY], again[SEQ_TRANSFER_CAPACITY]; uint16_t size = sizeof(bytes), size2;
    assert(seq_project_object(1, bytes, &size, false, false));
    seq_euclid_reset_track(0);
    assert(seq_project_restore_object(1, bytes, size));
    assert(seq_drum_pool_used() == 2 && seq_drum_euclid_state(0, 0)->fills == 3 && seq_drum_euclid_state(0, 2)->fills == 6);
    size2 = sizeof(again); assert(seq_project_object(1, again, &size2, false, false));
    assert(size2 == size && !memcmp(bytes, again, size));
    assert(bytes[105] == 'L');
    for (uint16_t truncated = 106; truncated < 161; truncated++) {
        uint16_t check = truncated;
        assert(!seq_project_object(1, bytes, &check, true, false));
    }
    seq_modules[0].composed = true; seq_modules[0].rhythm.kind = 1;
    assert(!seq_drum_euclid_hit(0, 0, 0, NULL));
    seq_modules[0].composed = false;
    seq_storage_save_if_dirty();
    seq_euclid_reset_track(0); seq_storage_load();
    assert(seq_drum_euclid_state(0, 2)->fills == 6);
    seq_euclid_reset_selected(0);
    assert(!seq_drum_euclid_hit(0, 2, 0, NULL) && seq_drum_pool_step_get(0, 2, 3));
    assert(seq_drum_euclid_state(0, 0)->fills == 3);
    /* Normal lane clear removes manual notes AND its generator. */
    key(KC_DEL, true); key(KC_F1, true); key(KC_F1, false); key(KC_DEL, false);
    assert(!seq_drum_pool_lane(0, 2) && seq_drum_euclid_state(0, 0)->fills == 3);
}

static void test_lane_euclid_legacy_capacity(void) {
    reset(); seq_enter(test_now);
    euclid_tracks[0].enabled = true; euclid_tracks[0].fills = 8;
    euclid_tracks[0].note_count = 2; euclid_tracks[0].note_pool[0] = 0; euclid_tracks[0].note_pool[1] = 2;
    uint16_t old[12] = {0};
    for (uint8_t n = 0; n < 12; n++) old[n] = seq_drum_window_mask(0, n, 0);
    uint8_t bytes[SEQ_TRANSFER_CAPACITY]; uint16_t size = sizeof(bytes);
    assert(seq_project_object(1, bytes, &size, false, false));
    seq_euclid_reset_track(0); assert(seq_project_restore_object(1, bytes, size));
    for (uint8_t n = 0; n < 12; n++) assert(old[n] == seq_drum_window_mask(0, n, 0));
    drum_active_note[0] = 0; seq_euclid_adjust_fills(0, -3);
    assert(seq_drum_euclid_state(0, 0)->fills == 5 && seq_drum_window_mask(0, 2, 0) == old[2]);
    seq_euclid_reset_selected(0); assert(!seq_drum_window_mask(0, 0, 0) && seq_drum_window_mask(0, 2, 0) == old[2]);
    size = sizeof(bytes); assert(seq_project_object(1, bytes, &size, false, false));
    seq_euclid_reset_track(0); assert(seq_project_restore_object(1, bytes, size));
    assert(!seq_drum_window_mask(0, 0, 0) && seq_drum_window_mask(0, 2, 0) == old[2]);
    /* Generator-only lanes consume pool slots and failed edits remain atomic. */
    reset(); seq_enter(test_now);
    for (uint8_t t = 0; t < 4; t++) for (uint8_t n = 0; n < 12; n++) {
        drum_active_note[t] = n; seq_euclid_enable(t, true);
    }
    assert(seq_drum_pool_used() == 48);
    seq_euclid_enable(4, true); seq_euclid_adjust_steps(4, -1); seq_euclid_adjust_fills(4, 1);
    assert(!seq_drum_pool_lane(4, drum_active_note[4]) && !euclid_tracks[4].enabled && euclid_tracks[4].steps == 16 && !euclid_tracks[4].fills);
    drum_active_note[0] = 0; seq_euclid_reset_selected(0);
    assert(seq_drum_pool_used() == 47);
    seq_euclid_enable(4, true); assert(seq_drum_pool_used() == 48);
    /* A two-lane object cannot replace a one-lane track in a full pool. */
    drum_active_note[0] = 1;
    size = sizeof(bytes); assert(seq_project_object(1, bytes, &size, false, false));
    assert(!seq_project_restore_object(5, bytes, size));
    assert(seq_drum_pool_used() == 48 && seq_euclid_selected(4, false)->fills == 4);
}
static void test_lane_euclid_gestures(void) {
    reset(); seq_enter(test_now); seq_euclid_mode_enter(test_now);
    key(KC_F1, true); key(KC_F3, true); key(KC_F3, false); key(KC_F1, false);
    assert(view_slot == 0 && drum_active_note[0] == 2 && euclid_mode_active);
    key(KC_W, true); key(KC_W, false); assert(seq_euclid_apply_encoder(1, test_now));
    assert(seq_drum_euclid_state(0, 2)->fills == 5 && seq_drum_euclid_state(0, 0)->fills == 4);
    key(KC_F2, true); key(KC_F2, false);
    assert(view_slot == 1 && seq_euclid_active_track() == 1);
    key(KC_Q, true); key(KC_Q, false); assert(seq_euclid_apply_encoder(-1, test_now));
    assert(seq_euclid_selected(1, false)->steps == 15 && seq_drum_euclid_state(0, 2)->steps == 16);
    key(KC_F1, true); key(KC_F1, false); assert(view_slot == 0 && drum_active_note[0] == 2);
    seq_euclid_mode_exit(true, test_now);
    assert(!seq_drum_euclid_hit(0, 2, 0, NULL) && seq_drum_euclid_state(0, 0)->fills == 4);
    /* Loop shortening clamps every lane, not just the viewed one. */
    drum_active_note[0] = 2; seq_euclid_enable(0, true);
    seq_track_set_steps(0, 8);
    assert(seq_drum_euclid_state(0, 0)->steps == 8 && seq_drum_euclid_state(0, 2)->steps == 8);
}
static void run_protocol_bridge(void) {
    reset();
    seq_enter(test_now);
    protocol_bridge = true;
    seq_drum_step_set(0, 0, 0, true);
    char line[512];
    while (fgets(line, sizeof(line), stdin)) {
        uint8_t  packet[128];
        uint16_t count = 0;
        for (size_t i = 0; line[i] && line[i] != '\n' && i + 1 < strlen(line); i += 2) {
            unsigned byte = 0;
            if (sscanf(line + i, "%2x", &byte) != 1 || count >= sizeof(packet)) break;
            packet[count++] = byte;
        }
        for (uint16_t offset = 0; offset < count; offset += 3)
            virtual_midi_receive_sysex(offset, count - offset > 3 ? 3 : count - offset, packet + offset);
    }
}
static void test_temp_snapshot_scratch(void) {
    reset(); seq_enter(test_now);
    for (uint8_t t = 0; t < 4; t++) {
        seq_slots[t].kind = SEQ_SLOT_KIND_MELODIC;
        for (uint8_t n = 0; n < MEL_TRACK_MAX_EVENTS; n++)
            assert(mel_track_add_note(t, n % 16, n % 16, n, 100));
    }
    seq_transport_start(test_now, 0);
    uint32_t position = seq_step_position, deadline = seq_next_step_ms;
    virtual_midi_clear_events();
    key(KC_ENT, true); key(KC_ESC, true); key(KC_ESC, false); key(KC_ENT, false);
    assert(seq_temp_active && seq_state.running);
    assert(seq_step_position == position && seq_next_step_ms == deadline);
    assert(!virtual_midi_state()->event_count);
    for (uint8_t t = 0; t < 4; t++) seq_clear_track(t, test_now);
    assert(seq_temp_restore(test_now));
    for (uint8_t t = 0; t < 4; t++) assert(mel_track_event_count(t) == MEL_TRACK_MAX_EVENTS);
    seq_matrix_scan();
    assert(seq_state.running);

    /* A codec request must not overwrite the rollback data of an active edit. */
    mel_track_edit_begin(0);
    assert(mel_track_remove_event(0, 0));
    uint8_t wire[SEQ_TRANSFER_CAPACITY]; uint16_t size = sizeof(wire);
    assert(!seq_project_object(1, wire, &size, false, false));
    assert(!seq_temp_begin(test_now) && !seq_temp_active && seq_state.running);
    assert(mel_track_add_note(0, 0, 0, 0, 100));
    assert(!mel_track_add_note(0, 0, 0, 0, 100));
    assert(!mel_track_edit_end());
    mel_event_snapshot_t first;
    assert(mel_track_event_at(0, 0, &first) && first.pitch == 0);
    assert(mel_track_event_count(0) == MEL_TRACK_MAX_EVENTS);
    assert(seq_temp_begin(test_now));
    seq_temp_commit();
}

static void test_temp_restore(void) {
    reset(); seq_enter(test_now);
    seq_slots[1].kind = SEQ_SLOT_KIND_MELODIC;
    seq_chord_mode_set(1, true);
    seq_chord_event_t chord = {.root=60,.type=0,.length=4,.velocity=100};
    assert(seq_chord_write(1, 0, chord, false));
    seq_drum_step_set(0, 0, 0, true);
    seq_drum_step_set(0, 2, 7, true);
    seq_automation_record(1, 0, 4, 90);
    cc_automation_manual[1] = true;
    melodic_last_mask[1] = 5;
    seq_scale_global = (seq_scale_context_t){1,2,1};
    seq_dirty = false; seq_save_status = 0;
    uint8_t before[13][SEQ_TRANSFER_CAPACITY]; uint16_t sizes[13];
    for (uint8_t i=0;i<13;i++) { sizes[i]=sizeof(before[i]); assert(seq_project_object(i,before[i],&sizes[i],false,false)); }
    seq_transport_start(test_now, 0);
    key(KC_ENT,true); key(KC_ESC,true); key(KC_ESC,false); key(KC_ENT,false);
    assert(seq_temp_active);
    seq_backup_request(17,0,0,NULL,0);assert((seq_visual_image[958]&112)==48);
    uint8_t saved[SEQ_TRANSFER_CAPACITY]; memcpy(saved,seq_transfer_buffer,sizeof(saved));
    seq_clear_track(1,test_now); seq_clear_track(0,test_now);
    drum_lane_muted[0][0]=true; melodic_track_muted[1]=true;
    seq_drum_step_set(4,8,12,true); bpm=170; seq_timing_swing=35;
    track_length_steps[0]=7; seq_scale_global.root=9; cc_values[1][0]=3;
    cc_automation_manual[1]=false; melodic_last_mask[1]=16;
    assert(seq_temp_active && !memcmp(saved,seq_transfer_buffer,sizeof(saved)));
    for (uint8_t cmd=1;cmd<=9;cmd++) { seq_backup_request(cmd,0,0,NULL,0); assert(backup_status==2); }
    assert(!memcmp(saved,seq_transfer_buffer,sizeof(saved)) && !seq_transfer_active);
    seq_state.running=false; seq_dirty=true; seq_storage_save_if_dirty();
    assert(!test_eeprom_valid && !memcmp(saved,seq_transfer_buffer,sizeof(saved)));
    seq_state.running=true;
    seq_play_step(test_now+=125); uint32_t position=seq_step_position;
    seq_scale_editing=true; euclid_mode_active=true;
    key(KC_ESC,true); key(KC_ESC,false);
    assert(!seq_temp_active && seq_state.running && seq_step_position==position);
    assert(!seq_scale_editing && !euclid_mode_active && !seq_dirty && seq_save_status==0);
    assert(melodic_last_mask[1]==5 && !melodic_track_muted[1] && !drum_lane_muted[0][0]);
    assert(seq_chord_mode[1] && seq_chord_count(1)==1 && mel_track_event_count(1)==3);
    for (uint8_t i=0;i<13;i++) {
        uint8_t after[SEQ_TRANSFER_CAPACITY]; uint16_t size=sizeof(after);
        assert(seq_project_object(i,after,&size,false,false));
        assert(size==sizes[i] && !memcmp(after,before[i],size));
    }
    assert(seq_track_step_position[0]==position%16);
    seq_play_step(test_now+=125);assert(melodic_active_any(1));
    seq_transport_stop(test_now);
}
static void test_temp_commit(void) {
    reset(); seq_enter(test_now);
    seq_drum_step_set(0, 0, 0, true);
    seq_transport_start(test_now, 0);
    key(KC_ENT, true); key(KC_ESC, true); key(KC_ESC, false); key(KC_ENT, false);
    assert(seq_temp_active);
    seq_drum_step_set(0, 0, 4, true);
    drum_lane_muted[0][0] = true;
    bpm = 150;
    seq_slots[1].kind = SEQ_SLOT_KIND_MELODIC;
    assert(mel_track_add_note(1, 0, 7, 60, 100));
    assert(mel_track_add_note(1, 0, 7, 64, 100));
    seq_play_step(test_now += 125);
    uint32_t position = seq_step_position, deadline = seq_next_step_ms;
    virtual_midi_clear_events();
    key(KC_ENT, true); key(KC_ESC, true);
    assert(!seq_temp_active && !seq_temp_length && seq_dirty);
    assert(seq_state.running && seq_step_position == position && seq_next_step_ms == deadline);
    assert(!virtual_midi_state()->event_count && !test_eeprom_valid);
    /* Repeated key-down and the key-up must not create a new snapshot. */
    key(KC_ESC, true); key(KC_ESC, false); key(KC_ENT, false);
    assert(!seq_temp_active && seq_drum_step_get(0, 0, 4) && drum_lane_muted[0][0]);
    assert(bpm == 150 && mel_track_event_count(1) == 2);
    /* The next TEMP uses the committed state as its new restore point. */
    key(KC_ENT, true); key(KC_ESC, true); key(KC_ESC, false); key(KC_ENT, false);
    assert(seq_temp_active);
    seq_clear_track(1, test_now); bpm = 90; drum_lane_muted[0][0] = false;
    key(KC_ESC, true); key(KC_ESC, false);
    assert(!seq_temp_active && bpm == 150 && drum_lane_muted[0][0] && mel_track_event_count(1) == 2);
    seq_transport_stop(test_now);
    seq_storage_save_if_dirty();
    assert(test_eeprom_valid && !seq_dirty);
}
static void queue_temp_restore(void) {
    key(KC_LSFT, true); key(KC_ESC, true); key(KC_ESC, false); key(KC_LSFT, false);
}
static void test_temp_quantized_controls(void) {
    reset(); seq_enter(test_now);
    assert(seq_temp_begin(test_now)); bpm = 150;
    queue_temp_restore(); /* No clock running: restore immediately. */
    assert(!seq_temp_active && !seq_temp_restore_pending && bpm == 120);
    seq_transport_start(test_now, 0);
    assert(seq_temp_begin(test_now)); bpm = 150;
    queue_temp_restore(); assert(seq_temp_active && seq_temp_restore_pending);
    seq_backup_request(17,0,0,NULL,0);assert((seq_visual_image[958]&112)==112);
    key(KC_LSFT, true); key(KC_ESC, true);
    assert(seq_temp_active && !seq_temp_restore_pending && bpm == 150);
    key(KC_ESC, true); /* Held-key repeats must not requeue. */
    assert(!seq_temp_restore_pending);
    key(KC_ESC, false); key(KC_LSFT, false);
    queue_temp_restore(); assert(seq_temp_active && seq_temp_restore_pending);
    key(KC_ESC, true); key(KC_ESC, false);
    assert(!seq_temp_active && !seq_temp_restore_pending && bpm == 120);
    assert(seq_temp_begin(test_now)); bpm = 160;
    queue_temp_restore();
    key(KC_ENT, true); key(KC_ESC, true); key(KC_ESC, false); key(KC_ENT, false);
    assert(!seq_temp_active && !seq_temp_restore_pending && bpm == 160);
    for (uint8_t i = 0; i < 16; i++) seq_play_step(test_now += 125);
    assert(bpm == 160);
    assert(seq_temp_begin(test_now)); bpm = 180; queue_temp_restore();
    seq_transport_stop(test_now);
    assert(!seq_temp_active && !seq_temp_restore_pending && bpm == 160 && !seq_state.running);
}
static void test_temp_pool_restore(void) {
    reset();seq_enter(test_now);
    for(uint8_t t=0;t<4;t++)for(uint8_t n=0;n<12;n++)seq_drum_step_set(t,n,0,true);
    assert(seq_drum_pool_used()==48 && seq_temp_begin(test_now));
    for(uint8_t t=0;t<4;t++)seq_clear_track(t,test_now);
    for(uint8_t t=4;t<8;t++)for(uint8_t n=0;n<12;n++)seq_drum_step_set(t,n,1,true);
    assert(seq_drum_pool_used()==48 && seq_temp_restore(test_now));
    for(uint8_t t=0;t<4;t++)for(uint8_t n=0;n<12;n++)assert(seq_drum_step_get(t,n,0));
    for(uint8_t t=4;t<8;t++)for(uint8_t n=0;n<12;n++)assert(!seq_drum_step_get(t,n,1));
}
static void test_temp_capacity(void) {
    reset(); seq_enter(test_now);
    for (uint8_t t=0;t<12;t++) {
        seq_slots[t].kind=SEQ_SLOT_KIND_MELODIC;
        for (uint8_t i=0;i<MEL_TRACK_MAX_EVENTS;i++) assert(mel_track_add_note(t,i,i,60,100));
    }
    assert(!seq_temp_begin(test_now) && !seq_temp_active);
    for (uint8_t t=0;t<12;t++) assert(mel_track_event_count(t)==MEL_TRACK_MAX_EVENTS);
    reset();seq_enter(test_now);
    seq_transfer_active=true;seq_transfer_buffer[0]=99;
    assert(!seq_temp_begin(test_now) && seq_transfer_buffer[0]==99);
    seq_transfer_active=false;
    assert(seq_temp_begin(test_now));
    seq_drum_step_set(0,0,1,true);
    seq_exit();
    assert(!seq_temp_active && !seq_state.active && !seq_drum_step_get(0,0,1));
}

static void test_desktop_shortcut(void) {
    reset(); seq_enter(test_now);
    assert(key(KC_LCTL, true));
    assert(key(KC_BSLS, true));
    assert(key(KC_LCTL, false));
    assert(key(KC_BSLS, false));
    assert(seq_state.active && !seq_state.running);
    seq_euclid_mode_enter(test_now);
    assert(key(KC_RCTL, true));
    assert(key(KC_BSLS, true));
    assert(key(KC_BSLS, false));
    assert(key(KC_RCTL, false));
    assert(euclid_mode_active);
    cc_mode.latched = true;
    assert(key(KC_LCTL, true));
    assert(key(KC_BSLS, true));
    assert(key(KC_BSLS, false));
    assert(key(KC_LCTL, false));
    assert(cc_mode.latched);
    cc_mode.latched = false;
    assert(!key(KC_BSLS, true));
    assert(!key(KC_BSLS, false));
}

static void test_mute_summary(void) {
    reset(); seq_enter(test_now);
    seq_slots[1].kind = SEQ_SLOT_KIND_MELODIC;
    melodic_track_muted[slot_melodic_track(1)] = true;
    drum_lane_muted[slot_drum_track(0)][3] = true;
    seq_visual_capture(17);
    assert(seq_visual_image[958] & 128);
    seq_visual_capture(21);
    assert(seq_visual_image[5] == 1);
    assert((seq_visual_image[6] & 2) && !(seq_visual_image[6] & 1));
    assert(seq_visual_image[8] & 1);
    for (uint8_t lane = 0; lane < SEQ_DRUM_NOTE_COUNT; lane++) drum_lane_muted[slot_drum_track(0)][lane] = true;
    seq_visual_capture(21);
    assert((seq_visual_image[6] & 1) && !(seq_visual_image[8] & 1));
    seq_slots[1].wraps_drum = true;
    seq_visual_capture(21);
    assert(!(seq_visual_image[6] & 2) && (seq_visual_image[8] & 2));
    seq_mute_clear_all(test_now);
    seq_visual_capture(21);
    assert(!(seq_visual_image[6] | seq_visual_image[7] | seq_visual_image[8] | seq_visual_image[9]));
}

static uint16_t health_word(uint8_t offset) {
    return backup_data[offset] | ((uint16_t)backup_data[offset + 1] << 8);
}

static void assert_project_health_size(void) {
    seq_backup_request(25, 0, 0, NULL, 0);
    assert(!backup_status && backup_size == SEQ_PROJECT_HEALTH_SIZE && (backup_data[1] & 16));
    uint16_t save_bytes = health_word(28), temp_bytes = health_word(32);
    uint16_t measured = 26;
    uint8_t object[SEQ_TRANSFER_CAPACITY];
    for (uint8_t i = 0; i <= 12; i++) {
        uint16_t size = sizeof(object);
        assert(seq_project_object(i, object, &size, false, false));
        measured += size;
    }
    assert(save_bytes == measured + 10 && temp_bytes == measured + sizeof(seq_temp_context_t));
}

static void test_project_health(void) {
    reset(); seq_enter(test_now);
    assert_project_health_size();
    seq_backup_request(25, 0, 0, NULL, 0);
    assert(!(backup_data[1] & 64) && (backup_data[1] & 32));
    seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    seq_chord_mode_set(0, true);
    seq_chord_event_t chord = {.root = 60, .type = SEQ_CHORD_CUSTOM, .length = 4, .velocity = 100, .inversion = 1, .quantize = 1, .quantize_root = 2, .quantize_kind = 1, .custom_count = 3, .intervals = {0, 4, 7}, .name = "Major"};
    assert(seq_chord_write(0, 0, chord, false));
    seq_drum_step_set(1, 2, 15, true);
    drum_active_note[1] = 2;
    seq_drum_step_mod_ensure(1, 2, 15)->config = 1;
    seq_automation_record(0, 1, 0, 80);
    seq_scale_global = (seq_scale_context_t){1, 2, 1};
    seq_scale_tracks[0] = (seq_scale_track_t){.mode = 1, .root = 1, .kind = 2};
    cc_automation_manual[0] = true;
    seq_euclid_enable(1, true);
    assert_project_health_size();
    seq_backup_request(25, 1, 0, NULL, 0);
    assert(backup_data[3] == 1 && backup_data[4] == 2 && backup_data[5] == 1);
    assert(health_word(8) == 0 && health_word(10) == 3 && health_word(12) == 1 && health_word(16) == 1 && health_word(24) == 1);
    seq_storage_save_if_dirty();
    assert(!seq_dirty && !seq_save_status);
    seq_backup_request(25, 0, 0, NULL, 0);
    assert(!(backup_data[1] & 1) && (backup_data[1] & 64));

    /* Telemetry cannot stop playback, select tracks, emit notes, save, prune
     * metadata, or overwrite either a transfer or the original TEMP drop. */
    mel_track_clear_range(0, 0, 4);
    assert(seq_chord_count(0) == 1);
    seq_transport_start(test_now, 0);
    uint16_t position = seq_step_position;
    uint8_t selected = view_slot;
    unsigned writes = test_eeprom_writes;
    virtual_midi_clear_events();
    memset(seq_transfer_buffer, 0xA5, SEQ_TEMP_CAPACITY);
    seq_backup_request(25, 1, 0, NULL, 0);
    assert(!backup_status && health_word(12) == 0 && seq_chord_count(0) == 1);
    assert(seq_state.running && position == seq_step_position && selected == view_slot && !seq_transfer_active);
    assert(!virtual_midi_state()->event_count && test_eeprom_writes == writes);
    for (uint16_t i = 0; i < SEQ_TEMP_CAPACITY; i++) assert(seq_transfer_buffer[i] == 0xA5);
    assert_project_health_size(); /* Size agrees with export after stale recipes are pruned. */
    assert(seq_temp_begin(test_now));
    uint32_t original = seq_project_checksum(seq_transfer_buffer, seq_temp_length);
    seq_drum_step_set(2, 1, 4, true);
    seq_backup_request(25, 0, 0, NULL, 0);
    assert((backup_data[1] & 2) && !(backup_data[1] & 32));
    assert(health_word(36) == seq_temp_length && seq_project_checksum(seq_transfer_buffer, seq_temp_length) == original);
    assert(seq_temp_restore(test_now) && !seq_drum_step_get(2, 1, 4));
    seq_transfer_active = true;
    seq_transfer_length = 10;
    seq_transfer_received = 7;
    memset(seq_transfer_buffer, 0xC3, 10);
    seq_backup_request(25, 0, 0, NULL, 0);
    assert((backup_data[1] & 8) && !(backup_data[1] & 32));
    assert(seq_transfer_length == 10 && seq_transfer_received == 7);
    for (uint8_t i = 0; i < 10; i++) assert(seq_transfer_buffer[i] == 0xC3);
    seq_transfer_active = false;
    mel_track_edit_begin(0);
    seq_backup_request(25, 0, 0, NULL, 0);
    assert(!backup_status && !(backup_data[1] & 16) && !(backup_data[1] & 32));
    assert(mel_track_edit_end());
    seq_backup_request(25, 12, 0, NULL, 0); assert(backup_status == 3);
    seq_backup_request(25, 0, 1, NULL, 0); assert(backup_status == 3);

    reset(); seq_enter(test_now);
    for (uint8_t t = 0; t < 12; t++) {
        seq_slots[t].kind = SEQ_SLOT_KIND_MELODIC;
        for (uint8_t i = 0; i < MEL_TRACK_MAX_EVENTS; i++) assert(mel_track_add_note(t, i, i, 60, 100));
    }
    assert_project_health_size();
    seq_backup_request(25, 0, 0, NULL, 0);
    assert(health_word(8) == MEL_TRACK_MAX_EVENTS && health_word(10) == 12 * MEL_TRACK_MAX_EVENTS);
    assert(health_word(28) > EECONFIG_USER_DATA_SIZE && health_word(32) > SEQ_TEMP_CAPACITY && !(backup_data[1] & 32));
    /* A valid EEPROM version marker is not proof of a valid project. A
     * subsequent oversized save must not turn corrupt storage into "saved". */
    test_eeprom_valid = true;
    memset(test_eeprom, 0, sizeof(test_eeprom));
    seq_storage_load();
    assert(seq_save_status == 3);
    seq_storage_save_if_dirty();
    seq_backup_request(25, 0, 0, NULL, 0);
    assert(backup_data[2] == 2 && (backup_data[1] & 1) && !(backup_data[1] & 64));
}

static void test_restore_chord_distribution(void) {
    reset();
    seq_enter(test_now);
    seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    seq_chord_mode_set(0, true);
    track_length_steps[0] = 32;
    track_length_bars[0] = 2;
    seq_chord_event_t chord = {.root = 60, .type = 0, .length = 1, .velocity = 100};
    for (uint8_t i = 0; i < 32; i++) assert(seq_chord_write(0, i, chord, false));
    uint8_t saved[SEQ_TRANSFER_CAPACITY], restored[SEQ_TRANSFER_CAPACITY];
    uint16_t length = sizeof(saved);
    assert(seq_project_object(1, saved, &length, false, false));

    reset();
    seq_enter(test_now);
    for (uint8_t t = 0; t < 4; t++) {
        seq_slots[t].kind = SEQ_SLOT_KIND_MELODIC;
        seq_chord_mode_set(t, true);
        for (uint8_t i = 0; i < 16; i++) assert(seq_chord_write(t, i, chord, false));
    }
    uint8_t previous[4][SEQ_TRANSFER_CAPACITY];
    uint16_t previous_size[4];
    for (uint8_t t = 0; t < 4; t++) {
        previous_size[t] = sizeof(previous[t]);
        assert(seq_project_object(t + 1, previous[t], &previous_size[t], false, false));
    }
    /* A malformed clear must leave the full recipe pool and notes intact. */
    seq_backup_request(6, 0, 0, (const uint8_t *)"bad!", 4);
    assert(backup_status == 3 && seq_chord_count(3) == 16 && mel_track_event_count(3) == 48);
    seq_backup_request(6, 0, 0, (const uint8_t *)"SEQ!", 4);
    assert(!backup_status);
    for (uint8_t t = 0; t < 12; t++) assert(!seq_chord_count(t) && !mel_track_event_count(t));

    /* Validate and commit through the same protocol used by the restore CLI. */
    uint32_t hash = seq_project_checksum(saved, length);
    uint8_t checksum[4] = {hash, hash >> 8, hash >> 16, hash >> 24};
    for (uint8_t command = 5; command >= 4; command--) {
        seq_backup_request(2, 1, length, NULL, 0);
        assert(!backup_status);
        for (uint16_t offset = 0; offset < length; offset += 32) {
            uint8_t size = length - offset > 32 ? 32 : length - offset;
            seq_backup_request(3, 1, offset, saved + offset, size);
            assert(!backup_status);
        }
        seq_backup_request(command, 1, command == 5 ? 1 : 0, checksum, sizeof(checksum));
        assert(!backup_status);
        if (command == 5) assert(backup_size == 5 && backup_data[3] == 32 && backup_data[4] == SEQ_CHORD_EVENT_CAPACITY);
    }
    assert(seq_chord_count(0) == 32 && mel_track_event_count(0) == 96);
    uint16_t size = sizeof(restored);
    assert(seq_project_object(1, restored, &size, false, false));
    assert(size == length && !memcmp(saved, restored, size));
    assert(seq_project_restore_object(2, saved, length));
    assert(seq_chord_count(1) == 32);
    /* A failed later object preserves applied tracks; the recovery clear then
     * allows the former full pool to be distributed across four tracks again. */
    assert(!seq_project_restore_object(3, saved, length - 1));
    assert(seq_chord_count(0) == 32 && seq_chord_count(1) == 32);
    seq_backup_request(6, 0, 0, (const uint8_t *)"SEQ!", 4);
    for (uint8_t t = 0; t < 4; t++) {
        assert(seq_project_restore_object(t + 1, previous[t], previous_size[t]));
        size = sizeof(restored);
        assert(seq_project_object(t + 1, restored, &size, false, false));
        assert(size == previous_size[t] && !memcmp(previous[t], restored, size));
    }
}

#include "test_seq_global_loop.inc"
#include "test_seq_palette_chords.inc"
#include "test_seq_variation.inc"

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--bridge")) {
        run_protocol_bridge();
        return 0;
    }

    test_variation_track_scope_clock();
    test_accumulator_chords();
    test_variation_groups();
    test_variation_controls_storage();
    test_variation_mutation_projection();
    test_palette_chord_gestures();
    test_palette_chord_playback();
    test_palette_chord_storage();
    test_palette_chord_protocol();
    test_global_loop_preservation();
    test_global_loop_playback();
    test_global_loop_external();
    test_global_loop_gestures();
    test_project_health();
    test_restore_chord_distribution();
    test_temp_snapshot_scratch();
    test_temp_restore();
    test_temp_commit();
    test_temp_quantized_controls();
    test_temp_capacity();
    test_temp_pool_restore();
    test_lane_euclid_gestures();
    test_lane_euclid();
    test_lane_euclid_legacy_capacity();
    test_inactive_keys();
    test_capacity_and_duplication();
    test_playback_and_polymeter();
    test_timing_and_external();
    test_harmony_capture();
    test_harmony_shortcut();
    test_scale_bound_chords();
    test_firmware_identity();
    test_step_hold();
    test_chord_hold_length();
    test_global_tempo();
    test_chord_mode();
    test_chord_inversions();
    test_custom_chords();
    test_cc_live_snapshot();
    test_cc_manual_mode();
    test_cc_shift_pgdn();
    test_cc_and_record();
    test_storage();
    test_gestures();
    test_mod_duplication_and_mutes();
    test_backup_protocol();
    test_euclid_and_capacity();
    test_melodic_euclid_retriggers();
    test_modules();
    test_live_state();
    test_visual_state();
    test_step_note_state();
    test_components();
    test_live_rhythm();
    test_live_palette();
    test_mute_summary();
    test_desktop_shortcut();
    test_palette_pitch_mode();
    test_scale_quantizer();
    test_scale_editor_storage();
    test_ordered_euclid_controls();
    test_live_rhythm_edit();
    test_print_lengths();
    test_drum_print();
    test_euclid_print();
    test_euclid_print_capacity_and_gestures();
    test_palette_remove();
    test_euclid_single_tap_exit();
    test_palette_clear();
    test_palette_replace_shortcut();
    test_palette_provenance_storage();
    test_palette_append();
    test_euclid_picked_notes();
    test_transport_restart();
    test_bounded_save();
    puts("seq_engine: 75 regression groups passed");
}
