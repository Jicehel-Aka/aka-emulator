/*
 * Gamebuino AKA - ESP32-S3 LCD_CAM (i8080 LCD mode) + ST7789V panel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/misc/aka_lcdcam.h"
#include "hw/misc/aka_shm.h"

/* LCD_CAM registers (offsets) used by the esp_lcd i80 driver */
#define REG_LCD_USER        0x14
#define REG_LCD_MISC        0x18
#define REG_LCD_CMD_VAL     0x28
#define REG_INT_ENA         0x64
#define REG_INT_RAW         0x68
#define REG_INT_ST          0x6c
#define REG_INT_CLR         0x70

/* LCD_USER bits */
#define USER_2BYTE_EN       (1u << 23)
#define USER_8BITS_ORDER    (1u << 19)
#define USER_UPDATE         (1u << 20)
#define USER_BIT_ORDER      (1u << 21)
#define USER_DOUT           (1u << 24)
#define USER_CMD            (1u << 26)
#define USER_START          (1u << 27)
#define USER_RESET          (1u << 28)
#define USER_CMD_2_CYCLE    (1u << 31)

/* LCD_MISC bits */
#define MISC_AFIFO_RESET    (1u << 27)
#define MISC_CD_DATA_SET    (1u << 28)
#define MISC_CD_CMD_SET     (1u << 30)
#define MISC_CD_IDLE_EDGE   (1u << 31)

#define INT_TRANS_DONE      (1u << 1)

/* ST7789V commands / MADCTL bits */
#define ST_SWRESET  0x01
#define ST_SLPIN    0x10
#define ST_SLPOUT   0x11
#define ST_INVOFF   0x20
#define ST_INVON    0x21
#define ST_DISPOFF  0x28
#define ST_DISPON   0x29
#define ST_CASET    0x2a
#define ST_RASET    0x2b
#define ST_RAMWR    0x2c
#define ST_MADCTL   0x36
#define ST_COLMOD   0x3a

#define MADCTL_MV   0x20
#define MADCTL_MX   0x40
#define MADCTL_MY   0x80
#define MADCTL_BGR  0x08

static bool aka_lcd_debug(void)
{
    static int dbg = -1;

    if (dbg < 0) {
        dbg = getenv("AKA_LCD_DEBUG") ? 1 : 0;
    }
    return dbg;
}

/* ------------------------------------------------------------------ */
/* Shared framebuffer                                                  */
/* ------------------------------------------------------------------ */

static uint16_t *aka_fb_pixels(AkaLcdCamState *s)
{
    return s->fb ? (uint16_t *)(s->fb + AKA_FB_HEADER_SIZE) : s->local_fb;
}

static void aka_fb_publish(AkaLcdCamState *s)
{
    if (!s->fb) {
        return;
    }
    stl_le_p(s->fb + 0x00, AKA_FB_MAGIC);
    stl_le_p(s->fb + 0x04, AKA_FB_VERSION);
    stl_le_p(s->fb + 0x08, s->width);
    stl_le_p(s->fb + 0x0c, s->height);
    stl_le_p(s->fb + 0x10, s->frame_count);
    stl_le_p(s->fb + 0x14, s->flags);
}

static void aka_panel_set_orientation(AkaLcdCamState *s)
{
    uint32_t w = (s->madctl & MADCTL_MV) ? 320 : 240;
    uint32_t h = (s->madctl & MADCTL_MV) ? 240 : 320;

    if (w != s->width || h != s->height) {
        s->width = w;
        s->height = h;
        memset(aka_fb_pixels(s), 0, AKA_FB_MAX_DIM * AKA_FB_MAX_DIM * 2);
    }
    if (s->madctl & MADCTL_BGR) {
        s->flags |= AKA_FB_FLAG_BGR;
    } else {
        s->flags &= ~AKA_FB_FLAG_BGR;
    }
    aka_fb_publish(s);
}

static void aka_panel_reset(AkaLcdCamState *s)
{
    s->cur_cmd = 0;
    s->param_cnt = 0;
    s->pix_have_hi = false;
    s->madctl = 0;
    s->colmod = 0x66;
    s->flags = 0;
    s->col_start = 0;
    s->col_end = 239;
    s->row_start = 0;
    s->row_end = 319;
    s->cur_x = 0;
    s->cur_y = 0;
    s->width = 0; /* force a clear in set_orientation */
    s->height = 0;
    aka_panel_set_orientation(s);
}

