/*
 * Gamebuino AKA - ESP32-S3 SENS block (SAR ADC1 only)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/misc/aka_sens.h"
#include "hw/misc/aka_shm.h"

#define AKA_INPUT_MAGIC  0x49414B41u
/* Board V4+: joystick X on ADC1 ch5, Y on ch4, battery (divided by 2) on ch3 */
#define PAD_BATT 3
#define PAD_JOYY 4
#define PAD_JOYX 5
/* Calibrated mV (IDF curve fitting, 12 dB, blank eFuse codes) for raw = 128 * i */
static const uint16_t aka_cal_mv[] = {
    0, 126, 249, 371, 491, 609, 726, 844, 960, 1076, 1192, 1309, 1427, 1544, 1662, 1778,
    1895, 2012, 2128, 2240, 2352, 2461, 2568, 2669, 2765, 2857, 2942, 3018, 3086, 3144, 3190, 3225
};

/* Pad voltage (mV) -> raw 12-bit, inverse of the calibration table */
static uint32_t aka_mv_to_raw(int32_t mv)
{
    const int n = ARRAY_SIZE(aka_cal_mv);

    if (mv <= 0) {
        return 0;
    }
    for (int i = 1; i < n; i++) {
        if (mv <= aka_cal_mv[i]) {
            const uint32_t d = aka_cal_mv[i] - aka_cal_mv[i - 1];
            return 128 * (i - 1) + (d ? (128 * (mv - aka_cal_mv[i - 1])) / d : 0);
        }
    }
    return 128 * (n - 1);      /* saturate just below 4095 (calibration blows up there) */
}

static void aka_sens_map_input(AkaSensState *s)
{
    if (s->in || !s->input_path) {
        return;
    }
    void *p = aka_shm_map(s->input_path, 32, false);
    if (p) {
        s->in = p;
    }
}

/*
 * SENS_SAR_MEAS1_CTRL2_REG / SENS_SAR_MEAS2_CTRL2_REG, see soc/sens_reg.h.
 * Both registers share the same layout.
 */
#define SENS_MEAS1_CTRL2        0xC
#define SENS_MEAS2_CTRL2        0x30
#define MEAS1_EN_PAD_S          19
#define MEAS1_EN_PAD_M          (0xFFFu << MEAS1_EN_PAD_S)
#define MEAS1_START_SAR         (1u << 17)
#define MEAS1_DONE_SAR          (1u << 16)
#define MEAS1_DATA_M            0xFFFFu

static uint32_t aka_sens_meas(AkaSensState *s, hwaddr off,
                              const uint32_t *pad_value)
{
    uint32_t reg = s->regs[off / 4];
    uint32_t pads = (reg & MEAS1_EN_PAD_M) >> MEAS1_EN_PAD_S;
    uint32_t data = 0;

    if (pads) {
        const unsigned pad = ctz32(pads) % AKA_SENS_ADC1_PADS;
        data = pad_value[pad] & 0xFFF;
        aka_sens_map_input(s);
        if (s->in && (uint32_t) s->in[0] == AKA_INPUT_MAGIC && pad_value == s->pad_value) {
            /* in[2]=joyx_mv in[3]=joyy_mv in[4]=battery_mv */
            if (pad == PAD_JOYX) {
                data = aka_mv_to_raw(s->in[2]);
            } else if (pad == PAD_JOYY) {
                data = aka_mv_to_raw(s->in[3]);
            } else if (pad == PAD_BATT) {
                data = aka_mv_to_raw(s->in[4] / 2);
            }
        }
    }
    reg = (reg & ~(MEAS1_DATA_M | MEAS1_DONE_SAR)) | data;
    if (reg & MEAS1_START_SAR) {
        reg |= MEAS1_DONE_SAR;
    }
    return reg;
}

