/**
 * @file sccb.cpp
 * @brief SCCB transport implementation (hardware I2C + bit-banged fallback).
 *
 * Part of the OV7670-ESP32-NonFIFO library. MIT licensed.
 */

#include "sccb.h"
#include "ov7670_regs.h"
#include "ov7670_log.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <Wire.h>
#else
#if __has_include("driver/i2c.h")
#include "driver/i2c.h"
#define OV7670_HAVE_IDF_I2C 1
#endif
#endif

/** Retries per transaction. OV7670 clones occasionally NACK the first attempt
 *  right after power-up, so a couple of retries removes a whole class of
 *  spurious "camera not found" reports. */
#define SCCB_RETRIES        3
/** Gap between the address-write and data-read phases of a register read.
 *  The sensor needs a moment to latch the register pointer. */
#define SCCB_READ_GAP_US    100

static int8_t  s_sioc = -1;
static int8_t  s_siod = -1;
static int8_t  s_port = -1;
static bool    s_bitbang = false;
static uint32_t s_half_us = 5;      /* half bit period for the bit-banger */
static uint32_t s_errors = 0;
static bool    s_ready = false;

/* =========================================================================
 * Bit-banged SCCB
 *
 * Both lines are driven as open-drain with the internal pull-ups enabled: a
 * "1" releases the line, a "0" pulls it down. That is what SCCB expects and
 * it means a confused sensor holding a line low cannot damage the ESP32.
 * ====================================================================== */

static inline void bb_delay(void) { esp_rom_delay_us(s_half_us); }

static inline void bb_sda(int level) { gpio_set_level((gpio_num_t)s_siod, level); }
static inline void bb_scl(int level) { gpio_set_level((gpio_num_t)s_sioc, level); }
static inline int  bb_sda_read(void)  { return gpio_get_level((gpio_num_t)s_siod); }

static void bb_init_pins(void)
{
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << s_sioc) | (1ULL << s_siod);
    io.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io);
    bb_sda(1);
    bb_scl(1);
}

static void bb_start(void)
{
    bb_sda(1); bb_delay();
    bb_scl(1); bb_delay();
    bb_sda(0); bb_delay();
    bb_scl(0); bb_delay();
}

static void bb_stop(void)
{
    bb_sda(0); bb_delay();
    bb_scl(1); bb_delay();
    bb_sda(1); bb_delay();
}

/** @return 0 if the slave ACKed, -1 on NACK. */
static int bb_write_byte(uint8_t data)
{
    for (int i = 7; i >= 0; i--) {
        bb_sda((data >> i) & 1);
        bb_delay();
        bb_scl(1);
        bb_delay();
        bb_scl(0);
        bb_delay();
    }
    /* Release SDA and sample the acknowledge bit. */
    bb_sda(1);
    bb_delay();
    bb_scl(1);
    bb_delay();
    int ack = bb_sda_read();
    bb_scl(0);
    bb_delay();
    return ack ? -1 : 0;
}

static uint8_t bb_read_byte(bool ack)
{
    uint8_t data = 0;
    bb_sda(1);              /* release so the slave can drive */
    for (int i = 7; i >= 0; i--) {
        bb_delay();
        bb_scl(1);
        bb_delay();
        data |= (uint8_t)(bb_sda_read() << i);
        bb_scl(0);
    }
    /* Master acknowledge: SCCB expects NACK after the single data byte. */
    bb_sda(ack ? 0 : 1);
    bb_delay();
    bb_scl(1);
    bb_delay();
    bb_scl(0);
    bb_sda(1);
    bb_delay();
    return data;
}

static esp_err_t bb_write_reg(uint8_t reg, uint8_t val)
{
    bb_start();
    if (bb_write_byte(OV7670_SCCB_ADDR_WRITE) != 0) { bb_stop(); return ESP_FAIL; }
    if (bb_write_byte(reg) != 0)                    { bb_stop(); return ESP_FAIL; }
    if (bb_write_byte(val) != 0)                    { bb_stop(); return ESP_FAIL; }
    bb_stop();
    return ESP_OK;
}

