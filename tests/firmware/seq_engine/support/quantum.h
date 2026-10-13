#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "keycodes.h"
#define SAFE_RANGE QK_USER
#define MATRIX_ROWS 6
#define MATRIX_COLS 16
#define PROGMEM
#define MOD_BIT(kc) (1u << ((kc) & 7))
#define pgm_read_word(p) (*(const uint16_t *)(p))
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define memcpy_P memcpy
#define NO_LED 255
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
typedef struct {
    uint8_t r, g, b;
} RGB;
typedef struct {
    struct {
        struct {
            uint8_t row, col;
        } key;
        bool     pressed;
        uint16_t time;
    } event;
} keyrecord_t;
uint8_t get_mods(void);
void    tap_code16(uint16_t keycode);
