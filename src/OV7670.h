/**
 * @file OV7670.h
 * @brief Umbrella header for the OV7670-ESP32-NonFIFO library.
 *
 * Include this one header from a sketch:
 *
 *     #include <OV7670.h>
 *
 * Everything else is available through it. The individual headers are still
 * usable directly if you only want, say, the register definitions.
 *
 * Part of the OV7670-ESP32-NonFIFO library. MIT licensed.
 * https://github.com/mantrap4912-cyber/OV7670-ESP32
 */

#pragma once

#define OV7670_LIB_VERSION_MAJOR 1
#define OV7670_LIB_VERSION_MINOR 0
#define OV7670_LIB_VERSION_PATCH 0
#define OV7670_LIB_VERSION       "1.0.0"

#include "ov7670_types.h"
#include "ov7670_regs.h"
#include "ov7670_presets.h"
#include "sccb.h"
#include "xclk.h"
#include "dvp_capture.h"
#include "OV7670Camera.h"

/* -------------------------------------------------------------------------
 * Small helpers that examples keep needing.
 * ---------------------------------------------------------------------- */

/**
 * @brief Swap the byte order of an RGB565 buffer in place.
 *
 * The OV7670 emits RGB565 big-endian (high byte first). Most SPI TFT drivers
 * and most PC-side decoders want little-endian. One pass over the buffer fixes
 * it; doing it here rather than in the DMA unpack keeps the capture path free
 * of format-specific branches.
 */
static inline void ov7670_swap_bytes(uint8_t *buf, size_t len)
{
    for (size_t i = 0; i + 1 < len; i += 2) {
        uint8_t t = buf[i];
        buf[i] = buf[i + 1];
        buf[i + 1] = t;
    }
}

/**
 * @brief Convert one RGB565 pixel (big-endian, as the sensor emits it) to
 *        separate 8-bit channels.
 */
static inline void ov7670_rgb565_to_rgb888(const uint8_t *px,
                                           uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint16_t v = (uint16_t)((px[0] << 8) | px[1]);
    /* Replicate the high bits into the low ones so full-scale stays full. */
    uint8_t r5 = (uint8_t)((v >> 11) & 0x1F);
    uint8_t g6 = (uint8_t)((v >> 5)  & 0x3F);
    uint8_t b5 = (uint8_t)( v        & 0x1F);
    *r = (uint8_t)((r5 << 3) | (r5 >> 2));
    *g = (uint8_t)((g6 << 2) | (g6 >> 4));
    *b = (uint8_t)((b5 << 3) | (b5 >> 2));
}

/**
 * @brief Convert one YUYV pair (4 bytes, 2 pixels) to two RGB888 pixels.
 * @param yuyv Four bytes: Y0 U Y1 V.
 * @param rgb  Six bytes out: R0 G0 B0 R1 G1 B1.
 */
static inline void ov7670_yuyv_to_rgb888(const uint8_t *yuyv, uint8_t *rgb)
{
    for (int i = 0; i < 2; i++) {
        int y = yuyv[i * 2];
        int u = (int)yuyv[1] - 128;
        int v = (int)yuyv[3] - 128;
        int r = y + ((91881 * v) >> 16);
        int g = y - ((22554 * u + 46802 * v) >> 16);
        int b = y + ((116130 * u) >> 16);
        rgb[i * 3 + 0] = (uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r));
        rgb[i * 3 + 1] = (uint8_t)(g < 0 ? 0 : (g > 255 ? 255 : g));
        rgb[i * 3 + 2] = (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b));
    }
}
