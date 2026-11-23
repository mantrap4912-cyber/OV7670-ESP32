/**
 * @file ov7670_types.h
 * @brief Public enums, pin map and configuration structures.
 *
 * Part of the OV7670-ESP32-NonFIFO library. MIT licensed.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Output frame size. Values map to sensor windowing + DCW settings. */
typedef enum {
    OV7670_FRAMESIZE_QQCIF = 0, /*!<  88 x  72 */
    OV7670_FRAMESIZE_QQVGA,     /*!< 160 x 120 */
    OV7670_FRAMESIZE_QCIF,      /*!< 176 x 144 */
    OV7670_FRAMESIZE_QVGA,      /*!< 320 x 240 */
    OV7670_FRAMESIZE_CIF,       /*!< 352 x 288 */
    OV7670_FRAMESIZE_VGA,       /*!< 640 x 480 - needs PSRAM for a full frame */
    OV7670_FRAMESIZE_COUNT
} ov7670_framesize_t;

/** @brief Pixel format emitted by the sensor on D0..D7. */
typedef enum {
    OV7670_PIXFORMAT_RGB565 = 0, /*!< 2 bytes/pixel, big-endian RGB565 */
    OV7670_PIXFORMAT_RGB555,     /*!< 2 bytes/pixel */
    OV7670_PIXFORMAT_RGB444,     /*!< 2 bytes/pixel */
    OV7670_PIXFORMAT_YUV422,     /*!< 2 bytes/pixel, YUYV order */
    OV7670_PIXFORMAT_GRAYSCALE,  /*!< 1 byte/pixel, extracted from YUV422 Y */
    OV7670_PIXFORMAT_BAYER,      /*!< 1 byte/pixel, raw Bayer BGGR */
    OV7670_PIXFORMAT_COUNT
} ov7670_pixformat_t;

/** @brief How captured lines are delivered to the application. */
typedef enum {
    /** Whole frame is assembled in a RAM buffer. Needs width*height*bpp bytes. */
    OV7670_CAPTURE_FULL_FRAME = 0,
    /** Each line is handed to a callback as it arrives; no frame buffer is
     *  allocated. This is the low-RAM path used for direct-to-TFT streaming. */
    OV7670_CAPTURE_LINE_CALLBACK
} ov7670_capture_mode_t;

/** @brief Fixed-UV special effects (see OV7670Camera::setSpecialEffect). */
typedef enum {
    OV7670_EFFECT_NONE = 0,
    OV7670_EFFECT_BW,        /*!< Black and white */
    OV7670_EFFECT_SEPIA,
    OV7670_EFFECT_BLUISH,
    OV7670_EFFECT_GREENISH,
    OV7670_EFFECT_REDDISH,
    OV7670_EFFECT_COUNT
} ov7670_effect_t;

/** @brief White-balance presets (see OV7670Camera::setLightMode). */
typedef enum {
    OV7670_WB_AUTO = 0,
    OV7670_WB_SUNNY,
    OV7670_WB_CLOUDY,
    OV7670_WB_OFFICE,
    OV7670_WB_HOME,
    OV7670_WB_COUNT
} ov7670_wb_mode_t;

/** @brief Night-mode frame-rate reduction limit. */
typedef enum {
    OV7670_NIGHT_OFF = 0,
    OV7670_NIGHT_DIV2,
    OV7670_NIGHT_DIV4,
    OV7670_NIGHT_DIV8
} ov7670_night_mode_t;

/** @brief Banding filter target, for suppressing mains flicker. */
typedef enum {
    OV7670_BANDING_AUTO = 0,
    OV7670_BANDING_50HZ,
    OV7670_BANDING_60HZ,
    OV7670_BANDING_DISABLED
} ov7670_banding_t;

/**
 * @brief GPIO assignment for every signal between the ESP32 and the sensor.
 *
 * Set @ref reset and @ref pwdn to -1 only if the breakout ties them to a fixed
 * level in hardware. Leaving them electrically floating is one of the most
 * common reasons an OV7670 fails to respond over SCCB.
 */
typedef struct {
    int8_t sioc;    /*!< SCCB clock  (ESP32 -> sensor) */
    int8_t siod;    /*!< SCCB data   (bidirectional) */
    int8_t vsync;   /*!< Vertical sync   (sensor -> ESP32) */
    int8_t href;    /*!< Horizontal ref  (sensor -> ESP32) */
    int8_t pclk;    /*!< Pixel clock     (sensor -> ESP32) */
    int8_t xclk;    /*!< System clock    (ESP32 -> sensor) */
    int8_t reset;   /*!< Active-low reset, or -1 if hard-wired */
    int8_t pwdn;    /*!< Active-high power down, or -1 if hard-wired */
    int8_t d[8];    /*!< Parallel data D0..D7; d[0] is the LSB */
} OV7670Pins;

