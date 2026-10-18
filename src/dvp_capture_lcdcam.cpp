/**
 * @file dvp_capture_lcdcam.cpp
 * @brief DVP capture back end for the ESP32-S3, using LCD_CAM + GDMA.
 *
 * ---------------------------------------------------------------------------
 * STATUS: EXPERIMENTAL, NOT HARDWARE-VERIFIED
 * ---------------------------------------------------------------------------
 * The ESP32-S3 has no I2S camera mode; parallel capture lives in the separate
 * LCD_CAM peripheral instead. This file implements the same interface as
 * dvp_capture_i2s.cpp against LCD_CAM and the general-purpose DMA controller.
 *
 * Unlike the classic-ESP32 back end, this one has **not** been validated
 * against real silicon by the author, and LCD_CAM register field names have
 * moved between ESP-IDF releases. It is therefore opt-in: build with
 *
 *     -DOV7670_ENABLE_LCDCAM_EXPERIMENTAL
 *
 * to compile it. Without that flag an ESP32-S3 build stops with a clear
 * message rather than silently producing a driver nobody has run.
 *
 * If you need a known-good S3 camera driver today, use Espressif's
 * esp32-camera component. If you get this one working, please open a PR with
 * the details - that is exactly the report needed to promote it to supported.
 *
 * ---------------------------------------------------------------------------
 * Differences from the I2S back end
 * ---------------------------------------------------------------------------
 * LCD_CAM writes packed bytes. There is no padding byte between samples, so a
 * line's DMA buffer is exactly as long as the line, and the "unpack" step for
 * 2-byte formats degenerates into a memcpy. Grayscale extraction still has to
 * walk the buffer, taking every second byte.
 *
 * Signal routing follows the same reasoning as the I2S back end: HREF gates
 * the data in hardware, and VSYNC drives a GPIO interrupt that brackets the
 * frame in software, so no assumption is baked in about VSYNC polarity.
 *
 * Part of the OV7670-ESP32-NonFIFO library. MIT licensed.
 */

#include "sdkconfig.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3) && defined(OV7670_ENABLE_LCDCAM_EXPERIMENTAL)

#include "dvp_capture.h"
#include "ov7670_log.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_rom_gpio.h"

#include "soc/lcd_cam_struct.h"
#include "soc/gpio_sig_map.h"
#include "hal/dma_types.h"
#include "esp_private/gdma.h"
#include "esp_private/periph_ctrl.h"
#include "esp_idf_version.h"

#ifndef GPIO_MATRIX_CONST_ONE_INPUT
#define GPIO_MATRIX_CONST_ONE_INPUT 0x38
#endif

#define DMA_DESC_MAX_BYTES 4092

#define MARK_SOF (-1)
#define MARK_EOF (-2)

typedef struct {
    dvp_config_t cfg;

    dma_descriptor_t *desc;
    uint8_t *dma_pool;
    uint8_t *line_scratch;

    size_t   line_bus_bytes;
    size_t   line_out_bytes;
    size_t   node_dma_bytes;    /* == line_bus_bytes; LCD_CAM packs tightly */
    uint8_t  node_count;

    gdma_channel_handle_t dma_chan;
    QueueHandle_t     evt_q;
    SemaphoreHandle_t frame_sem;
    TaskHandle_t      task;

    volatile bool running;
    volatile bool capturing;
    volatile bool continuous;
    volatile bool want_frame;
    volatile bool task_quit;

    uint8_t  *dst;
    uint8_t   fb_write;
    uint8_t   fb_ready;
    uint16_t  line_idx;
    uint16_t  last_line_count;
    int64_t   frame_start_us;

    OV7670Stats stats;
    float     fps_accum;
} dvp_state_t;

static dvp_state_t *s_dvp = NULL;

/* =========================================================================
 * LCD_CAM control
 * ====================================================================== */

