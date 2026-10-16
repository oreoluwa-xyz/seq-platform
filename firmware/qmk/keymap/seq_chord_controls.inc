static bool seq_chord_shortcut(uint16_t key, bool pressed) {
    int8_t track = track_from_keycode(key);
    if (track < 0) return false;
    uint16_t bit = 1u << track;
    if (!pressed && (seq_chord_shortcut_keys & bit)) {
        seq_chord_shortcut_keys &= ~bit;
        return true;
    }
    if (pressed && seq_state.active && seq_mod_tab() && seq_ctrl_mask) {
        seq_chord_shortcut_keys |= bit;
        if (!record_state.armed && seq_live_can_select() && !seq_track_confirmation_active()) seq_track_request_kind(track, SEQ_SLOT_KIND_CHORD, true, timer_read32());
        return true;
    }
    return false;
}
static uint32_t seq_chord_press_time;
static bool     seq_chord_hold_changed;
static void     seq_chord_cancel(void) {
    if (seq_chord_note_held) seq_module_preview_stop(timer_read32());
    seq_chord_selected = SEQ_CHORD_NONE;
    seq_chord_held = seq_chord_note_held = 0;
    seq_chord_delete                     = false;
    seq_chord_hold_changed               = false;
}
static void seq_chord_mode_set(uint8_t track, bool enabled) {
    seq_chord_cancel();
    seq_midi_stop_melodic_track(track, timer_read32());
    seq_chord_mode[track] = enabled;
    if (enabled) {
        seq_module_default(&seq_modules[track], SEQ_MODULE_EUCLID);
        seq_modules[track].composed  = true;
        seq_modules[track].rhythm    = (seq_rhythm_component_t){.kind = 1};
        euclid_tracks[track].enabled = false;
        euclid_mode_active           = false;
        seq_chord_event_t *d         = &seq_chord_draft[track];
        if (!seq_chord_count(track)) {
            d->scale_free = 0;
            d->inversion = 0;
            d->type      = seq_scale_tracks[track].mode == 1 || (seq_scale_tracks[track].mode == 0 && seq_scale_global.enabled) ? 10 : 0;
            seq_chord_context(track, d);
        }
    }
    seq_chord_error = 0;
    seq_mark_dirty();
}
static void seq_chord_audition(uint8_t track, uint16_t key) {
    seq_module_preview_stop(timer_read32());
    if (seq_track_muted[track] || melodic_track_muted[track]) return;
    module_preview.count   = seq_chord_render(&seq_chord_draft[track], module_preview.notes);
    module_preview.track   = track;
    module_preview.channel = midi_channel_for_track(track);
    module_preview.key     = key;
    module_preview.active  = module_preview.count > 0;
    uint16_t velocity      = (uint16_t)seq_chord_draft[track].velocity * melodic_track_velocity[track] / 127;
    for (uint8_t i = 0; i < module_preview.count; i++)
        if (!melodic_active_contains(track, module_preview.notes[i])) virtual_midi_note_on(module_preview.channel, module_preview.notes[i], velocity ? velocity : 1, timer_read32());
}
static bool seq_chord_change(uint8_t track, seq_chord_event_t next) {
    uint8_t notes[8];
    if (!seq_chord_render(&next, notes)) {
        seq_chord_error = 1;
        return false;
    }
    if (seq_chord_selected != SEQ_CHORD_NONE) {
        if (!seq_chord_write(track, seq_chord_selected, next, false)) return false;
        next = seq_chord_events[seq_chord_find(track, seq_chord_selected)];
    }
    seq_chord_draft[track] = next;
    seq_chord_hold_changed = true;
    seq_chord_error        = 0;
    seq_mark_dirty();
    if (seq_chord_note_held) seq_chord_audition(track, seq_chord_note_held);
    return true;
}
static bool seq_chord_encoder(int8_t delta) {
    uint8_t t = view_slot;
    if (!seq_state.active || seq_transfer_active || !seq_chord_mode[t] || slot_viewing_child(t) || seq_scale_editing || seq_track_confirmation_active() || switch_state.held || cmd_state.held || cc_context.active || cc_mode.held || record_state.armed) return false;
    seq_chord_event_t c = seq_chord_draft[t];
    if (seq_chord_parameter == 0) {
        int16_t pitch = c.root + delta;
        while (!c.scale_free && pitch >= 0 && pitch <= 127 && seq_chord_root(t, pitch) != pitch)
            pitch += delta;
        if (pitch < 0 || pitch > 127) return true;
        c.root = pitch;
        seq_chord_context(t, &c);
    } else if (seq_chord_parameter == 1) {
        const uint8_t scale_types[] = {10, 11, 13, 14, 15};
        const uint8_t free_types[] = {0, 1, 2, 16, 3, 4, 5, 6, 7, 8, 9, 12};
        bool bound = !c.scale_free && seq_chord_scale_enabled(t);
        const uint8_t *types = bound ? scale_types : free_types;
        uint8_t count = bound ? sizeof(scale_types) : sizeof(free_types);
        int16_t index = -1;
        for (uint8_t i = 0; i < count; i++) if (types[i] == c.type) index = i;
        if (index < 0) index = delta > 0 ? -1 : count;
        index += delta;
        if (index < 0) index = count - 1;
        if (index >= count) index = 0;
        uint8_t type = types[index];
        if (type == SEQ_CHORD_CUSTOM) {
            uint8_t pitches[8], voices = seq_chord_render(&c, pitches);
            if (voices < 2) return true;
            c.custom_count = voices;
            for (uint8_t i = 0; i < voices; i++)
                c.intervals[i] = (int16_t)pitches[i] - c.root;
            memset(c.name, 0, sizeof(c.name));
            memcpy(c.name, "Custom", 6);
            c.inversion = 0;
        }
        c.type         = type;
        uint8_t voices = seq_chord_voices(&c);
        if (c.inversion >= voices) c.inversion = voices - 1;
        seq_chord_context(t, &c);
    } else if (seq_chord_parameter == 3) {
        int16_t inversion = c.inversion + delta;
        uint8_t voices    = seq_chord_voices(&c);
        if (inversion < 0 || inversion >= voices) return true;
        c.inversion = inversion;
    } else if (seq_chord_parameter == 4) {
        int16_t velocity = c.velocity + delta * (seq_shift_active() ? 5 : 1);
        c.velocity       = velocity < 1 ? 1 : velocity > 127 ? 127 : velocity;
    } else {
        int32_t length = (int32_t)c.length + delta * (seq_shift_active() ? 4 : 1);
        if (length < 1) length = 1;
        if (length > seq_track_length_steps(t)) length = seq_track_length_steps(t);
        c.length = length;
    }
    seq_chord_change(t, c);
    return true;
}
static bool seq_chord_toggle_scale(uint8_t track) {
    if (seq_chord_selected == SEQ_CHORD_NONE) return false;
    seq_chord_event_t c = seq_chord_draft[track];
    if (!c.scale_free) {
        uint8_t notes[8], count = seq_chord_render(&c, notes);
        if (count < 2) return false;
        c.type = SEQ_CHORD_CUSTOM;
        c.custom_count = count;
        c.inversion = 0;
        c.quantize = 0;
        for (uint8_t i = 0; i < count; i++) c.intervals[i] = (int16_t)notes[i] - c.root;
        memset(c.name, 0, sizeof(c.name));
        memcpy(c.name, "Free", 4);
        c.scale_free = 1;
    } else {
        if (!seq_chord_scale_enabled(track)) return false;
        c.scale_free = 0;
        c.type = seq_chord_voices(&c) >= 4 ? 11 : 10;
        c.quantize = 0;
        seq_chord_context(track, &c);
        if (c.inversion >= seq_chord_voices(&c)) c.inversion = seq_chord_voices(&c) - 1;
    }
    return seq_chord_change(track, c);
}
static bool seq_chord_key(uint16_t key, bool pressed, uint32_t now) {
    uint8_t t = view_slot;
    if (!pressed && key == seq_chord_note_held) {
        seq_module_preview_stop(now);
        seq_chord_note_held = 0;
        return true;
    }
    if (!seq_state.active || !seq_chord_mode[t] || slot_viewing_child(t) || seq_transfer_active || seq_track_confirmation_active() || seq_scale_editing) return false;
    if (seq_chord_edit_track != t) {
        seq_chord_cancel();
        seq_chord_edit_track = t;
    }
    if (seq_mod_tab() || seq_mod_caps() || switch_state.held || cc_context.active || cc_mode.held) return false;
    if (seq_is_option_key(key)) return true; /* Rhythm generators remain a separate later slice. */
    if (key == KC_DEL) {
        seq_chord_delete = pressed;
        return false;
    }
    if (key == KC_ESC && pressed && (seq_chord_selected != SEQ_CHORD_NONE || seq_chord_note_held)) {
        seq_chord_cancel();
        return true;
    }
    if (record_state.armed) return false;
    if (key == KC_RBRC && seq_shift_active()) {
        if (pressed) seq_chord_toggle_scale(t);
        return true;
    }
    if (key == KC_I || key == KC_O || key == KC_P || key == KC_LBRC || key == KC_RBRC) {
        if (pressed) seq_chord_parameter = key == KC_I ? 2 : key == KC_O ? 0 : key == KC_P ? 4 : key == KC_LBRC ? 3 : 1;
        return true;
    }
    int8_t note = seq_melodic_key_index_from_keycode(key);
    if (note >= 0) {
        if (pressed) {
            if (!seq_chord_held) seq_chord_selected = SEQ_CHORD_NONE;
            seq_chord_event_t c = seq_chord_draft[t];
            if (!seq_chord_held) c.scale_free = 0;
            c.root              = seq_melodic_note_pitch(t, note);
            seq_chord_context(t, &c);
            if (seq_chord_change(t, c)) {
                seq_chord_note_held = key;
                seq_chord_audition(t, key);
            }
        }
        return true;
    }
    int8_t step = step_from_keycode(key);
    if (step >= 0) {
        if (!pressed) {
            if (seq_chord_held == key) {
                if (seq_chord_hold_changed || TIMER_DIFF_32(now, seq_chord_press_time) >= SEQ_STEP_HOLD_MS) seq_chord_selected = SEQ_CHORD_NONE;
                seq_chord_held = 0;
            }
            return true;
        }
        uint16_t absolute = view_bar * 16u + step;
        if (absolute >= seq_track_length_steps(t)) {
            seq_chord_error = 1;
            return true;
        }
        seq_chord_prune();
        int16_t index = seq_chord_find(t, absolute);
        if (seq_chord_delete) {
            seq_chord_write(t, absolute, seq_chord_draft[t], true);
            seq_chord_selected = SEQ_CHORD_NONE;
            return true;
        }
        if (index < 0 || seq_chord_note_held) {
            seq_chord_event_t placement = seq_chord_draft[t];
            placement.scale_free = 0;
            seq_chord_context(t, &placement);
            if (!seq_chord_write(t, absolute, placement, false)) return true;
            index = seq_chord_find(t, absolute);
        }
        seq_chord_selected     = seq_chord_events[index].start;
        seq_chord_draft[t]     = seq_chord_events[index];
        seq_chord_parameter    = 2;
        seq_chord_held         = key;
        seq_chord_press_time   = now;
        seq_chord_hold_changed = false;
        return true;
    }
    if (pressed && (track_from_keycode(key) >= 0 || key == KC_HOME || key == KC_PGUP || key == KC_PGDN)) seq_chord_cancel();
    return false;
}
static void seq_chord_request(uint8_t track, uint16_t offset, const uint8_t *data, uint8_t size) {
    uint8_t status = 0;
    if (track >= 12 || offset || size != 1 || !data || data[0] > 2)
        status = 3;
    else if (!seq_state.active || track != view_slot || !seq_live_can_select() || record_state.armed || seq_track_confirmation_active())
        status = 2;
    else
        seq_track_request_kind(track, data[0] == 1 ? SEQ_SLOT_KIND_CHORD : data[0] == 2 ? SEQ_SLOT_KIND_DRUM : SEQ_SLOT_KIND_MELODIC, true, timer_read32());
    virtual_midi_backup_reply(23, track, 0, status, NULL, 0);
}
