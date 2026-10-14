/* Engine-owned audition and stamping. Modules never send MIDI directly. */
static bool seq_module_preview_owns(uint8_t track, uint8_t note) {
    if (!module_preview.active || module_preview.track != track) return false;
    for (uint8_t i = 0; i < module_preview.count; i++)
        if (module_preview.notes[i] == note) return true;
    return false;
}
static void seq_module_preview_stop(uint32_t now) {
    if (!module_preview.active) return;
    uint8_t track         = module_preview.track;
    module_preview.active = false;
    for (uint8_t i = 0; i < module_preview.count; i++) {
        uint8_t note = module_preview.notes[i];
        if (!melodic_active_contains(track, note)) virtual_midi_note_off(module_preview.channel, note, now);
    }
    module_preview.count = 0;
}