/** @brief Runtime configuration passed to OV7670Camera::begin(). */
typedef struct {
    ov7670_framesize_t    frame_size;    /*!< Output resolution */
    ov7670_pixformat_t    pixel_format;  /*!< Output pixel format */
    ov7670_capture_mode_t capture_mode;  /*!< Full frame or per-line callback */

    uint32_t xclk_freq_hz;   /*!< XCLK to feed the sensor, 8..24 MHz. 16 MHz is safe. */
    uint32_t sccb_freq_hz;   /*!< SCCB bus clock. The OV7670 is happiest at 100 kHz. */

    /** Sensor internal clock prescaler written to CLKRC[5:0].
     *  Internal clock = XCLK / ((value + 1) * 2)... see docs/TIMING.md.
     *  Larger values slow PCLK down, which makes capture more forgiving on
     *  long or unshielded ribbon cables. 0 keeps the fastest rate. */
    uint8_t  clkrc_prescaler;

    bool use_psram;      /*!< Allocate frame buffers in PSRAM when available */
    uint8_t fb_count;    /*!< 1 = single buffer, 2 = double buffer (streaming) */

    /* Signal polarity. Some breakout boards buffer or invert the sync lines,
     * so these are configurable rather than hard-coded. Defaults suit a bare
     * OV7670 module wired directly. */
    bool vsync_invert;   /*!< true if VSYNC is active low at the ESP32 pin */
    bool href_invert;    /*!< true if HREF is active low at the ESP32 pin */
    bool pclk_invert;    /*!< true to sample data on the falling PCLK edge */

    /** Number of DMA nodes in the capture ring. Each node holds one line.
     *  More nodes give the CPU more slack before data is overwritten.
     *  4 is a good default; raise it if you see torn frames at VGA. */
    uint8_t dma_node_count;

    int8_t i2c_port;     /*!< I2C peripheral index, or -1 to bit-bang SCCB */
} OV7670Config;

/** @brief Cumulative capture statistics, useful for the stress-test example. */
typedef struct {
    uint32_t frames_captured;   /*!< Frames delivered to the application */
    uint32_t frames_dropped;    /*!< Frames discarded because no buffer was free */
    uint32_t frames_torn;       /*!< Frames whose line count did not match height */
    uint32_t dma_overruns;      /*!< DMA node reused before the CPU drained it */
    uint32_t sccb_errors;       /*!< SCCB transactions that NACKed or timed out */
    uint32_t last_frame_us;     /*!< Duration of the most recent frame */
    float    fps;               /*!< Rolling average frames per second */
} OV7670Stats;

/**
 * @brief Per-line delivery callback used by ::OV7670_CAPTURE_LINE_CALLBACK.
 *
 * @warning Called from a high-priority ISR-serviced context. Keep it short,
 *          mark it IRAM_ATTR, and never call blocking APIs from it.
 *
 * @param line       Pointer to the unpacked line, valid only for this call.
 * @param len        Length of the line in bytes.
 * @param line_index Zero-based row number within the frame.
 * @param ctx        Opaque pointer supplied when the callback was registered.
 */
typedef void (*ov7670_line_cb_t)(const uint8_t *line, size_t len,
                                 uint16_t line_index, void *ctx);

/** @brief Fill a config struct with library defaults (QQVGA RGB565 @ 16 MHz). */
void ov7670_config_defaults(OV7670Config *cfg);

/** @brief Pixel dimensions for a frame size. */
uint16_t ov7670_framesize_width(ov7670_framesize_t fs);
uint16_t ov7670_framesize_height(ov7670_framesize_t fs);

/** @brief Bytes per pixel for a pixel format (1 or 2). */
uint8_t ov7670_pixformat_bpp(ov7670_pixformat_t fmt);

/** @brief Total frame buffer size in bytes for a size/format pair. */
size_t ov7670_frame_bytes(ov7670_framesize_t fs, ov7670_pixformat_t fmt);

/** @brief Human-readable names, for logging and examples. */
const char *ov7670_framesize_name(ov7670_framesize_t fs);
const char *ov7670_pixformat_name(ov7670_pixformat_t fmt);

#ifdef __cplusplus
}
#endif
