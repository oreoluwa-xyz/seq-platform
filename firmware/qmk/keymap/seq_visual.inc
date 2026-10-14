/* Visual telemetry is a frozen, read-only image. Paged replies stay within the
 * existing 64-byte MIDI payload; a token rejects interleaved readers. */
#define SEQ_VISUAL_SIZE (64 + 6 * 16 * 3)
#define SEQ_DETAIL_SIZE (SEQ_VISUAL_SIZE + 16 * 16)
#define SEQ_RHYTHM_SIZE (SEQ_DETAIL_SIZE + 32 + 16 * 16)
#define SEQ_PALETTE_SIZE (SEQ_RHYTHM_SIZE + 64)
static uint8_t  seq_visual_image[SEQ_PALETTE_SIZE];
static uint8_t  seq_visual_token;
static uint8_t  seq_visual_command;
static uint32_t seq_visual_created;

#if defined(RGB_MATRIX_SNLED27351_SPI)
#    include "snled27351-spi.h"
/* V1 Max's driver-owned PWM buffer (192 registers per driver). Never mutated. */
extern uint8_t g_pwm_buffer[][192];
#endif

static uint8_t seq_visual_mode(void) {
    if (!seq_state.active) return 0;
    if (seq_scale_editing) return 10;
    if (seq_track_confirmation_active()) return 9;
    if (switch_state.held) return 6;
    if (cc_context.active || cc_mode.held) return 5;
    if (step_edit.active) return step_edit.is_drum ? 7 : 8;
    if (seq_chord_mode[view_slot] && !slot_viewing_child(view_slot)) return 11;
    if (seq_euclid_editing_enabled()) return 3;
    return slot_uses_melodic(view_slot) ? 2 : 1;
}

/* Project pitches without advancing probability, accumulator or note lifetimes.
 * Labels describe eligible output before the stateful accumulator and mutes. */
static void seq_projected_step_notes(uint8_t slot, uint16_t step, uint8_t *notes) {
    seq_module_t module;
    seq_module_export_selected(slot, &module);
    uint8_t track = slot_melodic_track(slot);
    uint8_t count = mel_track_event_count(track);

    int8_t offset = 0;
    bool euclidean = module.kind == SEQ_MODULE_EUCLID && euclid_tracks[slot].enabled;
    if (euclidean && !seq_euclid_step_hit(slot, step, NULL, &offset)) return;
    bool has_source = false;
    for (uint8_t n = 0; n < count; n++) {
        mel_event_snapshot_t event;
        if (!mel_track_event_at(track, n, &event) || step < event.start_step || step > event.end_step) continue;
        has_source = true;
        int16_t pitch = seq_melodic_euclid_pitch(track, event.pitch, offset, true);
        seq_live_note(notes, seq_scale_apply(track, pitch < 0 ? 0 : pitch > 127 ? 127 : pitch));
    }
    if (euclidean && !has_source) {
        uint8_t pitches[SEQ_MELODIC_KEY_COUNT];
        uint8_t voices = seq_melodic_euclid_fallback(track, pitches);
        for (uint8_t n = 0; n < voices; n++) {
            int16_t pitch = seq_melodic_euclid_pitch(track, pitches[n], offset, false);
            seq_live_note(notes, seq_scale_apply(track, pitch < 0 ? 0 : pitch > 127 ? 127 : pitch));
        }
    }
}

