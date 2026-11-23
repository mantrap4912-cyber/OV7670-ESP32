/**
 * @file xclk.cpp
 * @brief LEDC-based XCLK generator implementation.
 *
 * Part of the OV7670-ESP32-NonFIFO library. MIT licensed.
 */

#include "xclk.h"
#include "ov7670_log.h"

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Low-speed group is available on every ESP32 variant this library targets. */
#define XCLK_SPEED_MODE     LEDC_LOW_SPEED_MODE
/* 1-bit resolution: duty 1 of 2 is an exact 50% square wave, and it maximises
 * the achievable frequency (freq * 2^resolution must fit the source clock). */
#define XCLK_DUTY_RES       LEDC_TIMER_1_BIT
#define XCLK_DUTY_VALUE     1

static int8_t   s_pin = -1;
static uint8_t  s_timer = 0;
static uint8_t  s_channel = 0;
static uint32_t s_actual = 0;
static bool     s_running = false;

esp_err_t xclk_start(int8_t pin, uint32_t freq_hz,
                     uint8_t ledc_timer, uint8_t ledc_channel)
{
    if (pin < 0) {
        OV7670_LOGE("XCLK pin not set - the sensor will never respond to SCCB");
        return ESP_ERR_INVALID_ARG;
    }
    if (freq_hz < 1000000UL || freq_hz > 40000000UL) {
        OV7670_LOGE("XCLK %u Hz out of range (1..40 MHz)", (unsigned)freq_hz);
        return ESP_ERR_INVALID_ARG;
    }
    if (ledc_timer > 3 || ledc_channel > 7) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_running) xclk_stop();

    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode      = XCLK_SPEED_MODE;
    timer_cfg.duty_resolution = XCLK_DUTY_RES;
    timer_cfg.timer_num       = (ledc_timer_t)ledc_timer;
    timer_cfg.freq_hz         = freq_hz;
    timer_cfg.clk_cfg         = LEDC_AUTO_CLK;

    esp_err_t rc = ledc_timer_config(&timer_cfg);
    if (rc != ESP_OK) {
        OV7670_LOGE("ledc_timer_config(%u Hz) failed: %s",
                    (unsigned)freq_hz, esp_err_to_name(rc));
        return rc;
    }

    ledc_channel_config_t ch_cfg = {};
    ch_cfg.gpio_num   = pin;
    ch_cfg.speed_mode = XCLK_SPEED_MODE;
    ch_cfg.channel    = (ledc_channel_t)ledc_channel;
    ch_cfg.intr_type  = LEDC_INTR_DISABLE;
    ch_cfg.timer_sel  = (ledc_timer_t)ledc_timer;
    ch_cfg.duty       = XCLK_DUTY_VALUE;
    ch_cfg.hpoint     = 0;

    rc = ledc_channel_config(&ch_cfg);
    if (rc != ESP_OK) {
        OV7670_LOGE("ledc_channel_config failed: %s", esp_err_to_name(rc));
        return rc;
    }

    s_pin = pin;
    s_timer = ledc_timer;
    s_channel = ledc_channel;
    s_running = true;
    s_actual = ledc_get_freq(XCLK_SPEED_MODE, (ledc_timer_t)ledc_timer);

    /* LEDC divides the source clock by an integer, so the delivered frequency
     * is usually a little off the request. Warn once if the error is enough to
     * push sensor timing outside the datasheet window. */
    if (s_actual != 0) {
        uint32_t hi = (freq_hz > s_actual) ? freq_hz : s_actual;
        uint32_t lo = (freq_hz > s_actual) ? s_actual : freq_hz;
        if ((hi - lo) * 100 > freq_hz * 5) {
            OV7670_LOGW("XCLK requested %u Hz but LEDC produced %u Hz (>5%% off)",
                        (unsigned)freq_hz, (unsigned)s_actual);
        }
    }

    OV7670_LOGI("XCLK %u Hz on GPIO%d (LEDC timer %u, channel %u)",
                (unsigned)s_actual, pin, ledc_timer, ledc_channel);

    /* Give the sensor's internal PLL and state machines time to lock before
     * anyone tries to talk to it over SCCB. */
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

void xclk_stop(void)
{
    if (!s_running) return;
    ledc_stop(XCLK_SPEED_MODE, (ledc_channel_t)s_channel, 0);
    if (s_pin >= 0) {
        gpio_reset_pin((gpio_num_t)s_pin);
    }
    s_running = false;
    s_actual = 0;
}

uint32_t xclk_actual_freq(void)
{
    return s_running ? s_actual : 0;
}

bool xclk_is_running(void)
{
    return s_running;
}
