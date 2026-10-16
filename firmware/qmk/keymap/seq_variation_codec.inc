#define SEQ_VARIATION_WIRE_SIZE 11
static void seq_variation_pack(const seq_variation_t *v, uint8_t *out) {
    for (uint8_t i = 0; i < 4; i++) out[i] = v->seed >> (i * 8);
    out[4] = v->order; out[5] = v->rotate; out[6] = v->mutation; out[7] = v->range;
    out[8] = v->voicing; out[9] = v->enabled; out[10] = v->locked;
}
static bool seq_variation_unpack(seq_variation_t *v, const uint8_t *in) {
    *v = (seq_variation_t){.seed = (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24),
        .order = in[4], .rotate = in[5], .mutation = in[6], .range = in[7], .voicing = in[8], .enabled = in[9], .locked = in[10]};
    return v->order <= 4 && v->rotate <= 127 && v->mutation <= 100 && v->range <= 24 && v->voicing <= 4 && v->enabled <= 1 && v->locked <= 1;
}
static void seq_project_variation(seq_codec_t *c, uint8_t track) {
    seq_variation_t value = {0}; uint8_t wire[SEQ_VARIATION_WIRE_SIZE];
    seq_variation_pack(&seq_variations[track], wire);
    bool present = false;
    for (uint8_t i = 0; i < sizeof(wire); i++) present |= wire[i] != 0;
    if ((!c->read && present) || (c->read && c->pos < c->size && c->data[c->pos] == 'V')) {
        seq_codec_u8(c, 'V');
        if (seq_codec_u8(c, 1) != 1) c->ok = false;
        for (uint8_t i = 0; i < sizeof(wire); i++) wire[i] = seq_codec_u8(c, wire[i]);
        if (!seq_variation_unpack(&value, wire)) c->ok = false;
    }
    if (c->read && c->apply && c->ok) {
        seq_variations[track] = value;
        seq_variation_pending &= ~(1u << track);
        seq_variation_revision++;
    }
}
