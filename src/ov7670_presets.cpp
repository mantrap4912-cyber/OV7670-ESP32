/**
 * @file ov7670_presets.cpp
 * @brief Known-good register tables. See ov7670_presets.h for provenance.
 *
 * Part of the OV7670-ESP32-NonFIFO library. MIT licensed.
 */

#include "ov7670_presets.h"

#define TABLE_END { OV7670_REG_TABLE_END, OV7670_REG_TABLE_END_VAL }

/* =========================================================================
 * Base initialisation
 * ====================================================================== */

const ov7670_reg_entry_t ov7670_default_regs[] = {
    /* Start from a defined state. COM7_RESET is self-clearing but needs a
     * moment before the sensor accepts further writes. */
    { REG_COM7,   COM7_RESET },
    { OV7670_REG_DELAY, 10 },

    { REG_TSLB,   TSLB_YLAST },
    { REG_COM7,   0x00 },               /* VGA, YUV - refined later */

    /* Default VGA window (values from OmniVision via the Linux driver) */
    { REG_HSTART, 0x13 }, { REG_HSTOP,  0x01 }, { REG_HREF,   0xB6 },
    { REG_VSTART, 0x02 }, { REG_VSTOP,  0x7A }, { REG_VREF,   0x0A },

    { REG_COM3,   0x00 }, { REG_COM14,  0x00 },
    { REG_SCALING_XSC, 0x3A }, { REG_SCALING_YSC, 0x35 },
    { REG_SCALING_DCWCTR, 0x11 }, { REG_SCALING_PCLK_DIV, 0xF0 },
    { REG_SCALING_PCLK_DELAY, 0x02 },
    { REG_COM10,  0x00 },

    /* Gamma curve */
    { REG_SLOP,   0x20 },
    { 0x7B, 0x10 }, { 0x7C, 0x1E }, { 0x7D, 0x35 }, { 0x7E, 0x5A },
    { 0x7F, 0x69 }, { 0x80, 0x76 }, { 0x81, 0x80 }, { 0x82, 0x88 },
    { 0x83, 0x8F }, { 0x84, 0x96 }, { 0x85, 0xA3 }, { 0x86, 0xAF },
    { 0x87, 0xC4 }, { 0x88, 0xD7 }, { 0x89, 0xE8 },

    /* AGC / AEC parameters. AGC and AEC themselves are switched on further
     * down, once their tuning registers are in place. */
    { REG_COM8,   COM8_FASTAEC | COM8_AECSTEP },
    { REG_GAIN,   0x00 }, { REG_AECH,   0x00 },
    { REG_COM4,   0x40 },
    { REG_COM9,   0x18 },                       /* 4x gain ceiling */
    { REG_BD50MAX, 0x05 }, { REG_BD60MAX, 0x07 },
    { REG_AEW,    0x95 }, { REG_AEB,    0x33 },
    { REG_VPT,    0xE3 },
    { REG_HAECC1, 0x78 }, { REG_HAECC2, 0x68 },
    { 0xA1, 0x03 },
    { REG_HAECC3, 0xD8 }, { REG_HAECC4, 0xD8 },
    { REG_HAECC5, 0xF0 }, { REG_HAECC6, 0x90 }, { REG_HAECC7, 0x94 },
    { REG_COM8,   COM8_FASTAEC | COM8_AECSTEP | COM8_BFILT |
                  COM8_AGC | COM8_AEC },

    /* Reserved / magic analogue and array settings. */
    { REG_COM5,   0x61 }, { REG_COM6,   0x4B },
    { REG_RSVD_16, 0x02 },
    { REG_MVFP,   0x07 },
    { REG_ADCCTR1, 0x02 }, { REG_ADCCTR2, 0x91 },
    { 0x29, 0x07 },
    { REG_CHLF,   0x0B },
    { 0x35, 0x0B },
    { REG_ADC,    0x1D }, { REG_ACOM,   0x71 }, { REG_OFON,   0x2A },
    { REG_COM12,  0x78 },
    { 0x4D, 0x40 }, { 0x4E, 0x20 },
    { REG_GFIX,   0x00 },
    { REG_DBLV,   0x4A },
    { REG_REG74,  0x10 },
    { 0x8D, 0x4F }, { 0x8E, 0x00 }, { 0x8F, 0x00 }, { 0x90, 0x00 },
    { 0x91, 0x00 }, { 0x96, 0x00 }, { 0x9A, 0x00 },
    { 0xB0, 0x84 }, { 0xB1, 0x0C }, { 0xB2, 0x0E }, { 0xB3, 0x82 },
    { 0xB8, 0x0A },

    /* More reserved magic; several of these tune white balance. */
    { REG_AWBC1,  0x0A }, { REG_AWBC2,  0xF0 },
    { REG_AWBC3,  0x34 }, { REG_AWBC4,  0x58 },
    { REG_AWBC5,  0x28 }, { REG_AWBC6,  0x3A },
    { REG_AWBC7,  0x88 }, { REG_AWBC8,  0x88 },
    { REG_AWBC9,  0x44 }, { REG_AWBC10, 0x67 },
    { REG_AWBC11, 0x49 }, { REG_AWBC12, 0x0E },
    { REG_AWBCTR3, 0x0A }, { REG_AWBCTR2, 0x55 },
    { REG_AWBCTR1, 0x11 }, { REG_AWBCTR0, 0x9F },
    { REG_GGAIN,  0x40 }, { REG_BLUE,   0x40 }, { REG_RED,    0x60 },
    { REG_COM8,   COM8_FASTAEC | COM8_AECSTEP | COM8_BFILT |
                  COM8_AGC | COM8_AEC | COM8_AWB },

    /* Colour matrix (YUV defaults; the format tables override for RGB). */
    { REG_MTX1, 0x80 }, { REG_MTX2, 0x80 }, { REG_MTX3, 0x00 },
    { REG_MTX4, 0x22 }, { REG_MTX5, 0x5E }, { REG_MTX6, 0x80 },
    { REG_MTXS, 0x9E },

    { REG_COM16,  COM16_AWBGAIN },
    { REG_EDGE,   0x00 },
    { 0x75, 0x05 }, { REG_REG76, 0xE1 },
    { REG_DNSTH,  0x00 }, { 0x77, 0x01 },
    { REG_COM13,  COM13_GAMMA | COM13_UVSAT | 0x03 },
    { REG_REG4B,  0x09 },
    { 0xC9, 0x60 },
    { REG_COM16,  0x38 },
    { REG_CONTRAS, 0x40 },
    { REG_ARBLM,  0x11 },
    { REG_COM11,  COM11_EXP | COM11_HZAUTO },
    { 0xA4, 0x88 },
    { 0x96, 0x00 }, { 0x97, 0x30 }, { 0x98, 0x20 }, { 0x99, 0x30 },
    { 0x9A, 0x84 }, { 0x9B, 0x29 }, { 0x9C, 0x03 }, { 0x9D, 0x4C },
    { 0x9E, 0x3F },
    { 0x78, 0x04 },

    /* Undocumented indexed-register sequence. Register 0x79 selects a bank
     * and 0xC8 writes it; the sequence below is reproduced verbatim from the
     * reference settings and must not be reordered. */
    { 0x79, 0x01 }, { 0xC8, 0xF0 },
    { 0x79, 0x0F }, { 0xC8, 0x00 },
    { 0x79, 0x10 }, { 0xC8, 0x7E },
    { 0x79, 0x0A }, { 0xC8, 0x80 },
    { 0x79, 0x0B }, { 0xC8, 0x01 },
    { 0x79, 0x0C }, { 0xC8, 0x0F },
    { 0x79, 0x0D }, { 0xC8, 0x20 },
    { 0x79, 0x09 }, { 0xC8, 0x80 },
    { 0x79, 0x02 }, { 0xC8, 0xC0 },
    { 0x79, 0x03 }, { 0xC8, 0x40 },
    { 0x79, 0x05 }, { 0xC8, 0x30 },
    { 0x79, 0x26 },

    TABLE_END
};

