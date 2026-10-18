/**
 * @file xclk.h
 * @brief LEDC-based XCLK generator for the OV7670.
 *
 * The OV7670 has no oscillator of its own. Every internal state machine -
 * including the SCCB slave - is clocked from XCLK, so the clock must be
 * running *before* the first SCCB transaction. Starting XCLK late is the
 * single most common cause of "PID reads 0x00 / the camera does not answer".
 *
 * A 1-bit LEDC timer is used, giving a 50% duty square wave. On the classic
 * ESP32 the low-speed LEDC group is clocked from APB (80 MHz), so the usable
 * range here is roughly 1 MHz to 40 MHz; the OV7670 accepts 10-48 MHz with
 * 24 MHz nominal, and 16-20 MHz is the sweet spot for DVP capture.
 *
 * Part of the OV7670-ESP32-NonFIFO library. MIT licensed.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start generating XCLK on @p pin.
 *
 * @param pin      GPIO driving the sensor XCLK input.
 * @param freq_hz  Requested frequency in Hz (8..24 MHz recommended).
 * @param ledc_timer   LEDC timer index to use (0..3).
 * @param ledc_channel LEDC channel index to use (0..7).
 */
esp_err_t xclk_start(int8_t pin, uint32_t freq_hz,
                     uint8_t ledc_timer, uint8_t ledc_channel);

/** @brief Stop the clock and park the pin low. */
void xclk_stop(void);

/**
 * @brief Frequency the LEDC hardware actually produced.
 *
 * LEDC can only divide the source clock by integers at 1-bit resolution, so
 * the real frequency may differ from the request. The acceptance checklist in
 * TASKS.MD asks for within 5%; compare this against your requested value.
 */
uint32_t xclk_actual_freq(void);

/** @brief true if the clock is currently running. */
bool xclk_is_running(void);

#ifdef __cplusplus
}
#endif
