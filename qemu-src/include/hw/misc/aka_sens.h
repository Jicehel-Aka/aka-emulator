/*
 * Gamebuino AKA - ESP32-S3 SENS block (SAR ADC1 and ADC2)
 *
 * Minimal model: a conversion on ADC1 completes immediately and returns the
 * raw 12-bit value configured for the selected pad (joystick X/Y, battery).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"

#define TYPE_AKA_SENS "misc.aka.sens"
#define AKA_SENS(obj) OBJECT_CHECK(AkaSensState, (obj), TYPE_AKA_SENS)

#define AKA_SENS_BASE       0x60008800
#define AKA_SENS_SIZE       0x400
#define AKA_SENS_ADC1_PADS  12

typedef struct AkaSensState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t regs[AKA_SENS_SIZE / 4];
    /* Raw 12-bit value returned for each ADC1 pad (property adc1-padN) */
    uint32_t pad_value[AKA_SENS_ADC1_PADS];
    /* Same for ADC2 (properties adc2-padN) */
    uint32_t pad2_value[AKA_SENS_ADC1_PADS];
    /* Shared input file (joystick / battery), see aka_i2c_dev.c */
    char *input_path;
    volatile int32_t *in;           /* mmap of the shared input file (8 words) */
} AkaSensState;