/* =========================================================================
 * Pixel format tables
 *
 * Each table sets COM7's format bits, COM15's range/packing bits and the
 * colour matrix appropriate to that colour space. The resolution bits of COM7
 * are OR-ed in afterwards by ov7670_apply_window().
 * ====================================================================== */

static const ov7670_reg_entry_t fmt_rgb565[] = {
    { REG_COM7,   COM7_RGB },
    { REG_RGB444, 0x00 },                   /* RGB444 off */
    { REG_COM1,   0x00 },
    { REG_COM15,  COM15_R00FF | COM15_RGB565 },
    { REG_COM9,   0x38 },                   /* 16x gain ceiling, bit3 reserved */
    { REG_MTX1, 0xB3 }, { REG_MTX2, 0xB3 }, { REG_MTX3, 0x00 },
    { REG_MTX4, 0x3D }, { REG_MTX5, 0xA7 }, { REG_MTX6, 0xE4 },
    { REG_MTXS, 0x9E },
    { REG_COM13,  COM13_GAMMA | COM13_UVSAT },
    TABLE_END
};

static const ov7670_reg_entry_t fmt_rgb555[] = {
    { REG_COM7,   COM7_RGB },
    { REG_RGB444, 0x00 },
    { REG_COM1,   0x00 },
    { REG_COM15,  COM15_RGB555 | COM15_R00FF },
    { REG_COM9,   0x38 },
    { REG_MTX1, 0xB3 }, { REG_MTX2, 0xB3 }, { REG_MTX3, 0x00 },
    { REG_MTX4, 0x3D }, { REG_MTX5, 0xA7 }, { REG_MTX6, 0xE4 },
    { REG_MTXS, 0x9E },
    { REG_COM13,  COM13_GAMMA | COM13_UVSAT },
    TABLE_END
};

