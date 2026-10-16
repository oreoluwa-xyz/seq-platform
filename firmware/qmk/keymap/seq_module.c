#include "seq_module.h"
#include <string.h>

void seq_module_default(seq_module_t *m, uint8_t kind) {
    memset(m, 0, sizeof(*m));
    m->kind = kind;
    if (kind == SEQ_MODULE_EUCLID) {
        m->config.euclid = (seq_module_euclid_t){.steps = 16, .fills = 4, .enabled = 1, .probability = 100, .count = 1, .pin = 255};
    }
}
seq_rhythm_component_t seq_module_rhythm(const seq_module_t *m) {
    if (m->composed) return m->rhythm;
    if (m->kind != SEQ_MODULE_EUCLID) return (seq_rhythm_component_t){.kind = 1};
    const seq_module_euclid_t *e = &m->config.euclid;
    return (seq_rhythm_component_t){.kind = 2, .enabled = e->enabled, .steps = e->steps, .fills = e->fills, .rotation = e->rotation, .probability = e->probability, .accent = e->accent, .ratchet = e->ratchet, .template_active = e->template_active, .template_mask = e->template_mask};
}
bool seq_rhythm_valid(const seq_rhythm_component_t *r) {
    if (r->kind == 1) return !r->steps && !r->fills && !r->rotation && !r->template_mask && !r->enabled && !r->probability && !r->accent && !r->ratchet && !r->template_active;
    return r->kind == 2 && r->enabled <= 1 && r->steps && r->steps <= SEQ_MAX_STEPS && r->fills <= r->steps && r->rotation < r->steps && r->probability <= 100 && r->accent <= 100 && r->ratchet <= r->steps && r->template_active <= 1 && (!r->template_active || r->steps <= 16);
}
bool seq_rhythm_hit(const seq_rhythm_component_t *r, uint16_t step, uint16_t *index) {
    if (r->kind != 2 || !r->enabled || !r->steps || !r->fills) return false;
    uint16_t rotated = (step % r->steps + r->steps - r->rotation) % r->steps;
    uint16_t hit;
    if (r->template_active) {
        if (rotated >= 16 || !(r->template_mask & (1u << rotated))) return false;
        hit = __builtin_popcount(r->template_mask & ((1u << rotated) - 1u));
    } else {
        if (((uint32_t)rotated * r->fills) % r->steps >= r->fills) return false;
        hit = (uint32_t)rotated * r->fills / r->steps;
    }
    if (index) *index = hit;
    return true;
}
bool seq_module_valid(const seq_module_t *m) {
    seq_rhythm_component_t rhythm = seq_module_rhythm(m);
    if (!seq_rhythm_valid(&rhythm)) return false;
    if (m->kind == SEQ_MODULE_EUCLID) {
        const seq_module_euclid_t *e = &m->config.euclid;
        for (uint8_t i = 0; i < 8; i++)
            if (e->qualities[i] > 11 && e->qualities[i] != 16) return false;
        return e->pitch_mode <= 2 && e->root < 12 && e->count <= 8 && (e->pin == 255 || e->pin < e->count);
    }
    return false;
}
typedef struct {
    uint8_t *data;
    uint16_t size, pos;
    bool     read, ok, allow_bank, migrated;
} module_codec_t;
static uint8_t byte(module_codec_t *c, uint8_t value) {
    if (c->pos >= c->size) {
        c->ok = false;
        return 0;
    }
    if (c->read) return c->data[c->pos++];
    if (c->data) c->data[c->pos] = value;
    c->pos++;
    return value;
}
static uint16_t word(module_codec_t *c, uint16_t value) {
    uint16_t lo = byte(c, value & 255u);
    return lo | ((uint16_t)byte(c, value >> 8) << 8);
}

/* Legacy banks are consumed only while loading a track. Their rendered notes
 * stay in the melodic track; no bank storage survives migration. */