static void aka_panel_put_pixel(AkaLcdCamState *s, uint16_t pix)
{
    const bool mv = s->madctl & MADCTL_MV;
    /*
     * Display-space model: with MV=1, MX=1, MY=0 (the rotation used by the
     * Gamebuino AKA library) the image is upright; every other combination is
     * a mirror relative to that reference.
     */
    const bool flip_x = mv ? !(s->madctl & MADCTL_MX) : !!(s->madctl & MADCTL_MX);
    const bool flip_y = !!(s->madctl & MADCTL_MY);
    uint32_t x = flip_x ? (s->width - 1 - s->cur_x) : s->cur_x;
    uint32_t y = flip_y ? (s->height - 1 - s->cur_y) : s->cur_y;

    if (s->cur_x < s->width && s->cur_y < s->height) {
        aka_fb_pixels(s)[y * s->width + x] = pix;
    }
    if (++s->cur_x > s->col_end) {
        s->cur_x = s->col_start;
        if (++s->cur_y > s->row_end) {
            s->cur_y = s->row_start;
        }
    }
}

static void aka_panel_command(AkaLcdCamState *s, uint8_t cmd)
{
    s->cur_cmd = cmd;
    s->param_cnt = 0;

    switch (cmd) {
    case ST_SWRESET:
        aka_panel_reset(s);
        break;
    case ST_SLPIN:
        s->flags &= ~AKA_FB_FLAG_SLEEP_OUT;
        break;
    case ST_SLPOUT:
        s->flags |= AKA_FB_FLAG_SLEEP_OUT;
        break;
    case ST_INVOFF:
        s->flags &= ~AKA_FB_FLAG_INVERSION;
        break;
    case ST_INVON:
        s->flags |= AKA_FB_FLAG_INVERSION;
        break;
    case ST_DISPOFF:
        s->flags &= ~AKA_FB_FLAG_DISPLAY_ON;
        break;
    case ST_DISPON:
        s->flags |= AKA_FB_FLAG_DISPLAY_ON;
        break;
    case ST_RAMWR:
        s->cur_x = s->col_start;
        s->cur_y = s->row_start;
        s->pix_have_hi = false;
        break;
    default:
        break;
    }
    aka_fb_publish(s);
}

