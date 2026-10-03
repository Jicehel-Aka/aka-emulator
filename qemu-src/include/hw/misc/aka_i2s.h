/*
 * Gamebuino AKA - ESP32-S3 I2S TX (std mode) streaming to a shared audio ring.
 *
 * Only the transmit path used by the console is modelled: the driver programs a
 * circular GDMA OUT chain; this device consumes it in real time and publishes the
 * 16-bit samples in a shared file read by the front-end.
 */
#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/dma/esp_gdma.h"

#define TYPE_AKA_I2S "misc.aka.i2s"
#define AKA_I2S(obj) OBJECT_CHECK(AkaI2sState, (obj), TYPE_AKA_I2S)

#define AKA_I2S0_BASE  0x6000F000
#define AKA_I2S1_BASE  0x6002D000
#define AKA_I2S_SIZE   0x1000

#define AKA_AUDIO_MAGIC       0x41414B41u   /* 'AKAA' */
#define AKA_AUDIO_RING_FRAMES 16384

typedef struct AkaAudioShared {
    uint32_t magic;
    uint32_t rate;
    uint32_t channels;
    uint32_t ring_frames;
    volatile uint32_t write_pos;    /* in frames, free running */
    uint32_t pad[11];
    int16_t ring[AKA_AUDIO_RING_FRAMES];
} AkaAudioShared;

typedef struct AkaI2sState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t regs[AKA_I2S_SIZE / 4];
    ESPGdmaState *gdma;
    uint32_t periph;                /* GDMA_I2S0 / GDMA_I2S1 */
    char *audio_path;
    uint32_t default_rate;
    AkaAudioShared *out;
    QEMUTimer timer;
    int64_t last_ns;
    double frac;
    bool running;
    uint32_t chan;
    bool have_chan;
} AkaI2sState;
