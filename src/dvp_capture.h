/**
 * @file dvp_capture.h
 * @brief Hardware DMA capture of the 8-bit parallel (DVP) pixel stream.
 *
 * This is the part that makes a FIFO-less OV7670 work at all. The sensor
 * clocks out one byte per PCLK edge with no buffering whatsoever, at rates of
 * several MHz; software polling of the data pins cannot keep up, and there is
 * no external AL422B to hold the data for us. Instead a peripheral samples
 * D0..D7 in hardware, gated by VSYNC and HREF, and pushes the result into RAM
 * over DMA. That peripheral *is* the FIFO.
 *
 * Two back ends implement this interface, selected at compile time:
 *
 *   - dvp_capture_i2s.cpp    Classic ESP32. Uses the I2S0 peripheral in
 *                            camera/DVP slave mode. Fully implemented and the
 *                            primary target of this library.
 *   - dvp_capture_lcdcam.cpp ESP32-S3. Uses the LCD_CAM peripheral in camera
 *                            receive mode with GDMA. See the warning in that
 *                            file before relying on it.
 *
 * ESP32-S2 is not supported; its camera peripheral differs from both of the
 * above. Use Espressif's esp32-camera component there.
 *
 * Part of the OV7670-ESP32-NonFIFO library. MIT licensed.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ov7670_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief How captured bus bytes are turned into stored bytes. */
typedef enum {
    /** Store every byte the sensor emitted. Used for all 2-byte formats and
     *  for raw Bayer. */
    DVP_FILTER_COPY = 0,
    /** Store only the first byte of each pair. On a YUV422 stream that is the
     *  luma channel, which yields 8-bit grayscale for half the memory. */
    DVP_FILTER_Y_ONLY
} dvp_filter_t;

/** @brief Capture back-end configuration. */
typedef struct {
    int8_t pin_d[8];        /*!< D0..D7, d[0] = LSB */
    int8_t pin_vsync;
    int8_t pin_href;
    int8_t pin_pclk;

    bool vsync_invert;      /*!< Invert VSYNC in the GPIO matrix */
    bool href_invert;       /*!< Invert HREF in the GPIO matrix */
    bool pclk_invert;       /*!< Sample on the falling PCLK edge */

    uint16_t width;         /*!< Output width in pixels */
    uint16_t height;        /*!< Output height in lines */
    uint8_t  bus_bpp;       /*!< Bytes the sensor emits per pixel (1 or 2) */
    uint8_t  out_bpp;       /*!< Bytes stored per pixel (1 or 2) */
    dvp_filter_t filter;    /*!< Unpacking rule */

    uint8_t dma_node_count; /*!< DMA ring depth in lines (>= 2, 4 recommended) */

    /** Destination frame buffers. Provide 1 for single buffering or 2 for
     *  double buffering. Ignored when @ref line_cb is set. May live in PSRAM;
     *  the DMA engine never touches them directly. */
    uint8_t *fb[2];
    uint8_t  fb_count;
    size_t   fb_size;       /*!< Size of each buffer in bytes */

    /** Optional per-line callback. When non-NULL no frame buffer is used and
     *  each line is delivered as it is unpacked. */
    ov7670_line_cb_t line_cb;
    void            *line_cb_ctx;
} dvp_config_t;

/** @brief Allocate DMA memory, configure the peripheral and route the pins. */
esp_err_t dvp_init(const dvp_config_t *cfg);

/** @brief Tear everything down and free DMA memory. */
void dvp_deinit(void);

/**
 * @brief Capture exactly one frame, blocking until it is complete.
 *
 * Waits for the next VSYNC boundary before starting, so the returned frame is
 * always whole rather than a mid-frame fragment.
 *
 * @param timeout_ms Give up after this long.
 * @retval ESP_OK             A complete frame is in the buffer.
 * @retval ESP_ERR_TIMEOUT    No VSYNC or the frame never finished. Usually
 *                            means XCLK is not running or PCLK is not wired.
 */
esp_err_t dvp_capture_frame(uint32_t timeout_ms);

/** @brief Start free-running capture. Frames arrive back to back. */
esp_err_t dvp_start_continuous(void);

/** @brief Stop free-running capture. */
void dvp_stop(void);

/**
 * @brief Wait for the next complete frame in continuous mode.
 * @param[out] out_index Index of the frame buffer that is now readable.
 */
esp_err_t dvp_wait_frame(uint32_t timeout_ms, uint8_t *out_index);

/** @brief Number of lines actually received in the most recent frame. */
uint16_t dvp_last_line_count(void);

/** @brief Fill in DMA-related counters; the caller owns the struct. */
void dvp_get_stats(OV7670Stats *stats);

/** @brief Reset all counters to zero. */
void dvp_reset_stats(void);

/** @brief Name of the compiled-in back end, for logging. */
const char *dvp_backend_name(void);

#ifdef __cplusplus
}
#endif