static IRAM_ATTR void cam_engine_start(dvp_state_t *s)
{
    LCD_CAM.cam_ctrl1.cam_start = 0;
    LCD_CAM.cam_ctrl1.cam_reset = 1;
    LCD_CAM.cam_ctrl1.cam_reset = 0;
    LCD_CAM.cam_ctrl1.cam_afifo_reset = 1;
    LCD_CAM.cam_ctrl1.cam_afifo_reset = 0;

    gdma_start(s->dma_chan, (intptr_t)&s->desc[0]);

    LCD_CAM.cam_ctrl.cam_update = 1;
    LCD_CAM.cam_ctrl1.cam_start = 1;
}

static IRAM_ATTR void cam_engine_stop(dvp_state_t *s)
{
    LCD_CAM.cam_ctrl1.cam_start = 0;
    gdma_stop(s->dma_chan);
}

static esp_err_t cam_peripheral_config(dvp_state_t *s)
{
    periph_module_enable(PERIPH_LCD_CAM_MODULE);

    LCD_CAM.cam_ctrl.val = 0;
    /* Module clock: PLL160M / 2. The pixel timing comes from CAM_PCLK, so this
     * only has to be a legal, running clock. */
    LCD_CAM.cam_ctrl.cam_clk_sel = 3;
    LCD_CAM.cam_ctrl.cam_clkm_div_num = 2;
    LCD_CAM.cam_ctrl.cam_clkm_div_a = 0;
    LCD_CAM.cam_ctrl.cam_clkm_div_b = 0;

    LCD_CAM.cam_ctrl.cam_stop_en = 0;
    LCD_CAM.cam_ctrl.cam_vsync_filter_thres = 4;
    LCD_CAM.cam_ctrl.cam_byte_order = 0;
    LCD_CAM.cam_ctrl.cam_bit_order = 0;
    LCD_CAM.cam_ctrl.cam_line_int_en = 0;
    LCD_CAM.cam_ctrl.cam_vs_eof_en = 0;   /* EOF from the byte counter below */

    LCD_CAM.cam_ctrl1.val = 0;
    LCD_CAM.cam_ctrl1.cam_rec_data_bytelen = s->node_dma_bytes - 1;
    LCD_CAM.cam_ctrl1.cam_line_int_num = 0;
    LCD_CAM.cam_ctrl1.cam_clk_inv = s->cfg.pclk_invert ? 1 : 0;
    LCD_CAM.cam_ctrl1.cam_vsync_filter_en = 1;
    LCD_CAM.cam_ctrl1.cam_2byte_en = 0;   /* 8-bit bus */
    LCD_CAM.cam_ctrl1.cam_de_inv = 0;     /* HREF inversion is done in the matrix */
    LCD_CAM.cam_ctrl1.cam_hsync_inv = 0;
    LCD_CAM.cam_ctrl1.cam_vsync_inv = 0;
    LCD_CAM.cam_ctrl1.cam_vh_de_mode_en = 0;  /* gate on V_SYNC + DE (HREF) */

    LCD_CAM.cam_rgb_yuv.val = 0;          /* no colour conversion */

    LCD_CAM.cam_ctrl.cam_update = 1;
    return ESP_OK;
}

/* =========================================================================
 * Pin routing
 * ====================================================================== */

static esp_err_t route_input(int8_t pin, uint32_t signal, bool invert)
{
    if (pin < 0) return ESP_ERR_INVALID_ARG;
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << pin;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    esp_err_t rc = gpio_config(&io);
    if (rc != ESP_OK) return rc;
    esp_rom_gpio_connect_in_signal((uint32_t)pin, signal, invert);
    return ESP_OK;
}

/* Defined below; the IRAM_ATTR section attribute belongs on the definition
 * only - repeating it here makes GCC generate two conflicting sections. */
static void vsync_isr(void *arg);