static const ov7670_reg_entry_t fmt_rgb444[] = {
    { REG_COM7,   COM7_RGB },
    { REG_RGB444, R444_ENABLE },            /* RRRRGGGG BBBBxxxx */
    { REG_COM1,   0x40 },
    { REG_COM15,  COM15_R01FE | COM15_RGB565 },
    { REG_COM9,   0x38 },
    { REG_MTX1, 0xB3 }, { REG_MTX2, 0xB3 }, { REG_MTX3, 0x00 },
    { REG_MTX4, 0x3D }, { REG_MTX5, 0xA7 }, { REG_MTX6, 0xE4 },
    { REG_MTXS, 0x9E },
    { REG_COM13,  COM13_GAMMA | COM13_UVSAT },
    TABLE_END
};

static const ov7670_reg_entry_t fmt_yuv422[] = {
    { REG_COM7,   COM7_YUV },
    { REG_RGB444, 0x00 },
    { REG_COM1,   0x00 },
    { REG_COM15,  COM15_R00FF },
    { REG_COM9,   0x48 },                   /* 32x gain ceiling */
    { REG_MTX1, 0x80 }, { REG_MTX2, 0x80 }, { REG_MTX3, 0x00 },
    { REG_MTX4, 0x22 }, { REG_MTX5, 0x5E }, { REG_MTX6, 0x80 },
    { REG_MTXS, 0x9E },
    { REG_COM13,  COM13_GAMMA | COM13_UVSAT },
    TABLE_END
};

/* Grayscale is YUV422 on the wire; the Y bytes are extracted during DMA
 * unpacking, so the sensor-side table is identical to YUV422. */
#define fmt_grayscale fmt_yuv422

static const ov7670_reg_entry_t fmt_bayer[] = {
    { REG_COM7,   COM7_BAYER },
    { REG_RGB444, 0x00 },
    { REG_COM1,   0x00 },
    { REG_COM15,  COM15_R00FF },
    { REG_COM9,   0x38 },
    { REG_COM13,  0x08 },                   /* gamma off, no UV auto-adjust */
    TABLE_END
};

const ov7670_reg_entry_t *const ov7670_format_regs[OV7670_PIXFORMAT_COUNT] = {
    /* OV7670_PIXFORMAT_RGB565    */ fmt_rgb565,
    /* OV7670_PIXFORMAT_RGB555    */ fmt_rgb555,
    /* OV7670_PIXFORMAT_RGB444    */ fmt_rgb444,
    /* OV7670_PIXFORMAT_YUV422    */ fmt_yuv422,
    /* OV7670_PIXFORMAT_GRAYSCALE */ fmt_grayscale,
    /* OV7670_PIXFORMAT_BAYER     */ fmt_bayer,
};

/* =========================================================================
 * Windowing / scaling
 * ====================================================================== */

/* QCIF needs the sensor left in VGA mode with manual scaling; selecting
 * COM7_FMT_QCIF alone produces a badly cropped image on most modules. */
static const ov7670_reg_entry_t win_qcif_extra[] = {
    { REG_COM3,   COM3_SWAP | COM3_SCALEEN },
    { REG_COM14,  COM14_DCWEN | 0x01 },
    { REG_SCALING_PCLK_DIV, PCLK_DIV(1) },
    { REG_SCALING_PCLK_DELAY, 0x52 },
    { 0x7B, 0x1C }, { 0x7C, 0x28 }, { 0x7D, 0x3C }, { 0x7F, 0x69 },
    { REG_COM9,   0x38 },
    { 0xA1, 0x0B },
    { REG_REG74,  0x19 },
    { 0x9A, 0x80 },
    { REG_AWBC1,  0x14 },
    { REG_COM13,  0xC0 },
    TABLE_END
};

