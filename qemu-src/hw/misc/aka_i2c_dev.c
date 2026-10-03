/*
 * Gamebuino AKA - I2C slaves (button expanders, TAS2505 stub)
 *
 * GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/misc/aka_shm.h"
#include <sys/stat.h>
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/misc/aka_i2c_dev.h"

/* Same masks as gb_ll_common.h */
#define K_RUN    0x0002
#define K_MENU   0x0004
#define K_R1     0x0040
#define K_L1     0x0080

static const AkaInputShared aka_input_idle = { .magic = AKA_INPUT_MAGIC,
    .joyx_mv = 1650, .joyy_mv = 1650, .battery_mv = 3900 };

const volatile AkaInputShared *aka_input_map(const char *path)
{
    if (!path || !*path) {
        return NULL;
    }
    void *p = aka_shm_map(path, sizeof(AkaInputShared), true);
    if (!p) {
        return NULL;
    }
    AkaInputShared *in = p;
    if (in->magic != AKA_INPUT_MAGIC) {      /* freshly created file */
        *in = aka_input_idle;
    }
    return in;
}

/* ------------------------------------------------------------------ */
/* PCF8574-like expander                                               */
/* ------------------------------------------------------------------ */

typedef struct AkaExpanderState {
    I2CSlave parent_obj;
    uint8_t out;                      /* last byte written (pins driven high) */
    uint32_t half;                    /* 0: low byte (0x38), 1: high byte (0x3F) */
    char *input_path;
    const volatile AkaInputShared *in;
} AkaExpanderState;

OBJECT_DECLARE_SIMPLE_TYPE(AkaExpanderState, AKA_EXPANDER)

static uint8_t aka_expander_pins(AkaExpanderState *s)
{
    const uint32_t keys = s->in ? s->in->keys : 0;

    if (s->half) {
        /* all keys active low, pulled up */
        return (uint8_t)(0xFF & ~(keys >> 8));
    } else {
        uint8_t v = s->out | (K_MENU | K_R1 | K_L1);
        v &= ~(keys & (K_MENU | K_R1 | K_L1));
        /* RUN is active high: low when released, high when pressed */
        v &= ~K_RUN;
        if (keys & K_RUN) {
            v |= K_RUN;
        }
        return v;
    }
}

static int aka_expander_event(I2CSlave *i2c, enum i2c_event event)
{
    return 0;
}

static uint8_t aka_expander_recv(I2CSlave *i2c)
{
    return aka_expander_pins(AKA_EXPANDER(i2c));
}

static int aka_expander_send(I2CSlave *i2c, uint8_t data)
{
    AKA_EXPANDER(i2c)->out = data;
    return 0;
}

static void aka_expander_realize(DeviceState *dev, Error **errp)
{
    AkaExpanderState *s = AKA_EXPANDER(dev);

    s->out = 0xff;
    s->in = aka_input_map(s->input_path);
}

static Property aka_expander_props[] = {
    DEFINE_PROP_UINT32("half", AkaExpanderState, half, 0),
    DEFINE_PROP_STRING("input-path", AkaExpanderState, input_path),
    DEFINE_PROP_END_OF_LIST(),
};

static void aka_expander_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->realize = aka_expander_realize;
    device_class_set_props(dc, aka_expander_props);
    k->event = aka_expander_event;
    k->recv = aka_expander_recv;
    k->send = aka_expander_send;
}

static const TypeInfo aka_expander_info = {
    .name = TYPE_AKA_EXPANDER,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(AkaExpanderState),
    .class_init = aka_expander_class_init,
};

/* ------------------------------------------------------------------ */
/* TAS2505 stub: paged register file, status bits report "powered/ready" */
/* ------------------------------------------------------------------ */

typedef struct AkaTas2505State {
    I2CSlave parent_obj;
    uint8_t regs[256][128];
    uint8_t page;
    uint8_t reg;
    int pos;                          /* bytes received in current write */
} AkaTas2505State;

OBJECT_DECLARE_SIMPLE_TYPE(AkaTas2505State, AKA_TAS2505)

static int aka_tas_event(I2CSlave *i2c, enum i2c_event event)
{
    AkaTas2505State *s = AKA_TAS2505(i2c);

    if (event == I2C_START_SEND) {
        s->pos = 0;
    }
    return 0;
}

static int aka_tas_send(I2CSlave *i2c, uint8_t data)
{
    AkaTas2505State *s = AKA_TAS2505(i2c);

    if (s->pos == 0) {
        s->reg = data & 0x7f;
    } else {
        if (s->reg == 0) {               /* register 0: page select */
            s->page = data;
        } else {
            s->regs[s->page][s->reg & 0x7f] = data;
        }
        s->reg = (s->reg + 1) & 0x7f;
    }
    s->pos++;
    return 0;
}

static uint8_t aka_tas_recv(I2CSlave *i2c)
{
    AkaTas2505State *s = AKA_TAS2505(i2c);
    uint8_t v;

    if (s->reg == 0) {
        v = s->page;
    } else if (s->page == 0 && (s->reg == 0x24 || s->reg == 0x26)) {
        v = 0xff;                         /* DAC/PGA flags: powered and settled */
    } else {
        v = s->regs[s->page][s->reg & 0x7f];
    }
    s->reg = (s->reg + 1) & 0x7f;
    return v;
}

static void aka_tas_class_init(ObjectClass *klass, void *data)
{
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = aka_tas_event;
    k->recv = aka_tas_recv;
    k->send = aka_tas_send;
}

static const TypeInfo aka_tas_info = {
    .name = TYPE_AKA_TAS2505,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(AkaTas2505State),
    .class_init = aka_tas_class_init,
};

static void aka_i2c_dev_register_types(void)
{
    type_register_static(&aka_expander_info);
    type_register_static(&aka_tas_info);
}

type_init(aka_i2c_dev_register_types)
