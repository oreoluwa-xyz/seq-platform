#pragma once
#include <stddef.h>
void eeprom_read_block(void *buf, const void *addr, size_t len);
void eeprom_write_block(const void *buf, void *addr, size_t len);
void __wrap_eeprom_update_block(const void *buf, void *addr, size_t len);
