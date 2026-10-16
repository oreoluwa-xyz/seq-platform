static uint32_t seq_variation_seed(uint8_t track);
static uint32_t seq_variation_hash(uint32_t value);
static void seq_variation_request(uint8_t command, uint8_t track, uint16_t context, const uint8_t *data, uint8_t size) {
    uint8_t status = 0, out[32] = {1, track};
    if (track >= 12 || context) status = 3;
    else if (command == 26) {
        if (size) status = 3;
        else {
            seq_live_u16(out + 2, seq_variation_revision);
            out[4] = !!(seq_variation_pending & (1u << track));
            seq_variation_pack(&seq_variations[track], out + 5);
            seq_variation_pack(out[4] ? &seq_variation_queued[track] : &seq_variations[track], out + 16);
            out[27] = seq_accumulators[track].enabled; out[28] = seq_accumulators[track].random;
            out[29] = seq_accumulators[track].run_length; out[30] = seq_accumulators[track].direction < 0;
            out[31] = slot_is_melodic(track);
        }
    } else if (!data || size < 5 || data[0] > 5 || data[1] > 1 || data[2] > 1 ||
               size != (data[0] == 0 ? 16 : data[0] == 5 ? 9 : 5)) status = 3;
    else if (track != view_slot || slot_viewing_child(track) || !slot_is_melodic(track) || !seq_state.active ||
             !seq_live_can_select() || record_state.armed || euclid_palette_capture.active || seq_track_confirmation_active() ||
             (data[3] | ((uint16_t)data[4] << 8)) != seq_variation_revision) status = 2;
    else {
        seq_variation_t config = {0};
        if (data[0] == 0 && !seq_variation_unpack(&config, data + 5)) status = 3;
        if (data[0] == 5 && (data[5] > 1 || data[6] > 1 || (data[7] != 0 && data[7] != 2 && data[7] != 4) || data[8] > 1 || data[2])) status = 3;
        if (!status) {
            uint16_t affected = 0;
            bool lock = !(seq_variation_pending & (1u << track) ? seq_variation_queued[track].locked : seq_variations[track].locked);
            for (uint8_t t = 0; t < 12; t++) if ((data[1] || t == track) && slot_is_melodic(t)) {
                affected |= 1u << t;
                seq_variation_t next = seq_variation_pending & (1u << t) ? seq_variation_queued[t] : seq_variations[t];
                if (data[0] == 4) { seq_variation_pending &= ~(1u << t); continue; }
                if (data[0] == 5) {
                    seq_midi_stop_melodic_track(t, timer_read32());
                    seq_accumulators[t].enabled = data[5]; seq_accumulators[t].random = data[6];
                    seq_accumulators[t].run_length = data[7]; seq_accumulators[t].direction = data[8] ? -1 : 1;
                    seq_accumulator_reset_runtime(t);
                    seq_variation_rejoin |= 1u << t;
                    continue;
                }
                if (data[0] == 0) next = config;
                if (data[0] == 1) { next.seed = seq_variation_hash(next.seed + 0x9e3779b9u); next.enabled = 1; }
                if (data[0] == 2) {
                    if (lock && !next.locked) next.seed = seq_variation_hash(next.seed ^ (seq_step_position / seq_track_playback_length(t)));
                    next.locked = lock;
                }
                if (data[0] == 3) next = (seq_variation_t){0};
                seq_variation_queued[t] = next;
                seq_variation_pending |= 1u << t;
            }
            seq_variation_revision++;
            if (data[0] < 4 && (!data[2] || !seq_state.running)) seq_variation_apply_mask(affected, timer_read32());
            if (data[0] == 5) seq_mark_dirty();
        }
    }
    virtual_midi_backup_reply(command, track, context, status, out, !status && command == 26 ? sizeof(out) : 0);
}
