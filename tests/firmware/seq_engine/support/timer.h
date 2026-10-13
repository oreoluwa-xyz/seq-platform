#pragma once
#include <stdint.h>
#define TIMER_DIFF_32(a, b) ((uint32_t)((a) - (b)))
static inline bool timer_expired32(uint32_t now, uint32_t due) {
    return (int32_t)(now - due) >= 0;
}
uint32_t timer_read32(void);
