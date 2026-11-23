/**
 * @file ov7670_presets.h
 * @brief Known-good register tables per resolution and pixel format.
 *
 * Provenance of the tables in ov7670_presets.cpp:
 *
 *  - The base initialisation table and the per-format tables are derived from
 *    the mainline Linux driver drivers/media/i2c/ov7670.c (GPL-2.0), which is
 *    itself derived from OmniVision's reference settings. They are reproduced
 *    here as register/value pairs - facts about the hardware, not code - and
 *    re-expressed against this library's own table format and symbol names.
 *  - The windowing values (HSTART/HSTOP/VSTART/VSTOP) per frame size come from
 *    the same driver, which credits them to OmniVision.
 *  - The DCW down-sampling values used for QQVGA and QQCIF follow the
 *    "OV7670 Software Application Note" scaling section.
 *
 * A large fraction of these registers are undocumented "reserved" values that
 * simply have to be written for the sensor to produce a usable image. Do not
 * be tempted to prune the ones with no comment - several of them matter.
 *
 * Part of the OV7670-ESP32-NonFIFO library. MIT licensed.
 */

#pragma once

#include "ov7670_regs.h"
#include "ov7670_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Base initialisation, written once after soft reset. */
extern const ov7670_reg_entry_t ov7670_default_regs[];

/** @brief Per-pixel-format register tables, indexed by ov7670_pixformat_t. */
extern const ov7670_reg_entry_t *const ov7670_format_regs[OV7670_PIXFORMAT_COUNT];

/**
 * @brief Windowing and scaling description for one output resolution.
 */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t  com7_fmt;      /*!< COM7 resolution bits (COM7_FMT_*) */
    uint16_t hstart;        /*!< 11-bit horizontal window start */
    uint16_t hstop;         /*!< 11-bit horizontal window stop */
    uint16_t vstart;        /*!< 10-bit vertical window start */
    uint16_t vstop;         /*!< 10-bit vertical window stop */
    /** Extra registers applied after windowing (DCW / scaling), or NULL. */
    const ov7670_reg_entry_t *extra;
} ov7670_win_t;

/** @brief Window table, indexed by ov7670_framesize_t. */
extern const ov7670_win_t ov7670_windows[OV7670_FRAMESIZE_COUNT];

/**
 * @brief Colour-matrix coefficients used as the base for saturation control.
 * Six coefficients (MTX1..MTX6) plus the sign register MTXS.
 */
extern const uint8_t ov7670_matrix_rgb[7];
extern const uint8_t ov7670_matrix_yuv[7];

/** @brief Fixed-UV values for the special effects, 2 bytes (MANU, MANV) each. */
extern const uint8_t ov7670_effect_uv[OV7670_EFFECT_COUNT][2];

/** @brief Manual white-balance gains, 3 bytes (BLUE, RED, GGAIN) each. */
extern const uint8_t ov7670_wb_gains[OV7670_WB_COUNT][3];

/** @brief Brightness values for levels -4..+4, written to REG_BRIGHT. */
extern const uint8_t ov7670_brightness_tbl[9];

/** @brief Contrast values for levels -4..+4, written to REG_CONTRAS. */
extern const uint8_t ov7670_contrast_tbl[9];

#ifdef __cplusplus
}
#endif