static void seq_rhythm_snapshot(void) {
    uint8_t slot = view_slot % 12;
    seq_module_t module;
    seq_module_export_selected(slot, &module);
    seq_rhythm_component_t rhythm = seq_module_rhythm(&module);
    uint8_t *meta = seq_visual_image + SEQ_DETAIL_SIZE;
    meta[0] = (rhythm.kind == 2 ? 1 : 0) | (rhythm.enabled ? 2 : 0) | (rhythm.template_active ? 4 : 0) | (!module.composed && module.kind == SEQ_MODULE_EUCLID && seq_accumulators[slot].enabled ? 8 : 0);
    meta[14] = 1; meta[15] = seq_scale_global.enabled; meta[16] = seq_scale_global.root; meta[17] = seq_scale_global.kind;
    meta[18] = seq_scale_tracks[slot].mode; meta[19] = seq_scale_tracks[slot].root;
    meta[20] = seq_scale_tracks[slot].kind; meta[21] = seq_scale_tracks[slot].direction;
    meta[22] = seq_scale_editing; meta[23] = seq_scale_pending.enabled; meta[24] = seq_scale_pending.root; meta[25] = seq_scale_pending.kind;
    seq_live_u16(meta + 26, seq_scale_revision);
    meta[28] = SEQ_SCALE_COUNT;
    meta[30] = 127; /* Bit 0: melody source objects. Bit 1: harmony shortcut. Bit 2: chord editor. Bit 3: Ctrl + knob tempo. Bit 4: chord inversions. Bit 5: custom/quantized chords. Bit 6: scale-bound chord variations. */
    meta[31] = seq_harmony_request;
    meta[29] = 1; /* Paged CC state (command 21) available. */
    meta[11] = slot_uses_melodic(slot) ? 255 : drum_active_note[slot];
    meta[1] = 7; /* Bit 0: editing; bit 1: ordered controls; bit 2: explicit lane context. */
    seq_live_u16(meta + 2, rhythm.steps);
    seq_live_u16(meta + 4, rhythm.fills);
    seq_live_u16(meta + 6, rhythm.rotation);
    meta[8] = rhythm.probability;
    meta[9] = rhythm.accent;
    meta[10] = rhythm.ratchet;
    uint16_t generated = 0;
    uint16_t length = seq_track_length_steps(slot);
    for (uint8_t i = 0; i < 16; i++) {
        uint16_t step = view_bar * 16u + i;
        if (step < length && (slot_uses_melodic(slot) ? seq_rhythm_hit(&rhythm, step, NULL) : seq_drum_euclid_hit(slot, drum_active_note[slot], step, NULL))) generated |= 1u << i;
    }
    seq_live_u16(meta + 12, generated);
    if (!slot_uses_melodic(slot)) return;
    meta[0] |= 16; /* Projected melodic pitches available. */
    for (uint8_t i = 0; i < 16; i++) {
        uint16_t step = view_bar * 16u + i;
        if (step < length) seq_projected_step_notes(slot, step, seq_visual_image + SEQ_DETAIL_SIZE + 32 + i * 16);
    }
}

static void seq_palette_snapshot(void) {
    uint8_t slot = view_slot % 12;
    uint8_t *out = seq_visual_image + SEQ_RHYTHM_SIZE;
    memset(out + 40, 255, 16);
    out[2] = 255;
    out[62] = 16 | (seq_temp_active ? 32 : 0) | (seq_temp_restore_pending ? 64 : 0); /* Global TEMP capability, active, queued. */
    if (!slot_uses_melodic(slot) || seq_modules[slot].kind != SEQ_MODULE_EUCLID) return;
    uint8_t track = slot_melodic_track(slot);
    const seq_euclid_track_t *e = &euclid_tracks[slot];
    bool capturing = euclid_palette_capture.active && euclid_palette_capture.primed && euclid_palette_capture.track == slot;
    out[0] = 1 | (cmd_state.held ? 2 : 0) | (seq_shift_active() ? 4 : 0) | (capturing ? 8 : 0) | (euclid_mode_active ? 16 : 0);
    out[1] = e->note_count;
    out[2] = e->palette_pin;
    out[3] = seq_melodic_note_pitch(track, e->palette_root);
    out[58] = 1; /* Pitch-mode controls and per-step overrides supported. */
    out[59] = e->pitch_mode;
    out[60] = 1 | ((capturing ? euclid_palette_capture.replace : seq_palette_replace[slot]) ? 2 : 0) | (capturing && euclid_palette_capture.full ? 4 : 0);
    out[61] = capturing ? euclid_palette_capture.retained : 0;
    out[62] |= 1 | (!e->palette_authored ? 2 : 0) | 4 | 8; /* Replace shortcut; starter; clear; individual removal. */
    out[63] = e->palette_root;
    for (uint8_t i = 0; i < e->note_count && i < 8; i++) {
        int16_t pitch = (int16_t)out[3] + e->note_pool[i];
        out[8 + i] = (uint8_t)e->note_pool[i];
        out[16 + i] = pitch < 0 ? 0 : pitch > 127 ? 127 : pitch;
    }
    if (capturing) {
        out[4] = euclid_palette_capture.count;
        out[5] = seq_melodic_note_pitch(track, euclid_palette_capture.origin);
        for (uint8_t i = 0; i < out[4] && i < 8; i++) {
            int16_t pitch = (int16_t)out[5] + euclid_palette_capture.values[i];
            out[24 + i] = (uint8_t)euclid_palette_capture.values[i];
            out[32 + i] = pitch < 0 ? 0 : pitch > 127 ? 127 : pitch;
        }
    }
    for (uint8_t i = 0; i < 16; i++) {
        uint16_t step = view_bar * 16u + i, hit;
        bool stored = false;
        for (uint8_t n = 0; n < 16; n++) stored |= seq_visual_image[SEQ_VISUAL_SIZE + i * 16 + n] != 0;
        if (stored) out[56 + i / 8] |= 1u << (i % 8);
        if (!e->note_count || (stored && !e->pitch_mode)) continue;
        if (step < seq_track_length_steps(slot) && seq_euclid_step_hit(slot, step, &hit, NULL))
            out[40 + i] = e->palette_pin < e->note_count ? e->palette_pin : hit % (e->note_count ? e->note_count : 1);
    }
}

