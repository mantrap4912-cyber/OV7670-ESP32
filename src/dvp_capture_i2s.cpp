/**
 * @file dvp_capture_i2s.cpp
 * @brief DVP capture back end for the classic ESP32, using I2S0 camera mode.
 *
 * ---------------------------------------------------------------------------
 * How this works
 * ---------------------------------------------------------------------------
 * The ESP32's I2S0 peripheral has an alternate "camera" personality
 * (I2S_CONF2_REG.camera_en). In that mode it stops being an audio serial port
 * and becomes an 8-bit parallel sampler:
 *
 *   - PCLK drives the sample clock, routed to the peripheral's WS input.
 *   - D0..D7 are sampled on each PCLK edge.
 *   - The H_ENABLE input gates sampling, so only bytes inside an active HREF
 *     window reach the FIFO.
 *   - The FIFO drains into RAM through the I2S DMA engine.
 *
 * That chain is the replacement for the AL422B FIFO that a non-FIFO OV7670
 * module lacks. No byte is ever touched by software while it is in flight.
 *
 * ---------------------------------------------------------------------------
 * Signal routing, and why VSYNC is not routed to the hardware
 * ---------------------------------------------------------------------------
 * The peripheral has three gating inputs: V_SYNC, H_SYNC and H_ENABLE, and it
 * samples only while all of them are asserted. This driver uses exactly one of
 * them and ties the other two to the GPIO matrix constant-one signal:
 *
 *   PCLK  -> I2S0I_WS_IN      sample clock
 *   HREF  -> I2S0I_H_ENABLE   per-line data gate  (HREF means "pixel valid")
 *   const 1 -> I2S0I_H_SYNC   unused
 *   const 1 -> I2S0I_V_SYNC   unused, see below
 *
 * VSYNC is deliberately handled in software, by a GPIO interrupt that starts
 * and stops the DMA engine at frame boundaries. The reason is polarity: on a
 * default-configured OV7670, VSYNC is *high* during vertical blanking and low
 * while pixels are being emitted. Feeding that straight into an active-high
 * V_SYNC gate would enable capture exactly when there is no image data. Rather
 * than depend on a particular module's polarity - some breakouts buffer or
 * invert these lines - the frame boundary is taken from an interrupt whose
 * edge is selectable via OV7670Config::vsync_invert.
 *
 * If your module inverts HREF, set OV7670Config::href_invert; that flips the
 * inversion bit in the GPIO matrix and costs nothing at runtime.
 *
 * ---------------------------------------------------------------------------
 * Sampling mode and why the DMA buffer is twice the size of a line
 * ---------------------------------------------------------------------------
 * In camera mode the I2S FIFO is 32 bits wide and, in the SM_0A0B_0C0D
 * sampling mode used here, packs two 8-bit samples into each 32-bit word with
 * a padding byte between them. In memory (little-endian) one word looks like:
 *
 *      byte 0: second sample
 *      byte 1: padding
 *      byte 2: first sample
 *      byte 3: padding
 *
 * So four DMA bytes carry two real pixel bytes, and the DMA buffer for one
 * line must be twice the line's byte length. The unpack step in dvp_task()
 * squeezes the padding back out. That step is also where grayscale extraction
 * happens for free: taking only the first sample of each word off a YUV422
 * stream yields the luma plane.
 *
 * ---------------------------------------------------------------------------
 * Threading
 * ---------------------------------------------------------------------------
 * Two interrupts and one task cooperate through a single ordered queue:
 *
 *   VSYNC GPIO ISR  - starts/stops the DMA engine, posts SOF/EOF markers
 *   I2S in_suc_eof  - posts the index of the DMA node that just filled
 *   dvp_task        - unpacks nodes, tracks the line counter, publishes frames
 *
 * Everything that touches the frame buffer happens in the task, in queue
 * order. That keeps the ISRs short enough to be IRAM-safe and, importantly,
 * lets the frame buffer live in PSRAM - an ISR registered with
 * ESP_INTR_FLAG_IRAM may run while the flash/PSRAM cache is disabled and must
 * never dereference cached memory.
 *
 * Part of the OV7670-ESP32-NonFIFO library. MIT licensed.
 */

#include "sdkconfig.h"

#if defined(CONFIG_IDF_TARGET_ESP32)