static esp_err_t configure_pins(dvp_state_t *s)
{
    const dvp_config_t *c = &s->cfg;

    for (int i = 0; i < 8; i++) {
        if (c->pin_d[i] < 0) return ESP_ERR_INVALID_ARG;
        OV7670_RETURN_ON_ERR(
            route_input(c->pin_d[i], (uint32_t)(CAM_DATA_IN0_IDX + i), false),
            "route data pin");
    }
    OV7670_RETURN_ON_ERR(route_input(c->pin_pclk, CAM_PCLK_IDX, false), "route PCLK");
    OV7670_RETURN_ON_ERR(route_input(c->pin_href, CAM_H_ENABLE_IDX,
                                     c->href_invert), "route HREF");

    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ONE_INPUT, CAM_H_SYNC_IDX, false);
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ONE_INPUT, CAM_V_SYNC_IDX, false);

    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << c->pin_vsync;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = c->vsync_invert ? GPIO_INTR_NEGEDGE : GPIO_INTR_POSEDGE;
    OV7670_RETURN_ON_ERR(gpio_config(&io), "config VSYNC");

    esp_err_t rc = gpio_install_isr_service(ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM);
    if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) return rc;
    OV7670_RETURN_ON_ERR(gpio_isr_handler_add((gpio_num_t)c->pin_vsync,
                                              vsync_isr, s), "add VSYNC handler");
    gpio_intr_disable((gpio_num_t)c->pin_vsync);
    return ESP_OK;
}

/* =========================================================================
 * Interrupt handlers
 * ====================================================================== */

static IRAM_ATTR bool gdma_rx_eof_cb(gdma_channel_handle_t chan,
                                     gdma_event_data_t *event, void *user_data)
{
    dvp_state_t *s = (dvp_state_t *)user_data;
    BaseType_t hp = pdFALSE;

    uintptr_t base = (uintptr_t)s->desc;
    uintptr_t end  = base + (uintptr_t)s->node_count * sizeof(dma_descriptor_t);
    uintptr_t eof  = (uintptr_t)event->rx_eof_desc_addr;

    if (eof >= base && eof < end) {
        int32_t idx = (int32_t)((eof - base) / sizeof(dma_descriptor_t));
        if (xQueueSendFromISR(s->evt_q, &idx, &hp) != pdTRUE) {
            s->stats.dma_overruns++;
        }
    }
    return hp == pdTRUE;
}

static void IRAM_ATTR vsync_isr(void *arg)
{
    dvp_state_t *s = (dvp_state_t *)arg;
    BaseType_t hp = pdFALSE;
    int32_t mark;

    if (s->capturing) {
        cam_engine_stop(s);
        s->capturing = false;
        mark = MARK_EOF;
        xQueueSendFromISR(s->evt_q, &mark, &hp);
    }

    if (s->continuous || s->want_frame) {
        s->want_frame = false;
        mark = MARK_SOF;
        if (xQueueSendFromISR(s->evt_q, &mark, &hp) == pdTRUE) {
            cam_engine_start(s);
            s->capturing = true;
        }
    }

    if (hp == pdTRUE) portYIELD_FROM_ISR();
}

/* =========================================================================
 * Unpacking - LCD_CAM delivers packed bytes, so COPY is a straight memcpy
 * ====================================================================== */

static void unpack_copy(const uint8_t *src, uint8_t *dst, size_t out_len)
{
    memcpy(dst, src, out_len);
}

static void unpack_y_only(const uint8_t *src, uint8_t *dst, size_t out_len)
{
    for (size_t i = 0; i < out_len; i++) {
        dst[i] = src[i * 2];
    }
}

/* =========================================================================
 * Worker task - identical bookkeeping to the I2S back end
 * ====================================================================== */

static void dvp_frame_start(dvp_state_t *s)
{
    s->line_idx = 0;
    s->frame_start_us = esp_timer_get_time();
    s->dst = s->cfg.line_cb ? NULL : s->cfg.fb[s->fb_write];
}