static esp_err_t bb_read_reg(uint8_t reg, uint8_t *val)
{
    /* Phase 1: set the register pointer, then a full STOP. */
    bb_start();
    if (bb_write_byte(OV7670_SCCB_ADDR_WRITE) != 0) { bb_stop(); return ESP_FAIL; }
    if (bb_write_byte(reg) != 0)                    { bb_stop(); return ESP_FAIL; }
    bb_stop();

    esp_rom_delay_us(SCCB_READ_GAP_US);

    /* Phase 2: a brand new transaction to read the byte back. */
    bb_start();
    if (bb_write_byte(OV7670_SCCB_ADDR_READ) != 0)  { bb_stop(); return ESP_FAIL; }
    *val = bb_read_byte(false);
    bb_stop();
    return ESP_OK;
}

/* =========================================================================
 * Hardware I2C
 * ====================================================================== */

#ifdef ARDUINO

static TwoWire *s_wire = nullptr;

static esp_err_t hw_init(uint32_t freq_hz)
{
    s_wire = (s_port == 1) ? &Wire1 : &Wire;
    if (!s_wire->begin(s_siod, s_sioc, freq_hz)) {
        return ESP_FAIL;
    }
    s_wire->setTimeOut(50);
    return ESP_OK;
}

static void hw_deinit(void)
{
    if (s_wire) { s_wire->end(); s_wire = nullptr; }
}

static esp_err_t hw_write_reg(uint8_t reg, uint8_t val)
{
    s_wire->beginTransmission(OV7670_SCCB_ADDR_7BIT);
    s_wire->write(reg);
    s_wire->write(val);
    return (s_wire->endTransmission(true) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t hw_read_reg(uint8_t reg, uint8_t *val)
{
    /* Two separate transactions - see the note in sccb.h. */
    s_wire->beginTransmission(OV7670_SCCB_ADDR_7BIT);
    s_wire->write(reg);
    if (s_wire->endTransmission(true) != 0) {
        return ESP_FAIL;
    }
    esp_rom_delay_us(SCCB_READ_GAP_US);
    if (s_wire->requestFrom((int)OV7670_SCCB_ADDR_7BIT, 1, (int)true) != 1) {
        return ESP_FAIL;
    }
    *val = (uint8_t)s_wire->read();
    return ESP_OK;
}

#elif defined(OV7670_HAVE_IDF_I2C)

static esp_err_t hw_init(uint32_t freq_hz)
{
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = s_siod;
    conf.scl_io_num = s_sioc;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = freq_hz;
    esp_err_t rc = i2c_param_config((i2c_port_t)s_port, &conf);
    if (rc != ESP_OK) return rc;
    return i2c_driver_install((i2c_port_t)s_port, I2C_MODE_MASTER, 0, 0, 0);
}

static void hw_deinit(void)
{
    i2c_driver_delete((i2c_port_t)s_port);
}

static esp_err_t hw_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device((i2c_port_t)s_port, OV7670_SCCB_ADDR_7BIT,
                                      buf, sizeof(buf), pdMS_TO_TICKS(50));
}

static esp_err_t hw_read_reg(uint8_t reg, uint8_t *val)
{
    esp_err_t rc = i2c_master_write_to_device((i2c_port_t)s_port,
                                              OV7670_SCCB_ADDR_7BIT,
                                              &reg, 1, pdMS_TO_TICKS(50));
    if (rc != ESP_OK) return rc;
    esp_rom_delay_us(SCCB_READ_GAP_US);
    return i2c_master_read_from_device((i2c_port_t)s_port, OV7670_SCCB_ADDR_7BIT,
                                       val, 1, pdMS_TO_TICKS(50));
}

#else

/* No hardware I2C available in this build - force the bit-banged transport. */
static esp_err_t hw_init(uint32_t) { return ESP_ERR_NOT_SUPPORTED; }
static void hw_deinit(void) {}
static esp_err_t hw_write_reg(uint8_t, uint8_t) { return ESP_ERR_NOT_SUPPORTED; }
static esp_err_t hw_read_reg(uint8_t, uint8_t *) { return ESP_ERR_NOT_SUPPORTED; }

#endif

/* =========================================================================
 * Public API
 * ====================================================================== */

esp_err_t sccb_init(int8_t sioc, int8_t siod, uint32_t freq_hz, int8_t i2c_port)
{
    if (sioc < 0 || siod < 0) {
        OV7670_LOGE("SCCB pins not set (sioc=%d siod=%d)", sioc, siod);
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ready) sccb_deinit();

    s_sioc = sioc;
    s_siod = siod;
    s_port = i2c_port;
    s_errors = 0;
    if (freq_hz == 0) freq_hz = 100000;
    s_half_us = 1000000UL / (2UL * freq_hz);
    if (s_half_us == 0) s_half_us = 1;

    s_bitbang = (i2c_port < 0);
    if (s_bitbang) {
        bb_init_pins();
        OV7670_LOGI("SCCB bit-banged on SIOC=%d SIOD=%d (~%u Hz)",
                    sioc, siod, (unsigned)freq_hz);
    } else {
        esp_err_t rc = hw_init(freq_hz);
        if (rc != ESP_OK) {
            OV7670_LOGE("hardware I2C init failed: %s", esp_err_to_name(rc));
            return rc;
        }
        OV7670_LOGI("SCCB on I2C%d SIOC=%d SIOD=%d @ %u Hz",
                    i2c_port, sioc, siod, (unsigned)freq_hz);
    }
    s_ready = true;
    return ESP_OK;
}

void sccb_deinit(void)
{
    if (!s_ready) return;
    if (!s_bitbang) hw_deinit();
    s_ready = false;
}

esp_err_t sccb_write(uint8_t reg, uint8_t value)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    esp_err_t rc = ESP_FAIL;
    for (int attempt = 0; attempt < SCCB_RETRIES; attempt++) {
        rc = s_bitbang ? bb_write_reg(reg, value) : hw_write_reg(reg, value);
        if (rc == ESP_OK) break;
        esp_rom_delay_us(200);
    }
    if (rc != ESP_OK) {
        s_errors++;
        OV7670_LOGW("SCCB write 0x%02X=0x%02X failed", reg, value);
    }
    return rc;
}