static inline void seq_cc_load_mapping(uint8_t index, seq_cc_mapping_t *out);
static uint8_t seq_cc_current_track(void);
static void seq_cc_snapshot(void) {
    uint8_t track = seq_cc_current_track();
    uint8_t count = SEQ_CC_MAPPING_COUNT < 16 ? SEQ_CC_MAPPING_COUNT : 16;
    bool temporary = cc_mode.temporary || cc_mode.latched_temporary || (cc_session.active && cc_session.temporary);
    seq_visual_image[0] = 1;
    seq_visual_image[1] = (seq_state.active && cc_context.active ? 1 : 0) | (cc_mode.latched ? 2 : 0) | (temporary ? 4 : 0) | (seq_state.running ? 8 : 0) | (cc_context.hold_override_active ? 16 : 0);
    seq_visual_image[2] = track;
    seq_visual_image[3] = cc_session.active && cc_session.track == track && cc_session.mapping_index < count ? cc_session.mapping_index : 255;
    seq_visual_image[4] = count;
    seq_visual_image[160] = 1; /* Per-track automation mode support. */
    seq_visual_image[161] = cc_automation_manual[track] ? 1 : 0;
    seq_visual_image[162] = 1; /* Shift + PgDn shortcut. */
    for (uint8_t i = 0; i < count; i++) {
        seq_cc_mapping_t mapping;
        seq_cc_load_mapping(i, &mapping);
        uint8_t *out = seq_visual_image + 64 + i * 6;
        seq_live_u16(out, mapping.keycode);
        out[2] = mapping.controller;
        out[3] = mapping.channel == 255 ? midi_channel_for_track(track) : mapping.channel;
        out[4] = cc_values[track][i];
        out[5] = cc_key_pressed[track][i] ? 1 : 0;
    }
}

#include "seq_chord_visual.inc"

