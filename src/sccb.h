/**
 * @file sccb.h
 * @brief SCCB (Serial Camera Control Bus) transport for the OV7670.
 *
 * SCCB is I2C-like but not I2C. The difference that matters in practice is
 * that the OV7670 does **not** support a repeated START for register reads.
 * A read must be issued as two separate bus transactions:
 *
 *   1. START, device write address, register address, STOP
 *   2. START, device read address, read one byte, NACK, STOP
 *
 * Drivers that use the usual write-then-repeated-start-read sequence get
 * garbage or NACKs from this sensor. Both back ends here do it the long way.
 *
 * Two transports are provided:
 *   - Hardware I2C (Arduino Wire, or the ESP-IDF i2c driver outside Arduino)
 *   - A bit-banged open-drain implementation, selected by setting
 *     OV7670Config::i2c_port to -1. Slower, but it sidesteps clock stretching
 *     and bus-arbitration quirks that some OV7670 clones exhibit, and works on
 *     any pair of GPIOs.
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
 * @brief Bring up the SCCB bus.
 *
 * @param sioc      SCCB clock GPIO.
 * @param siod      SCCB data GPIO.
 * @param freq_hz   Bus clock. The OV7670 datasheet specifies 400 kHz maximum
 *                  but many modules are only reliable at 100 kHz.
 * @param i2c_port  Hardware I2C peripheral index, or -1 to bit-bang.
 */
esp_err_t sccb_init(int8_t sioc, int8_t siod, uint32_t freq_hz, int8_t i2c_port);

/** @brief Release the bus and any driver resources. */
void sccb_deinit(void);

/**
 * @brief Write one sensor register.
 * @retval ESP_OK on ACK, ESP_ERR_TIMEOUT or ESP_FAIL otherwise.
 */
esp_err_t sccb_write(uint8_t reg, uint8_t value);

/**
 * @brief Read one sensor register.
 * @param[out] value Receives the register contents on success.
 */
esp_err_t sccb_read(uint8_t reg, uint8_t *value);

/**
 * @brief Read-modify-write helper: reg = (reg & ~mask) | (value & mask).
 */
esp_err_t sccb_update_bits(uint8_t reg, uint8_t mask, uint8_t value);

/**
 * @brief Write a register table terminated by OV7670_REG_TABLE_END.
 *
 * Entries with register ::OV7670_REG_DELAY are treated as millisecond delays.
 * Stops and reports failure on the first NACK.
 */
esp_err_t sccb_write_table(const void *table);

/**
 * @brief Number of transactions that have failed since sccb_init().
 */
uint32_t sccb_error_count(void);

/**
 * @brief Probe the bus for the sensor by reading and validating PID/VER.
 * @param[out] pid Receives register 0x0A (expected 0x76), may be NULL.
 * @param[out] ver Receives register 0x0B (expected 0x73), may be NULL.
 */
esp_err_t sccb_probe(uint8_t *pid, uint8_t *ver);

#ifdef __cplusplus
}
#endif