static void dvp_frame_end(dvp_state_t *s)
{
    int64_t now = esp_timer_get_time();
    s->last_line_count = s->line_idx;
    s->stats.last_frame_us = (uint32_t)(now - s->frame_start_us);

    if (s->line_idx != s->cfg.height) s->stats.frames_torn++;
    s->stats.frames_captured++;

    if (s->stats.last_frame_us > 0) {
        float inst = 1000000.0f / (float)s->stats.last_frame_us;
        s->fps_accum = (s->fps_accum == 0.0f) ? inst
                                              : (s->fps_accum * 0.9f + inst * 0.1f);
        s->stats.fps = s->fps_accum;
    }

    if (!s->cfg.line_cb) {
        s->fb_ready = s->fb_write;
        if (s->cfg.fb_count > 1) s->fb_write ^= 1;
    }
    xSemaphoreGive(s->frame_sem);
}

static void dvp_task(void *arg)
{
    dvp_state_t *s = (dvp_state_t *)arg;
    int32_t evt;

    while (!s->task_quit) {
        if (xQueueReceive(s->evt_q, &evt, pdMS_TO_TICKS(100)) != pdTRUE) continue;

        if (evt == MARK_SOF) { dvp_frame_start(s); continue; }
        if (evt == MARK_EOF) { dvp_frame_end(s);   continue; }

        const uint8_t *src = s->dma_pool + (size_t)evt * s->node_dma_bytes;

        if (s->line_idx >= s->cfg.height) { s->line_idx++; continue; }

        uint8_t *dst;
        if (s->cfg.line_cb) {
            dst = s->line_scratch;
        } else {
            if (!s->dst) { s->line_idx++; continue; }
            size_t offset = (size_t)s->line_idx * s->line_out_bytes;
            if (offset + s->line_out_bytes > s->cfg.fb_size) { s->line_idx++; continue; }
            dst = s->dst + offset;
        }

        if (s->cfg.filter == DVP_FILTER_Y_ONLY) {
            unpack_y_only(src, dst, s->line_out_bytes);
        } else {
            unpack_copy(src, dst, s->line_out_bytes);
        }

        if (s->cfg.line_cb) {
            s->cfg.line_cb(dst, s->line_out_bytes, s->line_idx, s->cfg.line_cb_ctx);
        }
        s->line_idx++;
    }

    s->task = NULL;
    vTaskDelete(NULL);
}

/* =========================================================================
 * Public API
 * ====================================================================== */

const char *dvp_backend_name(void)
{
    return "LCD_CAM + GDMA (ESP32-S3, experimental)";
}