static bool discard_bank(seq_module_t *m, module_codec_t *c) {
    if (!c->read || !c->allow_bank) return false;
    uint8_t selected = byte(c, 0);
    int8_t transpose = (int8_t)byte(c, 0), inversion = (int8_t)byte(c, 0);
    if (selected >= SEQ_CHORD_SLOTS || transpose < -24 || transpose > 24 || inversion < -3 || inversion > 3) return false;
    for (uint8_t i = 0; i < SEQ_CHORD_SLOTS; i++) {
        uint8_t count = byte(c, 0), velocity = byte(c, 0), previous = 0;
        uint16_t length = word(c, 0);
        if (count > 8 || !velocity || velocity > 127 || !length || length > SEQ_MAX_STEPS) return false;
        for (uint8_t n = 0; n < count; n++) {
            uint8_t pitch = byte(c, 0);
            if (pitch > 127 || (n && pitch <= previous)) return false;
            previous = pitch;
        }
    }
    seq_module_default(m, SEQ_MODULE_EUCLID);
    m->composed = true;
    m->rhythm = (seq_rhythm_component_t){.kind = 1};
    c->migrated = true;
    return c->ok;
}
static bool chord_palette_contract(const seq_module_t *m) {
    if (m->config.euclid.pitch_mode == 2) return true;
    for (uint8_t i = 0; i < 8; i++) if (m->config.euclid.qualities[i]) return true;
    return false;
}
static void palette_qualities_codec(seq_module_t *m, module_codec_t *c) {
    for (uint8_t i = 0; i < 8; i++) m->config.euclid.qualities[i] = byte(c, m->config.euclid.qualities[i]);
}
static bool component_codec(seq_module_t *m, module_codec_t *c) {
    m->composed               = true;
    seq_rhythm_component_t *r = &m->rhythm;
    r->kind                   = byte(c, r->kind);
    if (byte(c, 1) != 1) return false;
    if (r->kind == 2) {
        r->enabled         = byte(c, r->enabled);
        r->steps           = word(c, r->steps);
        r->fills           = word(c, r->fills);
        r->rotation        = word(c, r->rotation);
        r->probability     = byte(c, r->probability);
        r->accent          = byte(c, r->accent);
        r->ratchet         = byte(c, r->ratchet);
        r->template_active = byte(c, r->template_active);
        r->template_mask   = word(c, r->template_mask);
    } else if (r->kind != 1)
        return false;
    uint8_t source = byte(c, m->kind + 3);
    uint8_t contract = byte(c, source == 3 && chord_palette_contract(m) ? 3 : source == 3 && !m->config.euclid.pitch_mode ? 2 : 1);
    if (source < 3 || source > 4 || (contract != 1 && !(source == 3 && (contract == 2 || contract == 3)))) return false;
    m->kind = source - 3;
    if (source == 4) {
        if (!seq_rhythm_valid(&m->rhythm) || m->rhythm.ratchet) return false;
        return discard_bank(m, c);
    }
    {
        seq_module_euclid_t *e = &m->config.euclid;
        e->root                = byte(c, e->root);
        e->count               = byte(c, e->count);
        e->pin                 = byte(c, e->pin);
        for (uint8_t n = 0; n < 8; n++)
            e->pool[n] = (int8_t)byte(c, e->pool[n]);
    }
    if (m->kind == SEQ_MODULE_EUCLID) m->config.euclid.pitch_mode = contract >= 2 ? byte(c, m->config.euclid.pitch_mode) : 1;
    if (contract < 3 && m->config.euclid.pitch_mode > 1) return false;
    if (contract == 3) palette_qualities_codec(m, c);
    return c->ok && seq_module_valid(m);
}
static bool codec(seq_module_t *m, module_codec_t *c) {
    if (byte(c, 'M') != 'M') return false;
    uint8_t version = byte(c, m->composed ? 2 : chord_palette_contract(m) ? 4 : m->kind == SEQ_MODULE_EUCLID && !m->config.euclid.pitch_mode ? 3 : 1);
    if (version == 2) return component_codec(m, c);
    if (version != 1 && version != 3 && version != 4) return false;
    m->kind = byte(c, m->kind);
    if (m->kind == SEQ_MODULE_CHORD_BANK) return version == 1 && discard_bank(m, c);
    if (m->kind == SEQ_MODULE_EUCLID) {
        seq_module_euclid_t *e = &m->config.euclid;
        e->enabled             = byte(c, e->enabled);
        e->steps               = word(c, e->steps);
        e->fills               = word(c, e->fills);
        e->rotation            = word(c, e->rotation);
        e->probability         = byte(c, e->probability);
        e->accent              = byte(c, e->accent);
        e->ratchet             = byte(c, e->ratchet);
        e->root                = byte(c, e->root);
        e->count               = byte(c, e->count);
        e->pin                 = byte(c, e->pin);
        e->template_active     = byte(c, e->template_active);
        e->template_mask       = word(c, e->template_mask);
        for (uint8_t i = 0; i < 8; i++)
            e->pool[i] = (int8_t)byte(c, (uint8_t)e->pool[i]);
    } else
        return false;
    if (version == 3 && m->kind != SEQ_MODULE_EUCLID) return false;
    if (m->kind == SEQ_MODULE_EUCLID) m->config.euclid.pitch_mode = version >= 3 ? byte(c, m->config.euclid.pitch_mode) : 1;
    if (version < 4 && m->config.euclid.pitch_mode > 1) return false;
    if (version == 4) palette_qualities_codec(m, c);
    return c->ok && seq_module_valid(m);
}
bool seq_module_encode(const seq_module_t *m, uint8_t *data, uint16_t *size) {
    if (!seq_module_valid(m)) return false;
    seq_module_t   copy = *m;
    module_codec_t c    = {.data = data, .size = *size, .ok = true};
    if (!codec(&copy, &c)) return false;
    *size = c.pos;
    return true;
}
bool seq_module_decode(seq_module_t *m, const uint8_t *data, uint16_t size) {
    seq_module_t   decoded = {0};
    module_codec_t c       = {.data = (uint8_t *)data, .size = size, .read = true, .ok = true};
    if (!codec(&decoded, &c) || c.pos != size) return false;
    *m = decoded;
    return true;
}


bool seq_module_migrate_bank(seq_module_t *m, const uint8_t *data, uint16_t size) {
    seq_module_t decoded = {0};
    module_codec_t c = {.data = (uint8_t *)data, .size = size, .read = true, .ok = true, .allow_bank = true};
    if (!codec(&decoded, &c) || !c.migrated || c.pos != size) return false;
    *m = decoded;
    return true;
}