#include "dvp_capture.h"
#include "ov7670_log.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_intr_alloc.h"
#include "esp_timer.h"
#include "esp_attr.h"

#include "soc/i2s_struct.h"
#include "soc/i2s_reg.h"
#include "soc/gpio_sig_map.h"
#include "soc/periph_defs.h"

/* lldesc_t has moved around between ESP-IDF releases; soc/lldesc.h is the
 * long-standing location and rom/lldesc.h is where it lives underneath. */
#if __has_include("soc/lldesc.h")
#include "soc/lldesc.h"
#elif __has_include("esp32/rom/lldesc.h")
#include "esp32/rom/lldesc.h"
#else
#include "rom/lldesc.h"
#endif

#if __has_include("esp_rom_gpio.h")
#include "esp_rom_gpio.h"
#else
#include "rom/gpio.h"
#define esp_rom_gpio_connect_in_signal gpio_matrix_in
#endif

#if __has_include("esp_private/periph_ctrl.h")
#include "esp_private/periph_ctrl.h"
#else
#include "driver/periph_ctrl.h"
#endif

/** GPIO matrix pseudo-pin that always reads 1. */
#ifndef GPIO_MATRIX_CONST_ONE_INPUT
#define GPIO_MATRIX_CONST_ONE_INPUT 0x38
#endif

/** I2S FIFO sampling mode: two 8-bit samples per 32-bit word. */
#define I2S_SAMPLING_MODE_0A0B_0C0D 1

/** Largest transfer one lldesc can describe (12-bit field, 4-byte aligned). */
#define LLDESC_MAX_BYTES 4092

/* Queue markers, distinguishable from DMA node indices (which are >= 0). */
#define MARK_SOF (-1)
#define MARK_EOF (-2)