esp_err_t dvp_init(const dvp_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (s_dvp) dvp_deinit();

    if (cfg->pin_vsync < 0 || cfg->pin_href < 0 || cfg->pin_pclk < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->dma_node_count < 2) return ESP_ERR_INVALID_ARG;
    if (!cfg->line_cb && (cfg->fb_count == 0 || !cfg->fb[0])) {
        return ESP_ERR_INVALID_ARG;
    }

    dvp_state_t *s = (dvp_state_t *)heap_caps_calloc(1, sizeof(dvp_state_t),
                                                     MALLOC_CAP_INTERNAL |
                                                     MALLOC_CAP_8BIT);
    if (!s) return ESP_ERR_NO_MEM;
    s->cfg = *cfg;
    s->line_bus_bytes = (size_t)cfg->width * cfg->bus_bpp;
    s->line_out_bytes = (size_t)cfg->width * cfg->out_bpp;
    s->node_dma_bytes = s->line_bus_bytes;
    s->node_count = cfg->dma_node_count;

    if (s->node_dma_bytes > DMA_DESC_MAX_BYTES) {
        OV7670_LOGE("line too wide for one DMA descriptor");
        heap_caps_free(s);
        return ESP_ERR_INVALID_ARG;
    }

    s->desc = (dma_descriptor_t *)heap_caps_calloc(s->node_count,
                                                   sizeof(dma_descriptor_t),
                                                   MALLOC_CAP_DMA);
    s->dma_pool = (uint8_t *)heap_caps_calloc(s->node_count, s->node_dma_bytes,
                                              MALLOC_CAP_DMA);
    if (!s->desc || !s->dma_pool) goto fail;

    if (cfg->line_cb) {
        s->line_scratch = (uint8_t *)heap_caps_calloc(1, s->line_out_bytes,
                                                      MALLOC_CAP_INTERNAL |
                                                      MALLOC_CAP_8BIT);
        if (!s->line_scratch) goto fail;
    }

    for (uint8_t i = 0; i < s->node_count; i++) {
        s->desc[i].dw0.size    = s->node_dma_bytes;
        s->desc[i].dw0.length  = 0;
        s->desc[i].dw0.owner   = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
        s->desc[i].dw0.suc_eof = 0;
        s->desc[i].buffer      = s->dma_pool + (size_t)i * s->node_dma_bytes;
        s->desc[i].next        = &s->desc[(i + 1) % s->node_count];
    }

    s->evt_q = xQueueCreate(s->node_count * 2 + 8, sizeof(int32_t));
    s->frame_sem = xSemaphoreCreateBinary();
    if (!s->evt_q || !s->frame_sem) goto fail;

    s_dvp = s;

    if (cam_peripheral_config(s) != ESP_OK) goto fail;
    if (configure_pins(s) != ESP_OK) goto fail;

    {
        gdma_channel_alloc_config_t chan_cfg = {};
        chan_cfg.direction = GDMA_CHANNEL_DIRECTION_RX;
        /* gdma_new_channel() was split into bus-specific variants in IDF 5.2.
         * The S3's LCD_CAM sits on the AHB controller. */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
        if (gdma_new_ahb_channel(&chan_cfg, &s->dma_chan) != ESP_OK) goto fail;
#else
        if (gdma_new_channel(&chan_cfg, &s->dma_chan) != ESP_OK) goto fail;
#endif
        if (gdma_connect(s->dma_chan,
                         GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_CAM, 0)) != ESP_OK) {
            goto fail;
        }
        gdma_strategy_config_t strategy = {};
        strategy.owner_check = false;
        strategy.auto_update_desc = true;
        gdma_apply_strategy(s->dma_chan, &strategy);

        gdma_rx_event_callbacks_t cbs = {};
        cbs.on_recv_eof = gdma_rx_eof_cb;
        if (gdma_register_rx_event_callbacks(s->dma_chan, &cbs, s) != ESP_OK) {
            goto fail;
        }
    }

    if (xTaskCreatePinnedToCore(dvp_task, "ov7670_dvp", 4096, s,
                                configMAX_PRIORITIES - 2, &s->task,
                                tskNO_AFFINITY) != pdPASS) {
        goto fail;
    }

    s->running = true;
    OV7670_LOGW("using the EXPERIMENTAL LCD_CAM back end - please report results");
    OV7670_LOGI("DVP ready: %ux%u, %u DMA nodes of %u bytes",
                cfg->width, cfg->height, s->node_count,
                (unsigned)s->node_dma_bytes);
    return ESP_OK;

fail:
    s_dvp = NULL;
    if (s->dma_chan) { gdma_disconnect(s->dma_chan); gdma_del_channel(s->dma_chan); }
    if (s->evt_q) vQueueDelete(s->evt_q);
    if (s->frame_sem) vSemaphoreDelete(s->frame_sem);
    if (s->line_scratch) heap_caps_free(s->line_scratch);
    if (s->dma_pool) heap_caps_free(s->dma_pool);
    if (s->desc) heap_caps_free(s->desc);
    heap_caps_free(s);
    return ESP_ERR_NO_MEM;
}

