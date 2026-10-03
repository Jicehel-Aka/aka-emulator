/*
 * Gamebuino AKA - ESP32-S3 I2S TX streaming (see aka_i2s.h)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/misc/aka_i2s.h"
#include "hw/misc/aka_shm.h"

#define I2S_TX_CONF        0x24
#define I2S_TX_CONF1       0x2C
#define I2S_TX_CLKM_CONF   0x34
#define I2S_TX_CLKM_DIV    0x3C
#define TX_RESET           (1u << 0)
#define TX_START           (1u << 2)

#define TICK_NS            (2 * 1000 * 1000)    /* 2 ms */

static AkaAudioShared *aka_audio_map(const char *path)
{
    if (!path || !*path) {
        return NULL;
    }
    return aka_shm_map(path, sizeof(AkaAudioShared), true);
}

/* Sample rate (frames/s) from the clock registers, or the default when it looks wrong. */
static uint32_t aka_i2s_rate(AkaI2sState *s)
{
    const uint32_t clkm = s->regs[I2S_TX_CLKM_CONF / 4];
    const uint32_t div = s->regs[I2S_TX_CLKM_DIV / 4];
    const uint32_t conf1 = s->regs[I2S_TX_CONF1 / 4];
    static const double src[4] = { 40e6, 120e6, 160e6, 0 };
    const double fsrc = src[(clkm >> 27) & 3];
    const uint32_t n = clkm & 0xFF;
    const uint32_t z = div & 0x1FF, y = (div >> 9) & 0x1FF, x = (div >> 18) & 0x1FF;
    const uint32_t bck = (conf1 >> 7) & 0x3F;
    const uint32_t bits = ((conf1 >> 19) & 0x1F) + 1;

    if (fsrc > 0 && n > 0 && bck > 0 && bits > 0) {
        /* fractional divider N + y/x ... (x == 0 : integer) */
        double d = n;
        if (x && z) {
            d += (y && y <= x) ? (double) z / x : 0;   /* approximation, validated below */
        }
        const double rate = fsrc / d / bck / (2.0 * bits);
        if (rate > 7000 && rate < 100000) {
            return (uint32_t) rate;
        }
    }
    return s->default_rate;
}

static void aka_i2s_tick(void *opaque)
{
    AkaI2sState *s = opaque;
    const int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (!s->have_chan && !esp_gdma_find_out_channel(s->gdma, s->periph, &s->chan)) {
        goto next;
    }
    s->have_chan = true;
    esp_gdma_out_take_start(s->gdma, s->chan);

    if (s->running) {
        const uint32_t rate = aka_i2s_rate(s);
        s->frac += (double) (now - s->last_ns) * rate / 1e9;
        uint32_t frames = (uint32_t) s->frac;
        s->frac -= frames;
        while (frames) {
            int16_t buf[512];
            const uint32_t chunk = MIN(frames, 512u);
            const uint32_t got = esp_gdma_out_stream(s->gdma, s->chan, (uint8_t *) buf, chunk * 2) / 2;

            if (!got) {
                break;                      /* nothing queued: under-run, leave the ring idle */
            }
            if (s->out) {
                uint32_t wp = s->out->write_pos;
                for (uint32_t i = 0; i < got; i++) {
                    s->out->ring[(wp + i) % AKA_AUDIO_RING_FRAMES] = buf[i];
                }
                s->out->rate = rate;
                __atomic_store_n(&s->out->write_pos, wp + got, __ATOMIC_RELEASE);
            }
            frames -= got;
        }
    }
    s->last_ns = now;
next:
    timer_mod(&s->timer, now + TICK_NS);
}

static uint64_t aka_i2s_read(void *opaque, hwaddr addr, unsigned int size)
{
    AkaI2sState *s = AKA_I2S(opaque);

    return addr < AKA_I2S_SIZE ? s->regs[addr / 4] : 0;
}

static void aka_i2s_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    AkaI2sState *s = AKA_I2S(opaque);

    if (addr >= AKA_I2S_SIZE) {
        return;
    }
    s->regs[addr / 4] = (uint32_t) value;
    if (getenv("AKA_I2S_LOG")) {
        fprintf(stderr, "[i2s%u] W 0x%03lx = 0x%08lx\n", s->periph, (unsigned long) addr, (unsigned long) value);
    }
    if (addr == 0x20) {
        s->regs[addr / 4] &= ~(1u << 8);     /* RX_UPDATE self clearing */
    }
    if (addr == I2S_TX_CONF) {
        s->regs[addr / 4] &= ~(TX_RESET | (1u << 8));      /* TX_RESET / TX_UPDATE self clearing */
        const bool start = value & TX_START;
        if (start && !s->out) {
            s->out = aka_audio_map(s->audio_path);
            if (s->out) {
                s->out->magic = AKA_AUDIO_MAGIC;
                s->out->channels = 1;
                s->out->ring_frames = AKA_AUDIO_RING_FRAMES;
                s->out->rate = s->default_rate;
                s->out->write_pos = 0;
            }
        }
        if (start && !s->running) {
            s->last_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            s->frac = 0;
        }
        s->running = start;
    }
}

static const MemoryRegionOps aka_i2s_ops = {
    .read = aka_i2s_read,
    .write = aka_i2s_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void aka_i2s_init(Object *obj)
{
    AkaI2sState *s = AKA_I2S(obj);

    memory_region_init_io(&s->iomem, obj, &aka_i2s_ops, s, TYPE_AKA_I2S, AKA_I2S_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void aka_i2s_realize(DeviceState *dev, Error **errp)
{
    AkaI2sState *s = AKA_I2S(dev);

    if (!s->gdma) {
        error_setg(errp, "aka.i2s: gdma link must be set");
        return;
    }
    timer_init_ns(&s->timer, QEMU_CLOCK_VIRTUAL, aka_i2s_tick, s);
    timer_mod(&s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + TICK_NS);
}

static Property aka_i2s_props[] = {
    DEFINE_PROP_LINK("gdma", AkaI2sState, gdma, TYPE_ESP_GDMA, ESPGdmaState *),
    DEFINE_PROP_UINT32("periph", AkaI2sState, periph, GDMA_I2S0),
    DEFINE_PROP_STRING("audio-path", AkaI2sState, audio_path),
    DEFINE_PROP_UINT32("default-rate", AkaI2sState, default_rate, 44100),
    DEFINE_PROP_END_OF_LIST(),
};

static void aka_i2s_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aka_i2s_realize;
    device_class_set_props(dc, aka_i2s_props);
}

static const TypeInfo aka_i2s_info = {
    .name = TYPE_AKA_I2S,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AkaI2sState),
    .instance_init = aka_i2s_init,
    .class_init = aka_i2s_class_init,
};

static void aka_i2s_register_types(void)
{
    type_register_static(&aka_i2s_info);
}

type_init(aka_i2s_register_types)