static void aka_panel_data(AkaLcdCamState *s, uint8_t b)
{
    switch (s->cur_cmd) {
    case ST_CASET:
    case ST_RASET:
        s->param_buf[s->param_cnt & 3] = b;
        s->param_cnt++;
        if (s->param_cnt == 2 || s->param_cnt == 4) {
            uint16_t v = (s->param_buf[(s->param_cnt - 2) & 3] << 8) |
                         s->param_buf[(s->param_cnt - 1) & 3];
            if (s->cur_cmd == ST_CASET) {
                if (s->param_cnt == 2) {
                    s->col_start = v;
                } else {
                    s->col_end = v;
                }
            } else {
                if (s->param_cnt == 2) {
                    s->row_start = v;
                } else {
                    s->row_end = v;
                }
            }
        }
        break;
    case ST_MADCTL:
        s->madctl = b;
        aka_panel_set_orientation(s);
        break;
    case ST_COLMOD:
        s->colmod = b;
        break;
    case ST_RAMWR:
        if (!s->pix_have_hi) {
            s->pix_hi = b;
            s->pix_have_hi = true;
        } else {
            aka_panel_put_pixel(s, (s->pix_hi << 8) | b);
            s->pix_have_hi = false;
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* LCD_CAM controller                                                  */
/* ------------------------------------------------------------------ */

static void aka_lcdcam_update_irq(AkaLcdCamState *s)
{
    qemu_set_irq(s->irq, (s->int_raw & s->int_ena) ? 1 : 0);
}

static inline uint8_t reverse_bits8(uint8_t v)
{
    v = (v >> 4) | (v << 4);
    v = ((v & 0xcc) >> 2) | ((v & 0x33) << 2);
    v = ((v & 0xaa) >> 1) | ((v & 0x55) << 1);
    return v;
}

static void aka_lcdcam_done(void *opaque)
{
    AkaLcdCamState *s = opaque;

    if (aka_lcd_debug()) {
        fprintf(stderr, "[lcdcam] done (ena=0x%x)\n", s->int_ena);
    }
    s->busy = false;
    s->regs[REG_LCD_USER / 4] &= ~USER_START;
    s->int_raw |= INT_TRANS_DONE;
    aka_lcdcam_update_irq(s);
}

static void aka_lcdcam_start(AkaLcdCamState *s)
{
    const uint32_t user = s->regs[REG_LCD_USER / 4];
    const uint32_t misc = s->regs[REG_LCD_MISC / 4];
    const bool dc_idle = !!(misc & MISC_CD_IDLE_EDGE);
    const bool dc_cmd = dc_idle ^ !!(misc & MISC_CD_CMD_SET);
    const bool dc_data = dc_idle ^ !!(misc & MISC_CD_DATA_SET);
    uint32_t nbytes = 0;

    if (aka_lcd_debug()) {
        fprintf(stderr, "[lcdcam] start user=0x%08x misc=0x%08x cmdval=0x%08x\n",
                user, misc, s->regs[REG_LCD_CMD_VAL / 4]);
    }

    if (user & USER_CMD) {
        const uint32_t v = s->regs[REG_LCD_CMD_VAL / 4];
        const int cycles = (user & USER_CMD_2_CYCLE) ? 2 : 1;

        for (int i = 0; i < cycles; i++) {
            /* 8-bit bus: cycle 0 sends value[7:0], cycle 1 value[23:16] */
            uint8_t b = (i == 0) ? (v & 0xff) : ((v >> 16) & 0xff);
            if (dc_cmd) {
                aka_panel_data(s, b);
            } else {
                aka_panel_command(s, b);
            }
            nbytes++;
        }
    }

    if ((user & USER_DOUT) && s->gdma) {
        uint32_t chan;

        if (esp_gdma_get_channel_periph(s->gdma, GDMA_LCDCAM,
                                        ESP_GDMA_OUT_IDX, &chan)) {
            uint32_t len = esp_gdma_out_chain_length(s->gdma, chan);
            if (aka_lcd_debug()) {
                fprintf(stderr, "[lcdcam]   data phase chan=%u len=%u cur_cmd=0x%02x\n",
                        chan, len, s->cur_cmd);
            }
            uint8_t *buf = g_malloc(len ? len : 1);

            if (len && esp_gdma_read_channel(s->gdma, chan, buf, len)) {
                const bool swap_pairs = (user & USER_8BITS_ORDER) &&
                                        !(user & USER_2BYTE_EN);
                const bool rev_bits = !!(user & USER_BIT_ORDER);
                const bool is_frame = (s->cur_cmd == ST_RAMWR);

                uint32_t i = 0;
                if (dc_data && is_frame && !rev_bits && !s->pix_have_hi) {
                    /* fast path: whole pixels straight into the framebuffer */
                    for (; i + 1 < len; i += 2) {
                        uint8_t hi = swap_pairs ? buf[i + 1] : buf[i];
                        uint8_t lo = swap_pairs ? buf[i] : buf[i + 1];
                        aka_panel_put_pixel(s, (hi << 8) | lo);
                    }
                }
                for (; i < len; i++) {
                    uint32_t idx = i;
                    if (swap_pairs && (i | 1) < len) {
                        idx = i ^ 1;
                    }
                    uint8_t b = buf[idx];
                    if (rev_bits) {
                        b = reverse_bits8(b);
                    }
                    if (dc_data) {
                        aka_panel_data(s, b);
                    } else {
                        aka_panel_command(s, b);
                    }
                }
                nbytes += len;
                if (is_frame) {
                    s->frame_count++;
                    aka_fb_publish(s);
                }
            }
            g_free(buf);
        }
    }

    /* 8-bit bus at ~20 MHz: ~50 ns per byte, plus a small fixed latency */
    s->busy = true;
    timer_mod(s->done_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                             5000 + (int64_t)nbytes * 50);
}

static uint64_t aka_lcdcam_read(void *opaque, hwaddr addr, unsigned int size)
{
    AkaLcdCamState *s = opaque;

    if (aka_lcd_debug() && (addr == REG_INT_ST || addr == REG_INT_RAW)) {
        fprintf(stderr, "[lcdcam] R 0x%02x raw=%x ena=%x\n", (unsigned)addr, s->int_raw, s->int_ena);
    }
    switch (addr) {
    case REG_INT_ENA:
        return s->int_ena;
    case REG_INT_RAW:
        return s->int_raw;
    case REG_INT_ST:
        return s->int_raw & s->int_ena;
    case REG_LCD_USER:
        return (s->regs[addr / 4] & ~USER_START) | (s->busy ? USER_START : 0);
    default:
        return addr < AKA_LCDCAM_SIZE ? s->regs[addr / 4] : 0;
    }
}

static void aka_lcdcam_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned int size)
{
    AkaLcdCamState *s = opaque;
    const uint32_t v = (uint32_t)value;

    if (aka_lcd_debug()) {
        fprintf(stderr, "[lcdcam] W 0x%02x = 0x%08x (busy=%d)\n", (unsigned)addr, v, s->busy);
    }

    switch (addr) {
    case REG_INT_ENA:
        s->int_ena = v & 0xf;
        aka_lcdcam_update_irq(s);
        break;
    case REG_INT_CLR:
        s->int_raw &= ~(v & 0xf);
        aka_lcdcam_update_irq(s);
        break;
    case REG_LCD_USER: {
        const bool was_started = s->busy;
        uint32_t nv = v & ~(USER_UPDATE | USER_RESET);   /* self-clearing */

        if (v & USER_RESET) {
            timer_del(s->done_timer);
            s->busy = false;
            nv &= ~USER_START;
        }
        s->regs[addr / 4] = nv;
        if ((nv & USER_START) && !was_started) {
            aka_lcdcam_start(s);
        } else if (!(nv & USER_START) && was_started) {
            /* lcd_ll_stop(): the done interrupt is still delivered */
        }
        break;
    }
    case REG_LCD_MISC:
        s->regs[addr / 4] = v & ~MISC_AFIFO_RESET;       /* self-clearing */
        break;
    default:
        if (addr < AKA_LCDCAM_SIZE) {
            s->regs[addr / 4] = v;
        }
        break;
    }
}

static const MemoryRegionOps aka_lcdcam_ops = {
    .read = aka_lcdcam_read,
    .write = aka_lcdcam_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void aka_lcdcam_reset(DeviceState *dev)
{
    AkaLcdCamState *s = AKA_LCDCAM(dev);

    timer_del(s->done_timer);
    memset(s->regs, 0, sizeof(s->regs));
    s->int_raw = 0;
    s->int_ena = 0;
    s->busy = false;
    aka_lcdcam_update_irq(s);
    aka_panel_reset(s);
}

static void aka_lcdcam_init(Object *obj)
{
    AkaLcdCamState *s = AKA_LCDCAM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &aka_lcdcam_ops, s,
                          TYPE_AKA_LCDCAM, AKA_LCDCAM_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void aka_lcdcam_realize(DeviceState *dev, Error **errp)
{
    AkaLcdCamState *s = AKA_LCDCAM(dev);

    s->done_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, aka_lcdcam_done, s);

    if (s->fb_path && s->fb_path[0]) {
        void *p = aka_shm_map(s->fb_path, AKA_FB_FILE_SIZE, true);
        if (!p) {
            error_setg(errp, "aka-lcdcam: cannot map '%s'", s->fb_path);
            return;
        }
        s->fb = p;
        memset(s->fb, 0, AKA_FB_FILE_SIZE);
    } else {
        s->local_fb = g_malloc0(AKA_FB_MAX_DIM * AKA_FB_MAX_DIM * 2);
    }
    aka_panel_reset(s);
}

static Property aka_lcdcam_props[] = {
    DEFINE_PROP_STRING("fb-path", AkaLcdCamState, fb_path),
    DEFINE_PROP_END_OF_LIST(),
};

static void aka_lcdcam_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aka_lcdcam_realize;
    device_class_set_legacy_reset(dc, aka_lcdcam_reset);
    device_class_set_props(dc, aka_lcdcam_props);
}

static const TypeInfo aka_lcdcam_info = {
    .name = TYPE_AKA_LCDCAM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AkaLcdCamState),
    .instance_init = aka_lcdcam_init,
    .class_init = aka_lcdcam_class_init,
};

static void aka_lcdcam_register_types(void)
{
    type_register_static(&aka_lcdcam_info);
}

type_init(aka_lcdcam_register_types)