/* QQVGA = QVGA windowing plus a further 2x DCW in both axes. */
static const ov7670_reg_entry_t win_qqvga_extra[] = {
    { REG_COM3,   COM3_DCWEN },
    { REG_COM14,  COM14_DCWEN | COM14_MANUAL_SCALE | 0x01 },
    { REG_SCALING_XSC, 0x3A },
    { REG_SCALING_YSC, 0x35 },
    { REG_SCALING_DCWCTR, DCWCTR_DOWN(1, 1) },
    { REG_SCALING_PCLK_DIV, PCLK_DIV(1) },
    { REG_SCALING_PCLK_DELAY, 0x02 },
    TABLE_END
};

/* QQCIF = QCIF windowing plus a further 2x DCW in both axes. */
static const ov7670_reg_entry_t win_qqcif_extra[] = {
    { REG_COM3,   COM3_SWAP | COM3_SCALEEN | COM3_DCWEN },
    { REG_COM14,  COM14_DCWEN | COM14_MANUAL_SCALE | 0x02 },
    { REG_SCALING_XSC, 0x3A },
    { REG_SCALING_YSC, 0x35 },
    { REG_SCALING_DCWCTR, DCWCTR_DOWN(2, 2) },
    { REG_SCALING_PCLK_DIV, PCLK_DIV(2) },
    { REG_SCALING_PCLK_DELAY, 0x52 },
    TABLE_END
};

const ov7670_win_t ov7670_windows[OV7670_FRAMESIZE_COUNT] = {
    /* QQCIF  88 x  72 */
    {  88,  72, COM7_FMT_QCIF, 456,  24,  14, 494, win_qqcif_extra },
    /* QQVGA 160 x 120 */
    { 160, 120, COM7_FMT_QVGA, 168,  24,  12, 492, win_qqvga_extra },
    /* QCIF  176 x 144 */
    { 176, 144, COM7_FMT_QCIF, 456,  24,  14, 494, win_qcif_extra },
    /* QVGA  320 x 240 */
    { 320, 240, COM7_FMT_QVGA, 168,  24,  12, 492, NULL },
    /* CIF   352 x 288 */
    { 352, 288, COM7_FMT_CIF,  170,  90,  14, 494, NULL },
    /* VGA   640 x 480 */
    { 640, 480, COM7_FMT_VGA,  158,  14,  10, 490, NULL },
};

/* =========================================================================
 * Image control tables
 * ====================================================================== */

/* MTX1..MTX6 plus MTXS. Saturation scales the first six around these. */
const uint8_t ov7670_matrix_rgb[7] = { 0xB3, 0xB3, 0x00, 0x3D, 0xA7, 0xE4, 0x9E };
const uint8_t ov7670_matrix_yuv[7] = { 0x80, 0x80, 0x00, 0x22, 0x5E, 0x80, 0x9E };

/* Fixed U/V pairs written to MANU/MANV with TSLB_FIXED_UV set. */
const uint8_t ov7670_effect_uv[OV7670_EFFECT_COUNT][2] = {
    /* NONE     */ { 0x80, 0x80 },
    /* BW       */ { 0x80, 0x80 },
    /* SEPIA    */ { 0x40, 0xA0 },
    /* BLUISH   */ { 0xA0, 0x40 },
    /* GREENISH */ { 0x60, 0x60 },
    /* REDDISH  */ { 0x80, 0xC0 },
};

/* BLUE (0x01), RED (0x02), GGAIN (0x6A) */
const uint8_t ov7670_wb_gains[OV7670_WB_COUNT][3] = {
    /* AUTO   */ { 0x40, 0x60, 0x40 },  /* unused; AWB drives the gains */
    /* SUNNY  */ { 0x5A, 0x5C, 0x40 },
    /* CLOUDY */ { 0x58, 0x60, 0x40 },
    /* OFFICE */ { 0x84, 0x4C, 0x40 },
    /* HOME   */ { 0x96, 0x40, 0x40 },
};

/* REG_BRIGHT is sign-magnitude: bit 7 is the sign, bits 6:0 the magnitude. */
const uint8_t ov7670_brightness_tbl[9] = {
    0xB8, 0xB0, 0xA0, 0x90, 0x00, 0x10, 0x20, 0x30, 0x38
};

const uint8_t ov7670_contrast_tbl[9] = {
    0x18, 0x20, 0x28, 0x30, 0x40, 0x50, 0x58, 0x60, 0x68
};
