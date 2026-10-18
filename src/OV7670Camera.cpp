/**
 * @file OV7670Camera.cpp
 * @brief High-level driver implementation.
 *
 * Part of the OV7670-ESP32-NonFIFO library. MIT licensed.
 */

#include "OV7670Camera.h"
#include "ov7670_presets.h"
#include "ov7670_log.h"
#include "sccb.h"
#include "xclk.h"
#include "dvp_capture.h"

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"

/* LEDC resources used for XCLK. Change these if they clash with your sketch. */
#ifndef OV7670_XCLK_LEDC_TIMER
#define OV7670_XCLK_LEDC_TIMER   0
#endif
#ifndef OV7670_XCLK_LEDC_CHANNEL
#define OV7670_XCLK_LEDC_CHANNEL 0
#endif

/* =========================================================================
 * Free helpers declared in ov7670_types.h
 * ====================================================================== */

extern "C" void ov7670_config_defaults(OV7670Config *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->frame_size      = OV7670_FRAMESIZE_QQVGA;
    cfg->pixel_format    = OV7670_PIXFORMAT_RGB565;
    cfg->capture_mode    = OV7670_CAPTURE_FULL_FRAME;
    cfg->xclk_freq_hz    = 16000000;
    cfg->sccb_freq_hz    = 100000;
    cfg->clkrc_prescaler = 1;   /* halve the internal clock; gentler PCLK */
    cfg->use_psram       = true;
    cfg->fb_count        = 1;
    cfg->vsync_invert    = false;
    cfg->href_invert     = false;
    cfg->pclk_invert     = false;
    cfg->dma_node_count  = 4;
    cfg->i2c_port        = 0;
}

extern "C" uint16_t ov7670_framesize_width(ov7670_framesize_t fs)
{
    if (fs >= OV7670_FRAMESIZE_COUNT) return 0;
    return ov7670_windows[fs].width;
}

extern "C" uint16_t ov7670_framesize_height(ov7670_framesize_t fs)
{
    if (fs >= OV7670_FRAMESIZE_COUNT) return 0;
    return ov7670_windows[fs].height;
}

extern "C" uint8_t ov7670_pixformat_bpp(ov7670_pixformat_t fmt)
{
    switch (fmt) {
    case OV7670_PIXFORMAT_GRAYSCALE:
    case OV7670_PIXFORMAT_BAYER:
        return 1;
    default:
        return 2;
    }
}

extern "C" size_t ov7670_frame_bytes(ov7670_framesize_t fs, ov7670_pixformat_t fmt)
{
    return (size_t)ov7670_framesize_width(fs) *
           (size_t)ov7670_framesize_height(fs) *
           (size_t)ov7670_pixformat_bpp(fmt);
}

extern "C" const char *ov7670_framesize_name(ov7670_framesize_t fs)
{
    static const char *names[OV7670_FRAMESIZE_COUNT] = {
        "QQCIF", "QQVGA", "QCIF", "QVGA", "CIF", "VGA"
    };
    return (fs < OV7670_FRAMESIZE_COUNT) ? names[fs] : "?";
}

extern "C" const char *ov7670_pixformat_name(ov7670_pixformat_t fmt)
{
    static const char *names[OV7670_PIXFORMAT_COUNT] = {
        "RGB565", "RGB555", "RGB444", "YUV422", "GRAYSCALE", "BAYER"
    };
    return (fmt < OV7670_PIXFORMAT_COUNT) ? names[fmt] : "?";
}

/* =========================================================================
 * Construction
 * ====================================================================== */

OV7670Camera::OV7670Camera()
{
    memset(&_pins, -1, sizeof(_pins));
    ov7670_config_defaults(&_cfg);
}

OV7670Camera::~OV7670Camera()
{
    end();
}

void OV7670Camera::onLine(ov7670_line_cb_t cb, void *ctx)
{
    _line_cb = cb;
    _line_ctx = ctx;
}

/* =========================================================================
 * Buffers
 * ====================================================================== */

