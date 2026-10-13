#include "eeprom.h"
#include <stdint.h>
#include <string.h>

/* QMK's generic update helper uses a block-sized VLA. A 3 KiB project exceeds
 * this board's 2 KiB process stack. Compare in small pieces, then let the
 * wear-leveling driver update its existing cache in one write. */
void __wrap_eeprom_update_block(const void *buf, void *addr, size_t len) {
    uint8_t        previous[64];
    const uint8_t *source = buf;
    for (size_t offset = 0; offset < len;) {
        size_t count = len - offset > sizeof(previous) ? sizeof(previous) : len - offset;
        eeprom_read_block(previous, (const void *)((uintptr_t)addr + offset), count);
        if (memcmp(source + offset, previous, count)) {
            eeprom_write_block(buf, addr, len);
            return;
        }
        offset += count;
    }
}
