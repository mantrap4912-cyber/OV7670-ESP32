/**
 * @file OV7670Camera.h
 * @brief High-level driver for a FIFO-less OV7670 wired directly to an ESP32.
 *
 * Typical use:
 * @code
 * OV7670Camera cam;
 * OV7670Pins pins = OV7670_PINS_DEFAULT;
 * OV7670Config cfg;
 * ov7670_config_defaults(&cfg);
 * cfg.frame_size = OV7670_FRAMESIZE_QQVGA;
 *
 * if (cam.begin(pins, cfg) != ESP_OK) { ... }
 * if (cam.captureFrame()) {
 *     size_t len;
 *     uint8_t *fb = cam.getFrameBuffer(&len);
 *     ...
 *     cam.releaseFrameBuffer();
 * }
 * @endcode
 *
 * Part of the OV7670-ESP32-NonFIFO library. MIT licensed.
 */

#pragma once

#include "ov7670_types.h"
#include "ov7670_regs.h"
#include "esp_err.h"

/**
 * @brief A workable default pin map for an ESP32 DevKit-C (38-pin).
 *
 * These are only defaults; nothing in the library depends on them. Strapping
 * pins (0, 2, 5, 12, 15) and the flash pins (6-11) are avoided. See
 * docs/WIRING.md for the reasoning and for alternatives.
 */
#define OV7670_PINS_DEFAULT { \
    /* sioc  */ 27,           \
    /* siod  */ 26,           \
    /* vsync */ 25,           \
    /* href  */ 23,           \
    /* pclk  */ 22,           \
    /* xclk  */ 21,           \
    /* reset */ 19,           \
    /* pwdn  */ 18,           \
    /* d0..7 */ { 34, 35, 32, 33, 4, 36, 39, 13 } }

class OV7670Camera {
public:
    OV7670Camera();
    ~OV7670Camera();

    /* --------------------------------------------------------------------
     * Lifecycle
     * ----------------------------------------------------------------- */

    /**
     * @brief Bring up XCLK, SCCB and the DMA capture path, in that order.
     *
     * The ordering matters: the sensor is clocked entirely from XCLK, so the
     * clock is started and allowed to settle before the first SCCB byte goes
     * out. RESET and PWDN are driven to their inactive levels first, since a
     * floating RESET is a common cause of a sensor that never enumerates.
     *
     * @retval ESP_OK              Camera is ready.
     * @retval ESP_ERR_NOT_FOUND   SCCB works but PID/VER are wrong - wrong
     *                             sensor, or SIOD/SIOC swapped.
     * @retval ESP_ERR_TIMEOUT     No SCCB response at all - check XCLK first.
     * @retval ESP_ERR_NO_MEM      Not enough RAM for the requested frame size.
     */
    esp_err_t begin(const OV7670Pins &pins, const OV7670Config &cfg);

    /** @brief Stop capture, release DMA memory, stop XCLK and free the bus. */
    void end();

    /** @brief true once begin() has succeeded. */
    bool isReady() const { return _ready; }

    /* --------------------------------------------------------------------
     * Capture
     * ----------------------------------------------------------------- */

    /**
     * @brief Capture one complete frame, blocking until it lands.
     * @param timeout_ms Give up after this long.
     * @return true if a whole frame was received.
     */
    bool captureFrame(uint32_t timeout_ms = 1000);

    /**
     * @brief Pointer to the most recently completed frame.
     * @param[out] len Receives the frame length in bytes. May be NULL.
     * @return NULL in line-callback mode, where no frame buffer exists.
     */
    uint8_t *getFrameBuffer(size_t *len = nullptr);

    /** @brief Release the frame back to the driver (required when double
     *         buffering, harmless otherwise). */
    void releaseFrameBuffer();

    /**
     * @brief Start free-running capture.
     *
     * In ::OV7670_CAPTURE_LINE_CALLBACK mode the callback registered with
     * onLine() is invoked for every line. Otherwise frames accumulate in the
     * frame buffers and waitFrame() blocks until one is ready.
     */
    bool startStreaming();

    /** @brief Stop free-running capture. */
    void stopStreaming();

    /** @brief Block until the next streamed frame is ready. */
    bool waitFrame(uint32_t timeout_ms = 1000);

