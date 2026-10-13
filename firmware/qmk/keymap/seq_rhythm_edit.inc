/* Absolute live rhythm edits carry the displayed old value to reject edits
 * made stale by a hardware gesture while the browser field was focused. */
static uint16_t seq_rhythm_edit_value(const seq_rhythm_component_t *r, uint8_t parameter) {
    switch (parameter) {
        case 0: return r->steps;
        case 1: return r->fills;
        case 2: return r->rotation;
        case 3: return r->probability;
        case 4: return r->accent;
        case 5: return r->enabled;
        default: return 0;
    }
}
static void seq_rhythm_edit_request(uint8_t track, uint16_t context, const uint8_t *data, uint8_t size) {
    uint8_t status = 0;
    if (track >= 12 || context > 25 || !data || size != 5 || data[0] > 5) {
        status = 3;
    } else {
        seq_module_t module;
        seq_module_export_selected(track, &module);
        seq_rhythm_component_t r = seq_module_rhythm(&module);
        uint8_t parameter = data[0];
        uint16_t value = data[1] | ((uint16_t)data[2] << 8);
        uint16_t expected = data[3] | ((uint16_t)data[4] << 8);
        uint16_t limit = parameter == 0 ? seq_track_length_steps(track) : parameter == 1 ? r.steps : parameter == 2 ? r.steps - 1u : parameter == 3 ? 100u : parameter == 4 ? 48u : 1u;
        if (r.kind != 2 || track != view_slot || context != (slot_uses_melodic(track) ? slot_viewing_child(track) : 2 + 2 * drum_active_note[track] + slot_viewing_child(track)) || !seq_state.active ||
            !seq_live_can_select() || record_state.armed || euclid_palette_capture.active || seq_track_confirmation_active() ||
            expected != seq_rhythm_edit_value(&r, parameter)) {
            status = 2;
        } else if (value > limit || (parameter == 0 && !value)) {
            status = 3;
        } else if (value != expected) {
            switch (parameter) {
                case 0:
                    r.steps = value;
                    if (r.fills > value) r.fills = value;
                    r.rotation %= value;
                    if (r.ratchet > value) r.ratchet = value;
                    r.template_active = false;
                    break;
                case 1: r.fills = value; r.template_active = false; break;
                case 2: r.rotation = value; break;
                case 3: r.probability = value; break;
                case 4: r.accent = value; break;
                case 5: r.enabled = value; break;
            }
            if (module.composed && slot_uses_melodic(track)) seq_modules[track].rhythm = r;
            if (module.kind == SEQ_MODULE_EUCLID) {
                seq_euclid_track_t *e = seq_euclid_selected(track, true);
                if (!e) { virtual_midi_backup_reply(19, track, context, 2, NULL, 0); return; }
                e->steps = r.steps; e->fills = r.fills; e->rotate = r.rotation;
                e->probability = r.probability; e->swing_amount = r.accent;
                e->enabled = r.enabled; e->template_active = r.template_active;
                e->ratchet_threshold = r.ratchet;
            }
            seq_mark_dirty();
        }
    }
    virtual_midi_backup_reply(19, track, context, status, NULL, 0);
}