typedef struct {
    dvp_config_t cfg;

    lldesc_t *desc;             /* node_count descriptors, DMA-capable RAM */
    uint8_t  *dma_pool;         /* node_count * node_dma_bytes, DMA-capable */
    uint8_t  *line_scratch;     /* one unpacked line, for the callback path */

    size_t   line_bus_bytes;    /* bytes the sensor emits per line */
    size_t   line_out_bytes;    /* bytes stored per line */
    size_t   node_dma_bytes;    /* DMA bytes per node = line_bus_bytes * 2 */
    uint8_t  node_count;

    intr_handle_t     i2s_intr;
    QueueHandle_t     evt_q;
    SemaphoreHandle_t frame_sem;
    TaskHandle_t      task;

    volatile bool running;      /* init() completed */
    volatile bool capturing;    /* DMA engine is armed for the current frame */
    volatile bool continuous;
    volatile bool want_frame;   /* one-shot request pending */
    volatile bool task_quit;

    /* Task-owned state (single writer, no locking needed) */
    uint8_t  *dst;              /* frame buffer being filled */
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
 * Low-level I2S control
 * ====================================================================== */

static IRAM_ATTR void i2s_dma_reset(void)
{
    I2S0.lc_conf.in_rst = 1;        I2S0.lc_conf.in_rst = 0;
    I2S0.lc_conf.ahbm_fifo_rst = 1; I2S0.lc_conf.ahbm_fifo_rst = 0;
    I2S0.lc_conf.ahbm_rst = 1;      I2S0.lc_conf.ahbm_rst = 0;
    I2S0.conf.rx_reset = 1;         I2S0.conf.rx_reset = 0;
    I2S0.conf.rx_fifo_reset = 1;    I2S0.conf.rx_fifo_reset = 0;
}

static IRAM_ATTR void i2s_engine_start(dvp_state_t *s)
{
    I2S0.conf.rx_start = 0;
    I2S0.int_ena.val = 0;
    I2S0.int_clr.val = ~0U;

    i2s_dma_reset();

    /* Raise in_suc_eof once per node. rx_eof_num counts 32-bit words. */
    I2S0.rx_eof_num = s->node_dma_bytes / sizeof(uint32_t);
    I2S0.in_link.addr = ((uint32_t)&s->desc[0]) & 0xFFFFF;
    I2S0.in_link.start = 1;

    I2S0.int_clr.val = ~0U;
    I2S0.int_ena.in_suc_eof = 1;
    I2S0.conf.rx_start = 1;
}

static IRAM_ATTR void i2s_engine_stop(void)
{
    I2S0.conf.rx_start = 0;
    I2S0.int_ena.val = 0;
    I2S0.int_clr.val = ~0U;
    I2S0.in_link.stop = 1;
}

static esp_err_t i2s_peripheral_config(void)
{
    periph_module_enable(PERIPH_I2S0_MODULE);

    i2s_dma_reset();

    /* Clear every field that only means something in audio serial modes. */
    I2S0.conf.val = 0;
    I2S0.conf.rx_slave_mod   = 1;   /* WS (= PCLK) is supplied by the sensor */
    I2S0.conf.rx_right_first = 0;
    I2S0.conf.rx_msb_right   = 0;
    I2S0.conf.rx_msb_shift   = 0;
    I2S0.conf.rx_mono        = 0;
    I2S0.conf.rx_short_sync  = 0;

    I2S0.conf2.val = 0;
    I2S0.conf2.lcd_en    = 1;
    I2S0.conf2.camera_en = 1;

    /* The peripheral is clocked externally by PCLK, so this divider only has
     * to be legal, not correct. */
    I2S0.clkm_conf.val = 0;
    I2S0.clkm_conf.clkm_div_a   = 0;
    I2S0.clkm_conf.clkm_div_b   = 0;
    I2S0.clkm_conf.clkm_div_num = 2;

    I2S0.fifo_conf.val = 0;
    I2S0.fifo_conf.dscr_en = 1;                 /* FIFO feeds the DMA engine */
    I2S0.fifo_conf.rx_fifo_mod = I2S_SAMPLING_MODE_0A0B_0C0D;
    I2S0.fifo_conf.rx_fifo_mod_force_en = 1;

    I2S0.conf_chan.val = 0;
    I2S0.conf_chan.rx_chan_mod = 1;

    I2S0.sample_rate_conf.val = 0;
    I2S0.sample_rate_conf.rx_bits_mod = 0;

    I2S0.timing.val = 0;
    I2S0.timing.rx_dsync_sw = 1;

    return ESP_OK;
}

/* =========================================================================
 * Pin routing
 * ====================================================================== */

static esp_err_t route_input(int8_t pin, uint32_t signal, bool invert, bool pullup)
{
    if (pin < 0) return ESP_ERR_INVALID_ARG;
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << pin;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
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
        if (c->pin_d[i] < 0) {
            OV7670_LOGE("data pin D%d is not assigned", i);
            return ESP_ERR_INVALID_ARG;
        }
        OV7670_RETURN_ON_ERR(
            route_input(c->pin_d[i], (uint32_t)(I2S0I_DATA_IN0_IDX + i), false, false),
            "route data pin");
    }

    OV7670_RETURN_ON_ERR(route_input(c->pin_pclk, I2S0I_WS_IN_IDX,
                                     c->pclk_invert, false), "route PCLK");
    OV7670_RETURN_ON_ERR(route_input(c->pin_href, I2S0I_H_ENABLE_IDX,
                                     c->href_invert, false), "route HREF");

    /* Unused gates held asserted - see the file header. */
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ONE_INPUT, I2S0I_H_SYNC_IDX, false);
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ONE_INPUT, I2S0I_V_SYNC_IDX, false);

    /* VSYNC: plain GPIO interrupt, no matrix routing. On a stock OV7670 the
     * line idles low during pixel output and pulses high through vertical
     * blanking, so a rising edge marks "the frame that just ended". */
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << c->pin_vsync;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = c->vsync_invert ? GPIO_INTR_NEGEDGE : GPIO_INTR_POSEDGE;
    OV7670_RETURN_ON_ERR(gpio_config(&io), "config VSYNC");

    esp_err_t rc = gpio_install_isr_service(ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM);
    if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
        OV7670_LOGE("gpio_install_isr_service failed: %s", esp_err_to_name(rc));
        return rc;
    }
    OV7670_RETURN_ON_ERR(gpio_isr_handler_add((gpio_num_t)c->pin_vsync,
                                              vsync_isr, s), "add VSYNC handler");
    gpio_intr_disable((gpio_num_t)c->pin_vsync);
    return ESP_OK;
}

/* =========================================================================
 * Interrupt handlers
 * ====================================================================== */