esp_err_t OV7670Camera::allocateBuffers()
{
    freeBuffers();

    if (_cfg.capture_mode == OV7670_CAPTURE_LINE_CALLBACK) {
        _fb_count = 0;
        return ESP_OK;   /* no frame buffer at all - that is the whole point */
    }

    _fb_count = (_cfg.fb_count >= 2) ? 2 : 1;

    uint32_t caps = MALLOC_CAP_8BIT;
#if CONFIG_SPIRAM_SUPPORT || CONFIG_ESP32_SPIRAM_SUPPORT || CONFIG_SPIRAM
    if (_cfg.use_psram) caps |= MALLOC_CAP_SPIRAM;
#endif

    for (uint8_t i = 0; i < _fb_count; i++) {
        _fb[i] = (uint8_t *)heap_caps_malloc(_frame_bytes, caps);
        if (!_fb[i] && (caps & MALLOC_CAP_SPIRAM)) {
            /* PSRAM absent or exhausted - fall back to internal RAM. */
            _fb[i] = (uint8_t *)heap_caps_malloc(_frame_bytes, MALLOC_CAP_8BIT);
        }
        if (!_fb[i]) {
            OV7670_LOGE("frame buffer %u of %u bytes could not be allocated "
                        "(largest free block: %u bytes)",
                        i, (unsigned)_frame_bytes,
                        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            if (_frame_bytes > 100000) {
                OV7670_LOGE("a %s %s frame needs %u bytes; a board without "
                            "PSRAM cannot hold one - use QVGA or smaller, or "
                            "switch to OV7670_CAPTURE_LINE_CALLBACK",
                            ov7670_framesize_name(_cfg.frame_size),
                            ov7670_pixformat_name(_cfg.pixel_format),
                            (unsigned)_frame_bytes);
            }
            freeBuffers();
            return ESP_ERR_NO_MEM;
        }
        memset(_fb[i], 0, _frame_bytes);
    }

    OV7670_LOGI("allocated %u frame buffer(s) of %u bytes",
                _fb_count, (unsigned)_frame_bytes);
    return ESP_OK;
}

void OV7670Camera::freeBuffers()
{
    for (int i = 0; i < 2; i++) {
        if (_fb[i]) { heap_caps_free(_fb[i]); _fb[i] = nullptr; }
    }
    _fb_count = 0;
    _fb_current = 0;
    _fb_held = false;
}

/* =========================================================================
 * Bring-up
 * ====================================================================== */

esp_err_t OV7670Camera::begin(const OV7670Pins &pins, const OV7670Config &cfg)
{
    if (_ready) end();

    _pins = pins;
    _cfg = cfg;
    if (_cfg.dma_node_count < 2) _cfg.dma_node_count = 4;
    if (_cfg.fb_count == 0) _cfg.fb_count = 1;
    if (_cfg.sccb_freq_hz == 0) _cfg.sccb_freq_hz = 100000;

    if (_cfg.frame_size >= OV7670_FRAMESIZE_COUNT ||
        _cfg.pixel_format >= OV7670_PIXFORMAT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    OV7670_LOGI("OV7670-ESP32-NonFIFO starting, back end: %s", dvp_backend_name());

    /* ---- 1. Power and reset lines -------------------------------------
     * Drive them; do not leave them floating. PWDN low = powered up,
     * RESET low = held in reset. */
    if (_pins.pwdn >= 0) {
        gpio_reset_pin((gpio_num_t)_pins.pwdn);
        gpio_set_direction((gpio_num_t)_pins.pwdn, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)_pins.pwdn, 1);   /* powered down for now */
    }
    if (_pins.reset >= 0) {
        gpio_reset_pin((gpio_num_t)_pins.reset);
        gpio_set_direction((gpio_num_t)_pins.reset, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)_pins.reset, 0);  /* held in reset */
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    /* ---- 2. XCLK, before anything else talks to the sensor -------------
     * The OV7670 has no oscillator. Until XCLK runs, its SCCB slave is not
     * clocked and every register access will fail. */
    esp_err_t rc = xclk_start(_pins.xclk, _cfg.xclk_freq_hz,
                              OV7670_XCLK_LEDC_TIMER, OV7670_XCLK_LEDC_CHANNEL);
    if (rc != ESP_OK) return rc;

    /* ---- 3. Release power down, then reset ----------------------------- */
    if (_pins.pwdn >= 0) {
        gpio_set_level((gpio_num_t)_pins.pwdn, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (_pins.reset >= 0) {
        gpio_set_level((gpio_num_t)_pins.reset, 1);
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    /* ---- 4. SCCB ------------------------------------------------------- */
    rc = sccb_init(_pins.sioc, _pins.siod, _cfg.sccb_freq_hz, _cfg.i2c_port);
    if (rc != ESP_OK) { xclk_stop(); return rc; }

    uint8_t pid = 0, ver = 0;
    rc = sccb_probe(&pid, &ver);
    if (rc != ESP_OK) {
        OV7670_LOGE("sensor not detected (PID=0x%02X VER=0x%02X)", pid, ver);
        OV7670_LOGE("checklist: XCLK on GPIO%d measured %u Hz? SIOC/SIOD not "
                    "swapped? pull-ups on SIOC/SIOD? RESET released?",
                    _pins.xclk, (unsigned)xclk_actual_freq());
        sccb_deinit();
        xclk_stop();
        return rc;
    }
    OV7670_LOGI("OV7670 found: PID=0x%02X VER=0x%02X", pid, ver);

    /* ---- 5. Register initialisation ------------------------------------ */
    if (!softReset()) {
        sccb_deinit();
        xclk_stop();
        return ESP_FAIL;
    }

    /* ---- 6. Capture path ----------------------------------------------- */
    rc = startCapturePath();
    if (rc != ESP_OK) {
        sccb_deinit();
        xclk_stop();
        return rc;
    }

    _ready = true;
    OV7670_LOGI("ready: %s %s, %ux%u, %u bytes per frame",
                ov7670_framesize_name(_cfg.frame_size),
                ov7670_pixformat_name(_cfg.pixel_format),
                _width, _height, (unsigned)_frame_bytes);
    return ESP_OK;
}

esp_err_t OV7670Camera::startCapturePath()
{
    const ov7670_win_t *win = &ov7670_windows[_cfg.frame_size];
    _width  = win->width;
    _height = win->height;
    _out_bpp = ov7670_pixformat_bpp(_cfg.pixel_format);
    /* Grayscale is carried as YUV422 on the wire: two bus bytes per pixel,
     * one stored byte per pixel. Everything else stores what it receives. */
    _bus_bpp = (_cfg.pixel_format == OV7670_PIXFORMAT_GRAYSCALE) ? 2 : _out_bpp;
    _frame_bytes = (size_t)_width * _height * _out_bpp;

    esp_err_t rc = allocateBuffers();
    if (rc != ESP_OK) return rc;

    dvp_config_t dcfg = {};
    memcpy(dcfg.pin_d, _pins.d, sizeof(dcfg.pin_d));
    dcfg.pin_vsync = _pins.vsync;
    dcfg.pin_href  = _pins.href;
    dcfg.pin_pclk  = _pins.pclk;
    dcfg.vsync_invert = _cfg.vsync_invert;
    dcfg.href_invert  = _cfg.href_invert;
    dcfg.pclk_invert  = _cfg.pclk_invert;
    dcfg.width   = _width;
    dcfg.height  = _height;
    dcfg.bus_bpp = _bus_bpp;
    dcfg.out_bpp = _out_bpp;
    dcfg.filter  = (_cfg.pixel_format == OV7670_PIXFORMAT_GRAYSCALE)
                       ? DVP_FILTER_Y_ONLY : DVP_FILTER_COPY;
    dcfg.dma_node_count = _cfg.dma_node_count;
    dcfg.fb[0] = _fb[0];
    dcfg.fb[1] = _fb[1];
    dcfg.fb_count = _fb_count;
    dcfg.fb_size  = _frame_bytes;

    if (_cfg.capture_mode == OV7670_CAPTURE_LINE_CALLBACK) {
        if (!_line_cb) {
            OV7670_LOGE("line-callback mode selected but onLine() was never called");
            freeBuffers();
            return ESP_ERR_INVALID_STATE;
        }
        dcfg.line_cb = _line_cb;
        dcfg.line_cb_ctx = _line_ctx;
    }

    rc = dvp_init(&dcfg);
    if (rc != ESP_OK) {
        freeBuffers();
        return rc;
    }
    return ESP_OK;
}

void OV7670Camera::end()
{
    if (!_ready) return;
    stopStreaming();
    dvp_deinit();
    freeBuffers();
    sccb_deinit();
    xclk_stop();
    if (_pins.pwdn >= 0) gpio_set_level((gpio_num_t)_pins.pwdn, 1);
    _ready = false;
}

/* =========================================================================
 * Register initialisation
 * ====================================================================== */

bool OV7670Camera::softReset()
{
    /* COM7_RESET is the first entry of the default table, so a single table
     * write both resets and re-initialises. */
    if (sccb_write_table(ov7670_default_regs) != ESP_OK) {
        OV7670_LOGE("default register table failed - the sensor NACKed");
        return false;
    }
    _mvfp_shadow = 0x07;    /* value the default table leaves in MVFP */
    _saturation = 0;

    if (!applyFormat()) return false;
    if (!applyWindow()) return false;

    /* CLKRC last: changing the internal clock while other timing registers
     * are in flux upsets some modules. */
    uint8_t clkrc = CLKRC_SCALE(_cfg.clkrc_prescaler);
    if (sccb_write(REG_CLKRC, clkrc) != ESP_OK) return false;

    /* Give the sensor a few frames to settle its AGC/AEC loops. */
    vTaskDelay(pdMS_TO_TICKS(100));
    return true;
}

bool OV7670Camera::applyFormat()
{
    const ov7670_reg_entry_t *tbl = ov7670_format_regs[_cfg.pixel_format];
    if (!tbl) return false;
    if (sccb_write_table(tbl) != ESP_OK) return false;

    /* The format table wrote COM7's colour-space bits with the resolution
     * bits cleared. applyWindow() ORs the resolution back in. */
    return true;
}

bool OV7670Camera::applyWindow()
{
    const ov7670_win_t *win = &ov7670_windows[_cfg.frame_size];

    uint8_t com7 = 0;
    if (sccb_read(REG_COM7, &com7) != ESP_OK) return false;
    com7 = (uint8_t)((com7 & ~COM7_FMT_MASK) | win->com7_fmt);
    if (sccb_write(REG_COM7, com7) != ESP_OK) return false;

    /* HSTART/HSTOP carry 11 bits: the top 8 live in their own register and
     * the low 3 of each are packed into HREF. VSTART/VSTOP carry 10 bits with
     * the low 2 of each packed into VREF. Getting this packing wrong is what
     * produces the classic "image is shifted and wraps around" symptom. */
    if (sccb_write(REG_HSTART, (uint8_t)(win->hstart >> 3)) != ESP_OK) return false;
    if (sccb_write(REG_HSTOP,  (uint8_t)(win->hstop  >> 3)) != ESP_OK) return false;

    uint8_t href = 0;
    if (sccb_read(REG_HREF, &href) != ESP_OK) return false;
    href = (uint8_t)((href & 0xC0) |
                     ((win->hstop & 0x07) << 3) |
                     (win->hstart & 0x07));
    if (sccb_write(REG_HREF, href) != ESP_OK) return false;

    if (sccb_write(REG_VSTART, (uint8_t)(win->vstart >> 2)) != ESP_OK) return false;
    if (sccb_write(REG_VSTOP,  (uint8_t)(win->vstop  >> 2)) != ESP_OK) return false;

    uint8_t vref = 0;
    if (sccb_read(REG_VREF, &vref) != ESP_OK) return false;
    vref = (uint8_t)((vref & 0xF0) |
                     ((win->vstop & 0x03) << 2) |
                     (win->vstart & 0x03));
    if (sccb_write(REG_VREF, vref) != ESP_OK) return false;

    if (win->extra) {
        if (sccb_write_table(win->extra) != ESP_OK) return false;
    }

    /* COM17's AEC window must track the COM7 resolution or exposure metering
     * reads the wrong area and the image hunts for brightness. The encoding
     * in COM17[7:6] is not a shift of COM7[5:3], so map it explicitly. */
    uint8_t aecwin;
    switch (win->com7_fmt) {
    case COM7_FMT_QVGA: aecwin = 0x40; break;
    case COM7_FMT_CIF:  aecwin = 0x80; break;
    case COM7_FMT_QCIF: aecwin = 0xC0; break;
    case COM7_FMT_VGA:
    default:            aecwin = 0x00; break;
    }
    sccb_update_bits(REG_COM17, COM17_AECWIN_MASK, aecwin);
    return true;
}

/* =========================================================================
 * Capture
 * ====================================================================== */

bool OV7670Camera::captureFrame(uint32_t timeout_ms)
{
    if (!_ready) return false;
    if (_streaming) {
        OV7670_LOGW("captureFrame() called while streaming - use waitFrame()");
        return false;
    }
    if (dvp_capture_frame(timeout_ms) != ESP_OK) return false;
    _fb_current = 0;
    return true;
}

uint8_t *OV7670Camera::getFrameBuffer(size_t *len)
{
    if (!_ready || _fb_count == 0) {
        if (len) *len = 0;
        return nullptr;
    }
    if (len) *len = _frame_bytes;
    _fb_held = true;
    return _fb[_fb_current];
}

void OV7670Camera::releaseFrameBuffer()
{
    _fb_held = false;
}

bool OV7670Camera::startStreaming()
{
    if (!_ready) return false;
    if (_streaming) return true;
    if (dvp_start_continuous() != ESP_OK) return false;
    _streaming = true;
    return true;
}

void OV7670Camera::stopStreaming()
{
    if (!_streaming) return;
    dvp_stop();
    _streaming = false;
}

bool OV7670Camera::waitFrame(uint32_t timeout_ms)
{
    if (!_ready) return false;
    uint8_t idx = 0;
    if (dvp_wait_frame(timeout_ms, &idx) != ESP_OK) return false;
    _fb_current = (idx < _fb_count) ? idx : 0;
    return true;
}

/* =========================================================================
 * Format changes
 * ====================================================================== */

bool OV7670Camera::setResolution(ov7670_framesize_t fs)
{
    if (fs >= OV7670_FRAMESIZE_COUNT) return false;
    if (!_ready) { _cfg.frame_size = fs; return true; }
    if (fs == _cfg.frame_size) return true;

    bool was_streaming = _streaming;
    stopStreaming();
    dvp_deinit();
    freeBuffers();

    ov7670_framesize_t previous = _cfg.frame_size;
    _cfg.frame_size = fs;

    if (!applyWindow()) {
        _cfg.frame_size = previous;
        applyWindow();
        startCapturePath();
        return false;
    }
    if (startCapturePath() != ESP_OK) {
        /* Roll back so the object is still usable at its old size. */
        _cfg.frame_size = previous;
        applyWindow();
        startCapturePath();
        return false;
    }
    if (was_streaming) startStreaming();
    return true;
}

bool OV7670Camera::setPixelFormat(ov7670_pixformat_t fmt)
{
    if (fmt >= OV7670_PIXFORMAT_COUNT) return false;
    if (!_ready) { _cfg.pixel_format = fmt; return true; }
    if (fmt == _cfg.pixel_format) return true;

    bool was_streaming = _streaming;
    stopStreaming();
    dvp_deinit();
    freeBuffers();

    ov7670_pixformat_t previous = _cfg.pixel_format;
    _cfg.pixel_format = fmt;

    /* The format table rewrites COM7 wholesale, so the window has to be
     * re-applied afterwards to put the resolution bits back. */
    if (!applyFormat() || !applyWindow() || startCapturePath() != ESP_OK) {
        _cfg.pixel_format = previous;
        applyFormat();
        applyWindow();
        startCapturePath();
        return false;
    }
    if (was_streaming) startStreaming();
    return true;
}

/* =========================================================================
 * Image controls
 * ====================================================================== */

static inline int clamp_level(int level)
{
    if (level < -4) return -4;
    if (level >  4) return  4;
    return level;
}

bool OV7670Camera::setMirror(bool enable)
{
    _mvfp_shadow = enable ? (uint8_t)(_mvfp_shadow | MVFP_MIRROR)
                          : (uint8_t)(_mvfp_shadow & ~MVFP_MIRROR);
    return sccb_write(REG_MVFP, _mvfp_shadow) == ESP_OK;
}

bool OV7670Camera::setVFlip(bool enable)
{
    _mvfp_shadow = enable ? (uint8_t)(_mvfp_shadow | MVFP_FLIP)
                          : (uint8_t)(_mvfp_shadow & ~MVFP_FLIP);
    return sccb_write(REG_MVFP, _mvfp_shadow) == ESP_OK;
}

bool OV7670Camera::setBrightness(int level)
{
    return sccb_write(REG_BRIGHT,
                      ov7670_brightness_tbl[clamp_level(level) + 4]) == ESP_OK;
}

bool OV7670Camera::setContrast(int level)
{
    return sccb_write(REG_CONTRAS,
                      ov7670_contrast_tbl[clamp_level(level) + 4]) == ESP_OK;
}

bool OV7670Camera::applyMatrixScaled(int level)
{
    /* Saturation is adjusted by scaling the six colour-matrix coefficients
     * around their format-specific defaults. Level -4 collapses them towards
     * zero (grey), +4 pushes them to roughly 1.75x. */
    bool rgb = (_cfg.pixel_format == OV7670_PIXFORMAT_RGB565 ||
                _cfg.pixel_format == OV7670_PIXFORMAT_RGB555 ||
                _cfg.pixel_format == OV7670_PIXFORMAT_RGB444);
    const uint8_t *base = rgb ? ov7670_matrix_rgb : ov7670_matrix_yuv;

    int scale_num = 4 + clamp_level(level);   /* 0..8 */
    for (int i = 0; i < 6; i++) {
        int v = ((int)base[i] * scale_num) / 4;
        if (v > 0xFF) v = 0xFF;
        if (sccb_write((uint8_t)(REG_MTX1 + i), (uint8_t)v) != ESP_OK) return false;
    }
    if (sccb_write(REG_MTXS, base[6]) != ESP_OK) return false;

    /* With saturation forced, let the sensor stop second-guessing it. */
    return sccb_update_bits(REG_COM13, COM13_UVSAT,
                            (level == 0) ? COM13_UVSAT : 0) == ESP_OK;
}

bool OV7670Camera::setSaturation(int level)
{
    _saturation = clamp_level(level);
    return applyMatrixScaled(_saturation);
}

bool OV7670Camera::setSpecialEffect(ov7670_effect_t effect)
{
    if (effect >= OV7670_EFFECT_COUNT) return false;

    uint8_t tslb = TSLB_YLAST;
    if (effect != OV7670_EFFECT_NONE) tslb |= TSLB_FIXED_UV;
    if (sccb_write(REG_TSLB, tslb) != ESP_OK) return false;
    if (sccb_write(REG_MANU, ov7670_effect_uv[effect][0]) != ESP_OK) return false;
    if (sccb_write(REG_MANV, ov7670_effect_uv[effect][1]) != ESP_OK) return false;
    return true;
}

bool OV7670Camera::setLightMode(ov7670_wb_mode_t mode)
{
    if (mode >= OV7670_WB_COUNT) return false;

    if (mode == OV7670_WB_AUTO) {
        return sccb_update_bits(REG_COM8, COM8_AWB, COM8_AWB) == ESP_OK;
    }
    if (sccb_update_bits(REG_COM8, COM8_AWB, 0) != ESP_OK) return false;
    if (sccb_write(REG_BLUE,  ov7670_wb_gains[mode][0]) != ESP_OK) return false;
    if (sccb_write(REG_RED,   ov7670_wb_gains[mode][1]) != ESP_OK) return false;
    if (sccb_write(REG_GGAIN, ov7670_wb_gains[mode][2]) != ESP_OK) return false;
    return true;
}

bool OV7670Camera::setNightMode(ov7670_night_mode_t mode)
{
    uint8_t bits = 0;
    switch (mode) {
    case OV7670_NIGHT_OFF:  bits = 0; break;
    case OV7670_NIGHT_DIV2: bits = COM11_NIGHT | COM11_NMFR_DIV2; break;
    case OV7670_NIGHT_DIV4: bits = COM11_NIGHT | COM11_NMFR_DIV4; break;
    case OV7670_NIGHT_DIV8: bits = COM11_NIGHT | COM11_NMFR_DIV8; break;
    default: return false;
    }
    return sccb_update_bits(REG_COM11, COM11_NIGHT | COM11_NMFR_MASK,
                            bits) == ESP_OK;
}

bool OV7670Camera::setBandingFilter(ov7670_banding_t banding)
{
    switch (banding) {
    case OV7670_BANDING_AUTO:
        if (sccb_update_bits(REG_COM8, COM8_BFILT, COM8_BFILT) != ESP_OK) return false;
        return sccb_update_bits(REG_COM11, COM11_HZAUTO | COM11_50HZ,
                                COM11_HZAUTO) == ESP_OK;
    case OV7670_BANDING_50HZ:
        if (sccb_update_bits(REG_COM8, COM8_BFILT, COM8_BFILT) != ESP_OK) return false;
        return sccb_update_bits(REG_COM11, COM11_HZAUTO | COM11_50HZ,
                                COM11_50HZ) == ESP_OK;
    case OV7670_BANDING_60HZ:
        if (sccb_update_bits(REG_COM8, COM8_BFILT, COM8_BFILT) != ESP_OK) return false;
        return sccb_update_bits(REG_COM11, COM11_HZAUTO | COM11_50HZ, 0) == ESP_OK;
    case OV7670_BANDING_DISABLED:
        return sccb_update_bits(REG_COM8, COM8_BFILT, 0) == ESP_OK;
    }
    return false;
}

bool OV7670Camera::setAutoExposure(bool enable)
{
    return sccb_update_bits(REG_COM8, COM8_AEC, enable ? COM8_AEC : 0) == ESP_OK;
}

bool OV7670Camera::setAutoGain(bool enable)
{
    return sccb_update_bits(REG_COM8, COM8_AGC, enable ? COM8_AGC : 0) == ESP_OK;
}

bool OV7670Camera::setAutoWhiteBalance(bool enable)
{
    return sccb_update_bits(REG_COM8, COM8_AWB, enable ? COM8_AWB : 0) == ESP_OK;
}

bool OV7670Camera::setGain(uint8_t gain)
{
    /* GAIN holds the low 8 bits; VREF[7:6] hold the top 2. This setter covers
     * the 8-bit range and leaves the high bits clear. */
    if (sccb_update_bits(REG_VREF, 0xC0, 0) != ESP_OK) return false;
    return sccb_write(REG_GAIN, gain) == ESP_OK;
}

bool OV7670Camera::setExposure(uint16_t exposure)
{
    /* Exposure is split across three registers: AECHH[5:0] = bits 15:10,
     * AECH = bits 9:2, COM1[1:0] = bits 1:0. */
    if (sccb_update_bits(REG_COM1, 0x03, (uint8_t)(exposure & 0x03)) != ESP_OK) {
        return false;
    }
    if (sccb_write(REG_AECH, (uint8_t)((exposure >> 2) & 0xFF)) != ESP_OK) {
        return false;
    }
    return sccb_update_bits(REG_AECHH, 0x3F,
                            (uint8_t)((exposure >> 10) & 0x3F)) == ESP_OK;
}

bool OV7670Camera::setColorBar(bool enable)
{
    /* Two independent colour-bar generators exist. The COM7 one replaces the
     * sensor output; the DSP one in COM17 overlays it. Drive both together so
     * the pattern shows regardless of the current format. */
    if (sccb_update_bits(REG_COM7, COM7_COLOR_BAR,
                         enable ? COM7_COLOR_BAR : 0) != ESP_OK) {
        return false;
    }
    return sccb_update_bits(REG_COM17, COM17_DSP_COLOR_BAR,
                            enable ? COM17_DSP_COLOR_BAR : 0) == ESP_OK;
}

/* =========================================================================
 * Low level
 * ====================================================================== */

bool OV7670Camera::writeReg(uint8_t reg, uint8_t value)
{
    return sccb_write(reg, value) == ESP_OK;
}

bool OV7670Camera::readReg(uint8_t reg, uint8_t *value)
{
    return sccb_read(reg, value) == ESP_OK;
}

bool OV7670Camera::updateReg(uint8_t reg, uint8_t mask, uint8_t value)
{
    return sccb_update_bits(reg, mask, value) == ESP_OK;
}

bool OV7670Camera::writeRegTable(const ov7670_reg_entry_t *table)
{
    return sccb_write_table(table) == ESP_OK;
}

bool OV7670Camera::probe(uint8_t *pid, uint8_t *ver)
{
    return sccb_probe(pid, ver) == ESP_OK;
}

uint32_t OV7670Camera::xclkFrequency() const
{
    return xclk_actual_freq();
}

void OV7670Camera::getStats(OV7670Stats *stats) const
{
    if (!stats) return;
    dvp_get_stats(stats);
    stats->sccb_errors = sccb_error_count();
}

void OV7670Camera::resetStats()
{
    dvp_reset_stats();
}

const char *OV7670Camera::backendName() const
{
    return dvp_backend_name();
}

void OV7670Camera::dumpRegisters()
{
    OV7670_LOGI("register dump:");
    for (int base = 0; base <= 0xC0; base += 16) {
        char line[80];
        int n = snprintf(line, sizeof(line), "  %02X:", base);
        for (int i = 0; i < 16; i++) {
            uint8_t v = 0;
            if (sccb_read((uint8_t)(base + i), &v) == ESP_OK) {
                n += snprintf(line + n, sizeof(line) - n, " %02X", v);
            } else {
                n += snprintf(line + n, sizeof(line) - n, " --");
            }
            if (n >= (int)sizeof(line) - 4) break;
        }
        OV7670_LOGI("%s", line);
    }
}
