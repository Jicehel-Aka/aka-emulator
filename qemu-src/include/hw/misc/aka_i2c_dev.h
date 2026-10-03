/*
 * Gamebuino AKA - I2C slave devices: PCF8574-like button expanders (0x38 and
 * 0x3F) and the TAS2505 audio codec/amplifier (0x18, register stub).
 *
 * The button/joystick state is read from a small file shared with the PC
 * front-end ("input-path", see AkaInputShared).
 */
#pragma once

#include "hw/i2c/i2c.h"

#define TYPE_AKA_EXPANDER "aka.expander"
#define TYPE_AKA_TAS2505  "aka.tas2505"

/* Layout of the shared input file (little endian) */
typedef struct AkaInputShared {
    uint32_t magic;       /* 'AKAI' 0x49414B41 */
    uint32_t keys;        /* EXPANDER_KEY_* bits, 1 = pressed */
    int32_t  joyx_mv;     /* 0..3300 */
    int32_t  joyy_mv;
    int32_t  battery_mv;
    uint32_t pad[3];
} AkaInputShared;

#define AKA_INPUT_MAGIC 0x49414B41u

/* Maps the shared input file; returns NULL when it is missing. Never fails hard. */
const volatile AkaInputShared *aka_input_map(const char *path);