static void IRAM_ATTR i2s_isr(void *arg)
{
    dvp_state_t *s = (dvp_state_t *)arg;
    BaseType_t hp = pdFALSE;

    /* Read the status word once and acknowledge exactly the bits we saw. The
     * register struct is volatile, so it cannot be copied wholesale in C++;
     * work with the raw word and the register masks instead. */
    uint32_t status = I2S0.int_st.val;
    I2S0.int_clr.val = status;

    if (status & I2S_IN_SUC_EOF_INT_ST_M) {
        /* Work out which node just completed from the descriptor address the
         * hardware latched, falling back to a running counter if it is out of
         * range (which should not happen, but a wrong index would corrupt the
         * image rather than merely lose a line). */
        uint32_t eof = I2S0.in_eof_des_addr;
        int32_t idx;
        uintptr_t base = (uintptr_t)s->desc;
        uintptr_t end  = base + (uintptr_t)s->node_count * sizeof(lldesc_t);
        if (eof >= base && eof < end) {
            idx = (int32_t)(((uintptr_t)eof - base) / sizeof(lldesc_t));
        } else {
            idx = -1;
        }
        if (idx >= 0) {
            if (xQueueSendFromISR(s->evt_q, &idx, &hp) != pdTRUE) {
                s->stats.dma_overruns++;
            }
        }
    }

    if (hp == pdTRUE) portYIELD_FROM_ISR();
}

static void IRAM_ATTR vsync_isr(void *arg)
{
    dvp_state_t *s = (dvp_state_t *)arg;
    BaseType_t hp = pdFALSE;
    int32_t mark;

    if (s->capturing) {
        i2s_engine_stop();
        s->capturing = false;
        mark = MARK_EOF;
        xQueueSendFromISR(s->evt_q, &mark, &hp);
    }

    if (s->continuous || s->want_frame) {
        s->want_frame = false;
        mark = MARK_SOF;
        if (xQueueSendFromISR(s->evt_q, &mark, &hp) == pdTRUE) {
            i2s_engine_start(s);
            s->capturing = true;
        }
    }

    if (hp == pdTRUE) portYIELD_FROM_ISR();
}

/* =========================================================================
 * Unpacking
 *
 * Both loops are written to read the DMA buffer strictly forwards, which is
 * what the memory system likes, and are unrolled four ways because at VGA
 * this runs ~7000 times a second over 2.5 kB each time.
 * ====================================================================== */

static void unpack_copy(const uint8_t *src, uint8_t *dst, size_t out_len)
{
    size_t pairs = out_len >> 1;
    while (pairs >= 4) {
        dst[0] = src[2];  dst[1] = src[0];
        dst[2] = src[6];  dst[3] = src[4];
        dst[4] = src[10]; dst[5] = src[8];
        dst[6] = src[14]; dst[7] = src[12];
        src += 16; dst += 8; pairs -= 4;
    }
    while (pairs--) {
        dst[0] = src[2];
        dst[1] = src[0];
        src += 4; dst += 2;
    }
    if (out_len & 1) {
        dst[0] = src[2];
    }
}

static void unpack_y_only(const uint8_t *src, uint8_t *dst, size_t out_len)
{
    size_t n = out_len;
    while (n >= 4) {
        dst[0] = src[2];
        dst[1] = src[6];
        dst[2] = src[10];
        dst[3] = src[14];
        src += 16; dst += 4; n -= 4;
    }
    while (n--) {
        dst[0] = src[2];
        src += 4; dst += 1;
    }
}

/* =========================================================================
 * Worker task
 * ====================================================================== */

static void dvp_frame_start(dvp_state_t *s)
{
    s->line_idx = 0;
    s->frame_start_us = esp_timer_get_time();
    if (s->cfg.line_cb) {
        s->dst = NULL;
    } else {
        s->dst = s->cfg.fb[s->fb_write];
    }
}