    /** @brief Register the per-line callback used in line-callback mode.
     *         Must be called before begin(). */
    void onLine(ov7670_line_cb_t cb, void *ctx = nullptr);

    /* --------------------------------------------------------------------
     * Format
     * ----------------------------------------------------------------- */

    /** @brief Change resolution. Reallocates buffers and restarts capture. */
    bool setResolution(ov7670_framesize_t fs);

    /** @brief Change pixel format. Reallocates buffers and restarts capture. */
    bool setPixelFormat(ov7670_pixformat_t fmt);

    uint16_t width()  const { return _width; }
    uint16_t height() const { return _height; }
    size_t   frameBytes() const { return _frame_bytes; }
    ov7670_framesize_t frameSize()   const { return _cfg.frame_size; }
    ov7670_pixformat_t pixelFormat() const { return _cfg.pixel_format; }

    /* --------------------------------------------------------------------
     * Image controls (all take effect on the next frame)
     * ----------------------------------------------------------------- */

    bool setMirror(bool enable);
    bool setVFlip(bool enable);
    /** @param level -4 (darkest) to +4 (brightest); 0 is neutral. */
    bool setBrightness(int level);
    /** @param level -4 (flattest) to +4 (hardest); 0 is neutral. */
    bool setContrast(int level);
    /** @param level -4 (grey) to +4 (vivid); 0 is neutral. */
    bool setSaturation(int level);
    bool setSpecialEffect(ov7670_effect_t effect);
    bool setLightMode(ov7670_wb_mode_t mode);
    bool setNightMode(ov7670_night_mode_t mode);
    bool setBandingFilter(ov7670_banding_t banding);
    bool setAutoExposure(bool enable);
    bool setAutoGain(bool enable);
    bool setAutoWhiteBalance(bool enable);
    /** @param gain 0..255, only meaningful with auto gain off. */
    bool setGain(uint8_t gain);
    /** @param exposure 0..65535, only meaningful with auto exposure off. */
    bool setExposure(uint16_t exposure);
    /** @brief Emit the sensor's internal colour bar instead of the image.
     *         The fastest way to prove the capture path independently of
     *         optics, lighting or lens focus. */
    bool setColorBar(bool enable);

    /* --------------------------------------------------------------------
     * Low level
     * ----------------------------------------------------------------- */

    bool writeReg(uint8_t reg, uint8_t value);
    bool readReg(uint8_t reg, uint8_t *value);
    bool updateReg(uint8_t reg, uint8_t mask, uint8_t value);
    /** @brief Write a table terminated by {0xFF, 0xFF}. */
    bool writeRegTable(const ov7670_reg_entry_t *table);

    /** @brief COM7 software reset followed by the full default register set. */
    bool softReset();

    /** @brief Read PID/VER and verify they match the datasheet. */
    bool probe(uint8_t *pid = nullptr, uint8_t *ver = nullptr);

    /** @brief Measured XCLK, for the acceptance check in TASKS.MD. */
    uint32_t xclkFrequency() const;

    /** @brief Cumulative capture statistics. */
    void getStats(OV7670Stats *stats) const;
    void resetStats();

    /** @brief Name of the DMA back end in use. */
    const char *backendName() const;

    /** @brief Dump the whole sensor register bank to the log, for debugging. */
    void dumpRegisters();

private:
    bool applyFormat();
    bool applyWindow();
    bool applyMatrixScaled(int level);
    esp_err_t allocateBuffers();
    void freeBuffers();
    esp_err_t startCapturePath();

    OV7670Pins   _pins;
    OV7670Config _cfg;

    uint16_t _width = 0;
    uint16_t _height = 0;
    uint8_t  _bus_bpp = 2;
    uint8_t  _out_bpp = 2;
    size_t   _frame_bytes = 0;

    uint8_t *_fb[2] = { nullptr, nullptr };
    uint8_t  _fb_count = 1;
    uint8_t  _fb_current = 0;
    bool     _fb_held = false;

    ov7670_line_cb_t _line_cb = nullptr;
    void            *_line_ctx = nullptr;

    bool _ready = false;
    bool _streaming = false;

    /* Cached shadow copies so read-modify-write does not need a bus read for
     * registers we are the only writer of. */
    uint8_t _mvfp_shadow = 0;
    int     _saturation = 0;
};