static uint64_t aka_sens_read(void *opaque, hwaddr addr, unsigned int size)
{
    AkaSensState *s = AKA_SENS(opaque);

    if (addr >= AKA_SENS_SIZE) {
        return 0;
    }
    if (addr == SENS_MEAS1_CTRL2) {
        return aka_sens_meas(s, addr, s->pad_value);
    }
    if (addr == SENS_MEAS2_CTRL2) {
        return aka_sens_meas(s, addr, s->pad2_value);
    }
    return s->regs[addr / 4];
}

static void aka_sens_write(void *opaque, hwaddr addr, uint64_t value,
                           unsigned int size)
{
    AkaSensState *s = AKA_SENS(opaque);

    if (addr >= AKA_SENS_SIZE) {
        return;
    }
    s->regs[addr / 4] = (uint32_t)value;
}

static const MemoryRegionOps aka_sens_ops = {
    .read = aka_sens_read,
    .write = aka_sens_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void aka_sens_init(Object *obj)
{
    AkaSensState *s = AKA_SENS(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &aka_sens_ops, s,
                          TYPE_AKA_SENS, AKA_SENS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void aka_sens_reset(DeviceState *dev)
{
    AkaSensState *s = AKA_SENS(dev);

    memset(s->regs, 0, sizeof(s->regs));
}

static Property aka_sens_props[] = {
    DEFINE_PROP_STRING("input-path", AkaSensState, input_path),
    DEFINE_PROP_UINT32("adc1-pad0", AkaSensState, pad_value[0], 2048),
    DEFINE_PROP_UINT32("adc1-pad1", AkaSensState, pad_value[1], 2048),
    DEFINE_PROP_UINT32("adc1-pad2", AkaSensState, pad_value[2], 2048),
    DEFINE_PROP_UINT32("adc1-pad3", AkaSensState, pad_value[3], 2048),
    DEFINE_PROP_UINT32("adc1-pad4", AkaSensState, pad_value[4], 2048),
    DEFINE_PROP_UINT32("adc1-pad5", AkaSensState, pad_value[5], 2048),
    DEFINE_PROP_UINT32("adc1-pad6", AkaSensState, pad_value[6], 2048),
    DEFINE_PROP_UINT32("adc1-pad7", AkaSensState, pad_value[7], 2048),
    DEFINE_PROP_UINT32("adc1-pad8", AkaSensState, pad_value[8], 2048),
    DEFINE_PROP_UINT32("adc1-pad9", AkaSensState, pad_value[9], 2048),
    DEFINE_PROP_UINT32("adc2-pad0", AkaSensState, pad2_value[0], 2048),
    DEFINE_PROP_UINT32("adc2-pad1", AkaSensState, pad2_value[1], 2048),
    DEFINE_PROP_UINT32("adc2-pad2", AkaSensState, pad2_value[2], 2048),
    DEFINE_PROP_UINT32("adc2-pad3", AkaSensState, pad2_value[3], 2048),
    DEFINE_PROP_UINT32("adc2-pad4", AkaSensState, pad2_value[4], 2048),
    DEFINE_PROP_UINT32("adc2-pad5", AkaSensState, pad2_value[5], 2048),
    DEFINE_PROP_UINT32("adc2-pad6", AkaSensState, pad2_value[6], 2048),
    DEFINE_PROP_UINT32("adc2-pad7", AkaSensState, pad2_value[7], 2048),
    DEFINE_PROP_UINT32("adc2-pad8", AkaSensState, pad2_value[8], 2048),
    DEFINE_PROP_UINT32("adc2-pad9", AkaSensState, pad2_value[9], 2048),
    DEFINE_PROP_END_OF_LIST(),
};

static void aka_sens_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, aka_sens_reset);
    device_class_set_props(dc, aka_sens_props);
}

static const TypeInfo aka_sens_info = {
    .name = TYPE_AKA_SENS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AkaSensState),
    .instance_init = aka_sens_init,
    .class_init = aka_sens_class_init,
};

static void aka_sens_register_types(void)
{
    type_register_static(&aka_sens_info);
}

type_init(aka_sens_register_types)