static void dvp_frame_end(dvp_state_t *s)
{
    int64_t now = esp_timer_get_time();
    s->last_line_count = s->line_idx;
    s->stats.last_frame_us = (uint32_t)(now - s->frame_start_us);

    if (s->line_idx != s->cfg.height) {
        s->stats.frames_torn++;
        OV7670_LOGD("torn frame: %u of %u lines", s->line_idx, s->cfg.height);
    }
    s->stats.frames_captured++;

    /* Rolling average, weighted towards recent frames. */
    if (s->stats.last_frame_us > 0) {
        float inst = 1000000.0f / (float)s->stats.last_frame_us;
        s->fps_accum = (s->fps_accum == 0.0f) ? inst
                                              : (s->fps_accum * 0.9f + inst * 0.1f);
        s->stats.fps = s->fps_accum;
    }

    if (!s->cfg.line_cb) {
        s->fb_ready = s->fb_write;
        if (s->cfg.fb_count > 1) {
            s->fb_write ^= 1;
        }
    }
    xSemaphoreGive(s->frame_sem);
}

static void dvp_task(void *arg)
{
    dvp_state_t *s = (dvp_state_t *)arg;
    int32_t evt;

    while (!s->task_quit) {
        if (xQueueReceive(s->evt_q, &evt, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        if (evt == MARK_SOF) {
            dvp_frame_start(s);
            continue;
        }
        if (evt == MARK_EOF) {
            dvp_frame_end(s);
            continue;
        }

        /* A DMA node filled. Unpack it into the current destination. */
        const uint8_t *src = s->dma_pool + (size_t)evt * s->node_dma_bytes;

        if (s->line_idx >= s->cfg.height) {
            /* More lines than the configured height - the sensor is emitting
             * a bigger window than we were told. Drop the excess rather than
             * running off the end of the frame buffer. */
            s->line_idx++;
            continue;
        }

        uint8_t *dst;
        if (s->cfg.line_cb) {
            dst = s->line_scratch;
        } else {
            if (!s->dst) { s->line_idx++; continue; }
            size_t offset = (size_t)s->line_idx * s->line_out_bytes;
            if (offset + s->line_out_bytes > s->cfg.fb_size) {
                s->line_idx++;
                continue;
            }
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
    return "I2S0 camera mode (ESP32)";
}

esp_err_t dvp_init(const dvp_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (s_dvp) dvp_deinit();

    if (cfg->pin_vsync < 0 || cfg->pin_href < 0 || cfg->pin_pclk < 0) {
        OV7670_LOGE("VSYNC/HREF/PCLK must all be assigned");
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->dma_node_count < 2) {
        OV7670_LOGE("dma_node_count must be at least 2 for double buffering");
        return ESP_ERR_INVALID_ARG;
    }
    if (!cfg->line_cb && (cfg->fb_count == 0 || !cfg->fb[0])) {
        OV7670_LOGE("no frame buffer and no line callback");
        return ESP_ERR_INVALID_ARG;
    }

    dvp_state_t *s = (dvp_state_t *)heap_caps_calloc(1, sizeof(dvp_state_t),
                                                     MALLOC_CAP_INTERNAL |
                                                     MALLOC_CAP_8BIT);
    if (!s) return ESP_ERR_NO_MEM;
    s->cfg = *cfg;

    s->line_bus_bytes = (size_t)cfg->width * cfg->bus_bpp;
    s->line_out_bytes = (size_t)cfg->width * cfg->out_bpp;
    s->node_dma_bytes = s->line_bus_bytes * 2;   /* padding byte per sample */
    s->node_count     = cfg->dma_node_count;

    if (s->node_dma_bytes > LLDESC_MAX_BYTES) {
        OV7670_LOGE("line too wide for one DMA descriptor (%u bytes > %u)",
                    (unsigned)s->node_dma_bytes, LLDESC_MAX_BYTES);
        heap_caps_free(s);
        return ESP_ERR_INVALID_ARG;
    }

    /* DMA descriptors and buffers must be in internal, DMA-capable RAM. The
     * frame buffer they are unpacked into may be PSRAM; only this pool has to
     * be reachable by the DMA engine. */
    s->desc = (lldesc_t *)heap_caps_calloc(s->node_count, sizeof(lldesc_t),
                                           MALLOC_CAP_DMA);
    s->dma_pool = (uint8_t *)heap_caps_calloc(s->node_count, s->node_dma_bytes,
                                              MALLOC_CAP_DMA);
    if (!s->desc || !s->dma_pool) {
        OV7670_LOGE("out of DMA-capable RAM: needed %u bytes for %u nodes",
                    (unsigned)(s->node_count * s->node_dma_bytes), s->node_count);
        goto fail;
    }

    if (cfg->line_cb) {
        s->line_scratch = (uint8_t *)heap_caps_calloc(1, s->line_out_bytes,
                                                      MALLOC_CAP_INTERNAL |
                                                      MALLOC_CAP_8BIT);
        if (!s->line_scratch) goto fail;
    }

    /* Circular descriptor ring, one node per line. */
    for (uint8_t i = 0; i < s->node_count; i++) {
        s->desc[i].size   = s->node_dma_bytes;
        s->desc[i].length = s->node_dma_bytes;
        s->desc[i].eof    = 0;
        s->desc[i].sosf   = 0;
        s->desc[i].owner  = 1;
        s->desc[i].offset = 0;
        s->desc[i].buf    = s->dma_pool + (size_t)i * s->node_dma_bytes;
        s->desc[i].empty  = (uint32_t)&s->desc[(i + 1) % s->node_count];
    }

    s->evt_q = xQueueCreate(s->node_count * 2 + 8, sizeof(int32_t));
    s->frame_sem = xSemaphoreCreateBinary();
    if (!s->evt_q || !s->frame_sem) goto fail;

    s_dvp = s;

    if (i2s_peripheral_config() != ESP_OK) goto fail;
    if (configure_pins(s) != ESP_OK) goto fail;

    {
        esp_err_t rc = esp_intr_alloc(ETS_I2S0_INTR_SOURCE,
                                      ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM,
                                      i2s_isr, s, &s->i2s_intr);
        if (rc != ESP_OK) {
            OV7670_LOGE("esp_intr_alloc(I2S0) failed: %s", esp_err_to_name(rc));
            goto fail;
        }
    }

    /* High priority and pinned so unpacking is never starved by application
     * work; one line of DMA slack is only tens of microseconds. */
    if (xTaskCreatePinnedToCore(dvp_task, "ov7670_dvp", 4096, s,
                                configMAX_PRIORITIES - 2, &s->task,
                                tskNO_AFFINITY) != pdPASS) {
        OV7670_LOGE("could not start the capture task");
        goto fail;
    }

    s->running = true;
    OV7670_LOGI("DVP ready: %ux%u, %u DMA nodes of %u bytes (%u B DMA RAM)",
                cfg->width, cfg->height, s->node_count,
                (unsigned)s->node_dma_bytes,
                (unsigned)(s->node_count * s->node_dma_bytes));
    return ESP_OK;

fail:
    s_dvp = NULL;
    if (s->i2s_intr) esp_intr_free(s->i2s_intr);
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
    if (s->i2s_intr) {
        esp_intr_free(s->i2s_intr);
        s->i2s_intr = NULL;
    }
    periph_module_disable(PERIPH_I2S0_MODULE);

    if (s->task) {
        s->task_quit = true;
        /* The task wakes at least every 100 ms on its own queue timeout. */
        for (int i = 0; i < 20 && s->task; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
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

    /* Drop anything left over from a previous attempt so the semaphore below
     * can only be satisfied by the frame we are about to request. */
    xQueueReset(s->evt_q);
    xSemaphoreTake(s->frame_sem, 0);

    s->want_frame = true;
    gpio_intr_enable((gpio_num_t)s->cfg.pin_vsync);

    /* Two VSYNC edges are needed: one to start the frame, one to end it. Allow
     * for that plus the frame itself before declaring a timeout. */
    BaseType_t got = xSemaphoreTake(s->frame_sem, pdMS_TO_TICKS(timeout_ms));

    gpio_intr_disable((gpio_num_t)s->cfg.pin_vsync);
    s->want_frame = false;
    if (s->capturing) {
        i2s_engine_stop();
        s->capturing = false;
    }

    if (got != pdTRUE) {
        OV7670_LOGW("capture timed out after %u ms - check XCLK, PCLK and VSYNC",
                    (unsigned)timeout_ms);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
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
    if (s->cfg.pin_vsync >= 0) {
        gpio_intr_disable((gpio_num_t)s->cfg.pin_vsync);
    }
    if (s->capturing) {
        i2s_engine_stop();
        s->capturing = false;
    }
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

#endif /* ESP32 target */