void dvp_deinit(void)
{
    dvp_state_t *s = s_dvp;
    if (!s) return;

    dvp_stop();

    if (s->cfg.pin_vsync >= 0) {
        gpio_intr_disable((gpio_num_t)s->cfg.pin_vsync);
        gpio_isr_handler_remove((gpio_num_t)s->cfg.pin_vsync);
    }
    if (s->dma_chan) {
        gdma_disconnect(s->dma_chan);
        gdma_del_channel(s->dma_chan);
        s->dma_chan = NULL;
    }
    periph_module_disable(PERIPH_LCD_CAM_MODULE);

    if (s->task) {
        s->task_quit = true;
        for (int i = 0; i < 20 && s->task; i++) vTaskDelay(pdMS_TO_TICKS(10));
    }

    s_dvp = NULL;
    if (s->evt_q) vQueueDelete(s->evt_q);
    if (s->frame_sem) vSemaphoreDelete(s->frame_sem);
    if (s->line_scratch) heap_caps_free(s->line_scratch);
    if (s->dma_pool) heap_caps_free(s->dma_pool);
    if (s->desc) heap_caps_free(s->desc);
    heap_caps_free(s);
}

esp_err_t dvp_capture_frame(uint32_t timeout_ms)
{
    dvp_state_t *s = s_dvp;
    if (!s || !s->running) return ESP_ERR_INVALID_STATE;
    if (s->continuous) return ESP_ERR_INVALID_STATE;

    xQueueReset(s->evt_q);
    xSemaphoreTake(s->frame_sem, 0);

    s->want_frame = true;
    gpio_intr_enable((gpio_num_t)s->cfg.pin_vsync);

    BaseType_t got = xSemaphoreTake(s->frame_sem, pdMS_TO_TICKS(timeout_ms));

    gpio_intr_disable((gpio_num_t)s->cfg.pin_vsync);
    s->want_frame = false;
    if (s->capturing) { cam_engine_stop(s); s->capturing = false; }

    return (got == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t dvp_start_continuous(void)
{
    dvp_state_t *s = s_dvp;
    if (!s || !s->running) return ESP_ERR_INVALID_STATE;
    if (s->continuous) return ESP_OK;
    xQueueReset(s->evt_q);
    xSemaphoreTake(s->frame_sem, 0);
    s->continuous = true;
    gpio_intr_enable((gpio_num_t)s->cfg.pin_vsync);
    return ESP_OK;
}

void dvp_stop(void)
{
    dvp_state_t *s = s_dvp;
    if (!s) return;
    s->continuous = false;
    s->want_frame = false;
    if (s->cfg.pin_vsync >= 0) gpio_intr_disable((gpio_num_t)s->cfg.pin_vsync);
    if (s->capturing) { cam_engine_stop(s); s->capturing = false; }
}

esp_err_t dvp_wait_frame(uint32_t timeout_ms, uint8_t *out_index)
{
    dvp_state_t *s = s_dvp;
    if (!s || !s->running) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s->frame_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (out_index) *out_index = s->fb_ready;
    return ESP_OK;
}

uint16_t dvp_last_line_count(void)
{
    return s_dvp ? s_dvp->last_line_count : 0;
}

void dvp_get_stats(OV7670Stats *stats)
{
    if (!stats) return;
    if (!s_dvp) { memset(stats, 0, sizeof(*stats)); return; }
    *stats = s_dvp->stats;
}

void dvp_reset_stats(void)
{
    if (!s_dvp) return;
    memset(&s_dvp->stats, 0, sizeof(s_dvp->stats));
    s_dvp->fps_accum = 0.0f;
}

#elif defined(CONFIG_IDF_TARGET_ESP32S3)

#error "ESP32-S3 support in OV7670-ESP32-NonFIFO is experimental and disabled by default. \
Build with -DOV7670_ENABLE_LCDCAM_EXPERIMENTAL to enable the LCD_CAM back end, \
or use Espressif's esp32-camera component for a hardware-validated S3 driver."

#elif defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32C3) || \
      defined(CONFIG_IDF_TARGET_ESP32C6)

#error "OV7670-ESP32-NonFIFO requires a parallel camera peripheral. \
The classic ESP32 (I2S0 camera mode) is supported; ESP32-S3 is experimental. \
This target has no compatible DVP capture peripheral in this library."

#endif