esp_err_t sccb_read(uint8_t reg, uint8_t *value)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (!value) return ESP_ERR_INVALID_ARG;
    esp_err_t rc = ESP_FAIL;
    for (int attempt = 0; attempt < SCCB_RETRIES; attempt++) {
        rc = s_bitbang ? bb_read_reg(reg, value) : hw_read_reg(reg, value);
        if (rc == ESP_OK) break;
        esp_rom_delay_us(200);
    }
    if (rc != ESP_OK) {
        s_errors++;
        OV7670_LOGW("SCCB read 0x%02X failed", reg);
    }
    return rc;
}

esp_err_t sccb_update_bits(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t current;
    esp_err_t rc = sccb_read(reg, &current);
    if (rc != ESP_OK) return rc;
    uint8_t updated = (uint8_t)((current & ~mask) | (value & mask));
    if (updated == current) return ESP_OK;
    return sccb_write(reg, updated);
}

esp_err_t sccb_write_table(const void *table)
{
    const ov7670_reg_entry_t *e = (const ov7670_reg_entry_t *)table;
    if (!e) return ESP_ERR_INVALID_ARG;

    for (; !(e->reg == OV7670_REG_TABLE_END &&
             e->val == OV7670_REG_TABLE_END_VAL); e++) {
        if (e->reg == OV7670_REG_DELAY) {
            vTaskDelay(pdMS_TO_TICKS(e->val));
            continue;
        }
        esp_err_t rc = sccb_write(e->reg, e->val);
        if (rc != ESP_OK) {
            OV7670_LOGE("register table aborted at 0x%02X", e->reg);
            return rc;
        }
        /* The datasheet requires a settling gap between SCCB writes; without
         * it a long table can leave the sensor in a half-configured state. */
        esp_rom_delay_us(100);
    }
    return ESP_OK;
}

uint32_t sccb_error_count(void)
{
    return s_errors;
}

esp_err_t sccb_probe(uint8_t *pid, uint8_t *ver)
{
    uint8_t p = 0, v = 0;
    esp_err_t rc = sccb_read(REG_PID, &p);
    if (rc != ESP_OK) return rc;
    rc = sccb_read(REG_VER, &v);
    if (rc != ESP_OK) return rc;

    if (pid) *pid = p;
    if (ver) *ver = v;

    if (p != OV7670_PID_VALUE || v != OV7670_VER_VALUE) {
        OV7670_LOGE("unexpected sensor ID: PID=0x%02X VER=0x%02X (want 0x%02X/0x%02X)",
                    p, v, OV7670_PID_VALUE, OV7670_VER_VALUE);
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}