static void seq_visual_capture(uint8_t command) {
    memset(seq_visual_image, 0, sizeof(seq_visual_image));
    seq_live_snapshot(seq_visual_image);
    seq_visual_command   = command;
    seq_visual_image[0]  = command == 17 ? 5 : command == 16 ? 4 : command == 14 ? 3 : 2;
    seq_visual_image[60] = seq_visual_mode();
    seq_visual_image[61] = (current_layout == SEQ_LAYOUT_GRID_4X4 ? 1 : 0) | (seq_step_hold.editing ? 16 : 0);
    seq_visual_token     = seq_visual_token % 127u + 1u;
    seq_visual_image[62] = seq_visual_token;
    seq_visual_image[63] = 1; /* Model 1: V1 Max ANSI, matrix 6 x 16. */
    seq_visual_created   = timer_read32();
    if (command == 22) { seq_chord_snapshot(); return; }
    if (command == 21) { seq_cc_snapshot(); return; }
    if ((command == 14 || command == 16 || command == 17) && slot_uses_melodic(view_slot)) {
        uint8_t  track = slot_melodic_track(view_slot);
        uint16_t start = view_bar * 16u, length = seq_track_length_steps(view_slot);
        for (uint8_t i = 0; i < mel_track_event_count(track); i++) {
            mel_event_snapshot_t event;
            if (!mel_track_event_at(track, i, &event)) continue;
            for (uint8_t step = 0; step < 16; step++) {
                uint16_t absolute = start + step;
                if (absolute < length && absolute >= event.start_step && absolute <= event.end_step) seq_live_note(seq_visual_image + SEQ_VISUAL_SIZE + step * 16, event.pitch);
            }
        }
    }
    if (command == 16 || command == 17) seq_rhythm_snapshot();
    if (command == 17) seq_palette_snapshot();
#ifdef RGB_MATRIX_ENABLE
    seq_visual_image[61] |= 8 | (get_highest_layer(default_layer_state) >= 2 ? 2 : 0);
#endif
#if defined(RGB_MATRIX_SNLED27351_SPI)
    seq_visual_image[61] |= 4;
    for (uint8_t row = 0; row < 6; row++) {
        for (uint8_t col = 0; col < 16; col++) {
            uint8_t index = g_led_config.matrix_co[row][col];
            if (index == NO_LED || index >= RGB_MATRIX_LED_COUNT || !rgb_matrix_is_enabled()) continue;
            snled27351_led_t led;
            memcpy_P(&led, &g_snled27351_leds[index], sizeof(led));
            uint16_t offset              = 64 + (row * 16 + col) * 3;
            seq_visual_image[offset]     = g_pwm_buffer[led.driver][led.r];
            seq_visual_image[offset + 1] = g_pwm_buffer[led.driver][led.g];
            seq_visual_image[offset + 2] = g_pwm_buffer[led.driver][led.b];
        }
    }
#endif
}

static void seq_visual_request(uint8_t command, uint8_t object, uint16_t offset, uint8_t size) {
    uint8_t  status = 0;
    uint16_t total  = command == 22 ? 448 : command == 21 ? 192 : command == 17 ? SEQ_PALETTE_SIZE : command == 16 ? SEQ_RHYTHM_SIZE : command == 14 ? SEQ_DETAIL_SIZE : SEQ_VISUAL_SIZE;
    if (size || offset >= total || offset % 64 || (!offset && object)) {
        status = 3;
    } else if (!offset) {
        seq_visual_capture(command);
    } else if (command != seq_visual_command || !seq_visual_token || object != seq_visual_token || TIMER_DIFF_32(timer_read32(), seq_visual_created) > 2000u) {
        status = 2;
    }
    uint8_t count = status ? 0 : (total - offset > 64 ? 64 : total - offset);
    virtual_midi_backup_reply(command, object, offset, status, status ? NULL : seq_visual_image + offset, count);
}

static bool seq_harmony_source(seq_codec_t *c, uint8_t slot) {
    if (!slot_uses_melodic(slot)) return false;
    uint16_t length = seq_track_length_steps(slot);
    uint8_t track = slot_melodic_track(slot);
    seq_codec_u8(c, 1);seq_codec_u8(c, slot);seq_codec_u16(c, length);
    seq_codec_u8(c, seq_scale_global.enabled);seq_codec_u8(c, seq_scale_global.root);seq_codec_u8(c, seq_scale_global.kind);
    seq_codec_u8(c, midi_channel_for_track(track));
    uint8_t previous = 255;
    bool euclidean = seq_modules[slot].kind == SEQ_MODULE_EUCLID && euclid_tracks[slot].enabled;
    for (uint16_t step = 0; step < length; step++) {
        uint8_t notes[16] = {0}, pitch = 255;
        seq_projected_step_notes(slot, step, notes);
        for (uint8_t n = 0; n < 128; n++) if (notes[n/8] & (1u << (n%8))) pitch = n;
        bool onset = pitch != 255 && (euclidean || pitch != previous);
        for (uint8_t n = 0; !onset && pitch != 255 && n < mel_track_event_count(track); n++) {
            mel_event_snapshot_t event;
            if (mel_track_event_at(track, n, &event) && event.start_step == step) onset = true;
        }
        seq_codec_u8(c, pitch);seq_codec_u8(c, onset);previous = pitch;
    }
    return c->ok;
}
