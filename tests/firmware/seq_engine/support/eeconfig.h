#pragma once
#include <stdbool.h>
#include <stdint.h>
bool eeconfig_is_user_datablock_valid(void);
void eeconfig_read_user_datablock(void *data);
void eeconfig_update_user_datablock(const void *data);
