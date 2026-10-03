/*
 * Gamebuino AKA - ESP32-S3 LCD_CAM (i8080 LCD mode) + ST7789V panel
 *
 * The controller reproduces the register interface used by ESP-IDF's
 * esp_lcd i80 driver: command phase, DMA data phase (through the GDMA),
 * byte/bit swizzling and the "transaction done" interrupt. The panel is
 * modelled behind it and drawn into a framebuffer that is shared with the
 * PC front-end through a memory-mapped file (property "fb-path").
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "hw/dma/esp_gdma.h"

#define TYPE_AKA_LCDCAM "misc.aka.lcdcam"
#define AKA_LCDCAM(obj) OBJECT_CHECK(AkaLcdCamState, (obj), TYPE_AKA_LCDCAM)

#define AKA_LCDCAM_BASE     0x60041000
#define AKA_LCDCAM_SIZE     0x100

/*
 * Shared framebuffer file layout (little endian):
 *   0x00 u32 magic 'AKAF'     0x04 u32 version (1)
 *   0x08 u32 width            0x0c u32 height   (current orientation)
 *   0x10 u32 frame counter    0x14 u32 flags    (AKA_FB_FLAG_*)
 *   0x40 pixels, RGB565, row stride = width, at most 320x320
 */
#define AKA_FB_MAGIC        0x46414B41u
#define AKA_FB_VERSION      1
#define AKA_FB_HEADER_SIZE  0x40
#define AKA_FB_MAX_DIM      320
#define AKA_FB_FILE_SIZE    (AKA_FB_HEADER_SIZE + AKA_FB_MAX_DIM * AKA_FB_MAX_DIM * 2)

#define AKA_FB_FLAG_DISPLAY_ON  (1u << 0)
#define AKA_FB_FLAG_INVERSION   (1u << 1)   /* INVON was sent */
#define AKA_FB_FLAG_BGR         (1u << 2)   /* MADCTL RGB/BGR bit */
#define AKA_FB_FLAG_SLEEP_OUT   (1u << 3)

typedef struct AkaLcdCamState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;
    ESPGdmaState *gdma;          /* set by the machine, like SHA/AES */
    QEMUTimer *done_timer;

    uint32_t regs[AKA_LCDCAM_SIZE / 4];
    uint32_t int_raw;
    uint32_t int_ena;
    bool busy;

    /* Panel (ST7789V) */
    uint8_t  cur_cmd;
    uint8_t  param_buf[4];
    uint32_t param_cnt;
    uint16_t pix_hi;
    bool     pix_have_hi;
    uint16_t col_start, col_end;
    uint16_t row_start, row_end;
    uint16_t cur_x, cur_y;
    uint8_t  madctl;
    uint8_t  colmod;
    uint32_t flags;
    uint32_t frame_count;

    /* Shared framebuffer */
    char *fb_path;
    uint8_t *fb;                 /* mmap of the shared file, or NULL */
    uint16_t *local_fb;          /* used when no file is configured */
    uint32_t width, height;
} AkaLcdCamState;
