/**
 * @file ov7670_log.h
 * @brief Thin logging shim so the library behaves the same under the Arduino
 *        core and plain ESP-IDF.
 *
 * Define OV7670_LOG_LEVEL before including the library (or as a build flag)
 * to control verbosity: 0 = silent, 1 = errors, 2 = +warnings, 3 = +info,
 * 4 = +debug. Default is 3 under Arduino, 4 under ESP-IDF where the IDF log
 * level filter applies on top.
 *
 * Part of the OV7670-ESP32-NonFIFO library. MIT licensed.
 */

#pragma once

#include "esp_log.h"

#ifndef OV7670_LOG_LEVEL
#define OV7670_LOG_LEVEL 3
#endif

#define OV7670_LOG_TAG "ov7670"

#if OV7670_LOG_LEVEL >= 1
#define OV7670_LOGE(fmt, ...) ESP_LOGE(OV7670_LOG_TAG, fmt, ##__VA_ARGS__)
#else
#define OV7670_LOGE(fmt, ...) do {} while (0)
#endif

#if OV7670_LOG_LEVEL >= 2
#define OV7670_LOGW(fmt, ...) ESP_LOGW(OV7670_LOG_TAG, fmt, ##__VA_ARGS__)
#else
#define OV7670_LOGW(fmt, ...) do {} while (0)
#endif

#if OV7670_LOG_LEVEL >= 3
#define OV7670_LOGI(fmt, ...) ESP_LOGI(OV7670_LOG_TAG, fmt, ##__VA_ARGS__)
#else
#define OV7670_LOGI(fmt, ...) do {} while (0)
#endif

#if OV7670_LOG_LEVEL >= 4
#define OV7670_LOGD(fmt, ...) ESP_LOGD(OV7670_LOG_TAG, fmt, ##__VA_ARGS__)
#else
#define OV7670_LOGD(fmt, ...) do {} while (0)
#endif

/** Return @p err from the enclosing function if @p expr is not ESP_OK. */
#define OV7670_RETURN_ON_ERR(expr, msg)                                        \
    do {                                                                       \
        esp_err_t _rc = (expr);                                                \
        if (_rc != ESP_OK) {                                                   \
            OV7670_LOGE("%s failed: %s", msg, esp_err_to_name(_rc));           \
            return _rc;                                                        \
        }                                                                      \
    } while (0)
