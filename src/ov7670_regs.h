/**
 * @file ov7670_regs.h
 * @brief OV7670 SCCB register map, bit definitions and sensor constants.
 *
 * Register names and bit masks follow the OmniVision OV7670/OV7171 CMOS VGA
 * CameraChip datasheet (v1.4) and the naming convention established by the
 * mainline Linux driver drivers/media/i2c/ov7670.c, so that published
 * register tables from either source can be used verbatim.
 *
 * Part of the OV7670-ESP32-NonFIFO library. MIT licensed.
 */

#pragma once

#include <stdint.h>

/* -------------------------------------------------------------------------
 * SCCB device addressing
 *
 * PITFALL: the datasheet quotes 0x42 (write) / 0x43 (read). Those are the
 * *bus bytes*, i.e. the 7-bit address already shifted left with the R/W bit
 * appended. Every I2C API on the ESP32 (Wire, driver/i2c.h) expects the
 * 7-bit address. Passing 0x42 to Wire will silently talk to nothing.
 * ---------------------------------------------------------------------- */
#define OV7670_SCCB_ADDR_7BIT   0x21    /*!< Use this with Wire / i2c driver */
#define OV7670_SCCB_ADDR_WRITE  0x42    /*!< Raw bus byte, datasheet notation */
#define OV7670_SCCB_ADDR_READ   0x43    /*!< Raw bus byte, datasheet notation */

/* Product / manufacturer identification (read-only) */
#define OV7670_PID_VALUE        0x76    /*!< REG_PID  (0x0A) always reads 0x76 */
#define OV7670_VER_VALUE        0x73    /*!< REG_VER  (0x0B) always reads 0x73 */
#define OV7670_MIDH_VALUE       0x7F    /*!< REG_MIDH (0x1C) manufacturer ID hi */
#define OV7670_MIDL_VALUE       0xA2    /*!< REG_MIDL (0x1D) manufacturer ID lo */

/* Native sensor array */
#define OV7670_MAX_WIDTH        640
#define OV7670_MAX_HEIGHT       480

/* -------------------------------------------------------------------------
 * Register addresses
 * ---------------------------------------------------------------------- */
#define REG_GAIN            0x00    /*!< AGC gain, lower 8 bits (upper 2 in VREF) */
#define REG_BLUE            0x01    /*!< Blue channel gain (manual white balance) */
#define REG_RED             0x02    /*!< Red channel gain (manual white balance) */
#define REG_VREF            0x03    /*!< Vertical frame control + AGC/exposure hi bits */
#define REG_COM1            0x04    /*!< Common control 1 */
#define REG_BAVE            0x05    /*!< U/B average level */
#define REG_GbAVE           0x06    /*!< Y/Gb average level */
#define REG_AECHH           0x07    /*!< Exposure value, bits [15:10] */
#define REG_RAVE            0x08    /*!< V/R average level */
#define REG_COM2            0x09    /*!< Common control 2 (output drive strength) */
#define REG_PID             0x0A    /*!< Product ID high byte (0x76) */
#define REG_VER             0x0B    /*!< Product ID low byte  (0x73) */
#define REG_COM3            0x0C    /*!< Common control 3 (scaling / DCW enable) */
#define REG_COM4            0x0D    /*!< Common control 4 (average option) */
#define REG_COM5            0x0E    /*!< Common control 5 (reserved) */
#define REG_COM6            0x0F    /*!< Common control 6 (HREF / black pixels) */
#define REG_AECH            0x10    /*!< Exposure value, bits [9:2] */
#define REG_CLKRC           0x11    /*!< Internal clock prescaler */
#define REG_COM7            0x12    /*!< Common control 7 (reset, resolution, format) */
#define REG_COM8            0x13    /*!< Common control 8 (AGC/AWB/AEC enables) */
#define REG_COM9            0x14    /*!< Common control 9 (AGC gain ceiling) */
#define REG_COM10           0x15    /*!< Common control 10 (sync polarity, PCLK gating) */
#define REG_RSVD_16         0x16    /*!< Reserved */
#define REG_HSTART          0x17    /*!< Horizontal frame start, high 8 of 11 bits */
#define REG_HSTOP           0x18    /*!< Horizontal frame stop,  high 8 of 11 bits */
#define REG_VSTART          0x19    /*!< Vertical frame start,   high 8 of 10 bits */
#define REG_VSTOP           0x1A    /*!< Vertical frame stop,    high 8 of 10 bits */
#define REG_PSHFT           0x1B    /*!< Data format, pixel delay after HREF */
#define REG_MIDH            0x1C    /*!< Manufacturer ID high (0x7F) */
#define REG_MIDL            0x1D    /*!< Manufacturer ID low  (0xA2) */
#define REG_MVFP            0x1E    /*!< Mirror / VFlip enable */
#define REG_LAEC            0x1F    /*!< Reserved */
#define REG_ADCCTR0         0x20
#define REG_ADCCTR1         0x21
#define REG_ADCCTR2         0x22
#define REG_ADCCTR3         0x23
#define REG_AEW             0x24    /*!< AGC/AEC stable operating region, upper limit */
#define REG_AEB             0x25    /*!< AGC/AEC stable operating region, lower limit */
#define REG_VPT             0x26    /*!< AGC/AEC fast mode operating region */
#define REG_BBIAS           0x27
#define REG_GbBIAS          0x28
#define REG_EXHCH           0x2A    /*!< Dummy pixel insert MSB */
#define REG_EXHCL           0x2B    /*!< Dummy pixel insert LSB */
#define REG_RBIAS           0x2C
#define REG_ADVFL           0x2D    /*!< Insert dummy lines, low 8 bits */
#define REG_ADVFH           0x2E    /*!< Insert dummy lines, high 8 bits */
#define REG_YAVE            0x2F    /*!< Y/G channel average value */
#define REG_HSYST           0x30    /*!< HSYNC rising edge delay */
#define REG_HSYEN           0x31    /*!< HSYNC falling edge delay */
#define REG_HREF            0x32    /*!< HREF control, low 3 bits of HSTART/HSTOP */
#define REG_CHLF            0x33    /*!< Array current control */
#define REG_ARBLM           0x34    /*!< Array reference control */
#define REG_ADC             0x37    /*!< ADC control */
#define REG_ACOM            0x38    /*!< ADC and analog common mode control */
#define REG_OFON            0x39    /*!< ADC offset control */
#define REG_TSLB            0x3A    /*!< Line buffer test option / UV output order */
#define REG_COM11           0x3B    /*!< Common control 11 (night mode, banding) */
#define REG_COM12           0x3C    /*!< Common control 12 (HREF option) */
#define REG_COM13           0x3D    /*!< Common control 13 (gamma, UV saturation) */
#define REG_COM14           0x3E    /*!< Common control 14 (DCW / PCLK divider) */
#define REG_EDGE            0x3F    /*!< Edge enhancement adjustment */
#define REG_COM15           0x40    /*!< Common control 15 (output range, RGB565/555) */
#define REG_COM16           0x41    /*!< Common control 16 (AWB gain, denoise) */
#define REG_COM17           0x42    /*!< Common control 17 (AEC window, colour bar) */
#define REG_AWBC1           0x43    /*!< AWB control 1..6 (reserved / magic) */
#define REG_AWBC2           0x44
#define REG_AWBC3           0x45
#define REG_AWBC4           0x46
#define REG_AWBC5           0x47
#define REG_AWBC6           0x48
#define REG_REG4B           0x4B
#define REG_DNSTH           0x4C    /*!< De-noise strength */
#define REG_MTX1            0x4F    /*!< Colour matrix coefficient 1 */
#define REG_MTX2            0x50
#define REG_MTX3            0x51
#define REG_MTX4            0x52
#define REG_MTX5            0x53
#define REG_MTX6            0x54
#define REG_BRIGHT          0x55    /*!< Brightness control (sign-magnitude) */
#define REG_CONTRAS         0x56    /*!< Contrast control */
#define REG_CONTRAS_CENTER  0x57    /*!< Contrast centre */
#define REG_MTXS            0x58    /*!< Matrix coefficient sign */
#define REG_AWBC7           0x59    /*!< AWB control 7..12 (reserved / magic) */
#define REG_AWBC8           0x5A
#define REG_AWBC9           0x5B
#define REG_AWBC10          0x5C
#define REG_AWBC11          0x5D
#define REG_AWBC12          0x5E
#define REG_BLKPIX_LVL      0x5F
#define REG_MANU            0x67    /*!< Manual U value (fixed-UV special effects) */
#define REG_MANV            0x68    /*!< Manual V value (fixed-UV special effects) */
#define REG_GFIX            0x69    /*!< Fix gain control */
#define REG_GGAIN           0x6A    /*!< G channel AWB gain */
#define REG_DBLV            0x6B    /*!< PLL control / regulator bypass */
#define REG_AWBCTR3         0x6C    /*!< AWB control 3 */
#define REG_AWBCTR2         0x6D
#define REG_AWBCTR1         0x6E
#define REG_AWBCTR0         0x6F
#define REG_SCALING_XSC     0x70    /*!< Horizontal scale factor / test pattern bit 7 */
#define REG_SCALING_YSC     0x71    /*!< Vertical scale factor / test pattern bit 7 */
#define REG_SCALING_DCWCTR  0x72    /*!< DCW down-sample control */
#define REG_SCALING_PCLK_DIV 0x73   /*!< Clock divider for DCW/scaling */
#define REG_REG74           0x74    /*!< Digital gain control */
#define REG_REG76           0x76    /*!< Pixel correction / white-black pixel */
#define REG_SLOP            0x7A    /*!< Gamma curve highest segment slope */
#define REG_GAM_BASE        0x7B    /*!< Gamma curve GAM1..GAM15 -> 0x7B..0x89 */
#define OV7670_GAMMA_LEN    15
#define REG_RGB444          0x8C    /*!< RGB444 control */
#define REG_DM_LNL          0x92    /*!< Dummy line low 8 bits */
#define REG_DM_LNH          0x93    /*!< Dummy line high 8 bits */
#define REG_HAECC1          0x9F    /*!< Histogram AEC/AGC control 1..7 */
#define REG_HAECC2          0xA0
#define REG_SCALING_PCLK_DELAY 0xA2 /*!< Pixel clock delay */
#define REG_BD50MAX         0xA5    /*!< 50 Hz banding step limit */
#define REG_HAECC3          0xA6
#define REG_HAECC4          0xA7
#define REG_HAECC5          0xA8
#define REG_HAECC6          0xA9
#define REG_HAECC7          0xAA
#define REG_BD60MAX         0xAB    /*!< 60 Hz banding step limit */

/* Sentinel used to terminate register tables. 0xFF is not a writable
 * register on the OV7670, so it is unambiguous as an end marker. */
#define OV7670_REG_TABLE_END        0xFF
#define OV7670_REG_TABLE_END_VAL    0xFF
/* Pseudo-register: value is a delay in milliseconds rather than a write. */
#define OV7670_REG_DELAY            0xFE

/* -------------------------------------------------------------------------
 * Bit definitions
 * ---------------------------------------------------------------------- */

/* COM1 (0x04) */
#define COM1_CCIR656        0x40    /*!< CCIR656 format enable */

/* COM2 (0x09) */
#define COM2_SSLEEP         0x10    /*!< Soft sleep mode */
#define COM2_DRIVE_1X       0x00    /*!< Output drive capability 1x */
#define COM2_DRIVE_2X       0x01
#define COM2_DRIVE_3X       0x02
#define COM2_DRIVE_4X       0x03

/* COM3 (0x0C) */
#define COM3_SWAP           0x40    /*!< Byte swap */
#define COM3_TRI_CLK        0x20    /*!< Tri-state PCLK during vertical blank */
#define COM3_TRI_DATA       0x10    /*!< Tri-state data bus during vertical blank */
#define COM3_SCALEEN        0x08    /*!< Enable scaling */
#define COM3_DCWEN          0x04    /*!< Enable down-sample / crop / window */

/* COM6 (0x0F) */
#define COM6_RESET_TIMING   0x02    /*!< Reset all timing when format changes */

/* CLKRC (0x11) - internal clock = XCLK / ((CLKRC[5:0] + 1) * 2) unless bit 6 */
#define CLKRC_EXT_CLK       0x40    /*!< Use external clock directly, no prescale */
#define CLKRC_RSVD          0x80    /*!< Reserved, datasheet says write 1 */
#define CLKRC_SCALE(n)      ((n) & 0x3F)

/* COM7 (0x12) */
#define COM7_RESET          0x80    /*!< SCCB register reset (self-clearing) */
#define COM7_FMT_MASK       0x38
#define COM7_FMT_VGA        0x00
#define COM7_FMT_CIF        0x20
#define COM7_FMT_QVGA       0x10
#define COM7_FMT_QCIF       0x08
#define COM7_RGB            0x04    /*!< Output format: RGB (qualified by COM15) */
#define COM7_YUV            0x00    /*!< Output format: YUV */
#define COM7_BAYER          0x01    /*!< Output format: Bayer RAW */
#define COM7_PBAYER         0x05    /*!< Output format: processed Bayer RAW */
#define COM7_COLOR_BAR      0x02    /*!< Colour bar test pattern enable */

/* COM8 (0x13) */
#define COM8_FASTAEC        0x80    /*!< Enable fast AGC/AEC algorithm */
#define COM8_AECSTEP        0x40    /*!< Unlimited AEC step size */
#define COM8_BFILT          0x20    /*!< Band filter enable */
#define COM8_AGC            0x04    /*!< Auto gain control enable */
#define COM8_AWB            0x02    /*!< Auto white balance enable */
#define COM8_AEC            0x01    /*!< Auto exposure control enable */

/* COM9 (0x14) - AGC gain ceiling in bits [6:4]; bit 3 is reserved-must-be-1 */
#define COM9_AGC_GAIN_2X    0x00
#define COM9_AGC_GAIN_4X    0x10
#define COM9_AGC_GAIN_8X    0x20
#define COM9_AGC_GAIN_16X   0x30
#define COM9_AGC_GAIN_32X   0x40
#define COM9_AGC_GAIN_64X   0x50
#define COM9_AGC_GAIN_128X  0x60
#define COM9_FREEZE_AGC_AEC 0x01

/* COM10 (0x15) */
#define COM10_HSYNC         0x40    /*!< HREF pin outputs HSYNC instead of HREF */
#define COM10_PCLK_HB       0x20    /*!< Suppress PCLK during horizontal blank */
#define COM10_HREF_REV      0x08    /*!< Reverse HREF polarity */
#define COM10_VS_LEAD       0x04    /*!< VSYNC changes on falling edge of PCLK */
#define COM10_VS_NEG        0x02    /*!< VSYNC negative (active low) */
#define COM10_HS_NEG        0x01    /*!< HSYNC negative */

/* MVFP (0x1E) */
#define MVFP_MIRROR         0x20    /*!< Mirror image horizontally */
#define MVFP_FLIP           0x10    /*!< Vertical flip */
#define MVFP_BLACK_SUN      0x04    /*!< Black sun enable */

/* TSLB (0x3A) */
#define TSLB_YLAST          0x04    /*!< UYVY / VYUY output order select */
#define TSLB_AUTO_OUTWIN    0x01    /*!< Auto output window when format changes */
#define TSLB_FIXED_UV       0x10    /*!< Use fixed UV from MANU/MANV (effects) */

/* COM11 (0x3B) */
#define COM11_NIGHT         0x80    /*!< Night mode enable */
#define COM11_NMFR_MASK     0x60    /*!< Night mode minimum frame rate divisor */
#define COM11_NMFR_DIV2     0x20
#define COM11_NMFR_DIV4     0x40
#define COM11_NMFR_DIV8     0x60
#define COM11_HZAUTO        0x10    /*!< Auto-detect 50/60 Hz banding */
#define COM11_50HZ          0x08    /*!< Manual banding filter: 1 = 50 Hz */
#define COM11_EXP           0x02    /*!< Exposure timing may be less than limit */

/* COM12 (0x3C) */
#define COM12_HREF_ALWAYS   0x80    /*!< Always output HREF, even in vertical blank */

/* COM13 (0x3D) */
#define COM13_GAMMA         0x80    /*!< Gamma enable */
#define COM13_UVSAT         0x40    /*!< UV saturation auto-adjust */
#define COM13_UVSWAP        0x01    /*!< UV channel swap */

/* COM14 (0x3E) */
#define COM14_DCWEN         0x10    /*!< DCW and scaling PCLK enable */
#define COM14_MANUAL_SCALE  0x08    /*!< Manual scaling enable */
#define COM14_PCLK_DIV_MASK 0x07    /*!< PCLK divider: output = input >> n */

/* COM15 (0x40) */
#define COM15_R10F0         0x00    /*!< Output range [16..240] */
#define COM15_R01FE         0x80    /*!< Output range [ 1..254] */
#define COM15_R00FF         0xC0    /*!< Output range [ 0..255] */
#define COM15_RGB_MASK      0x30
#define COM15_RGB_NORMAL    0x00    /*!< Normal RGB output (with COM7_RGB) */
#define COM15_RGB565        0x10
#define COM15_RGB555        0x30

/* COM16 (0x41) */
#define COM16_AWBGAIN       0x08    /*!< AWB gain enable */
#define COM16_DENOISE_EN    0x10    /*!< De-noise enable */
#define COM16_EDGE_EN       0x20    /*!< Edge enhancement enable */

/* COM17 (0x42) */
#define COM17_AECWIN_MASK   0xC0    /*!< AEC window must match COM7 resolution */
#define COM17_DSP_COLOR_BAR 0x08    /*!< DSP colour bar enable */

/* RGB444 (0x8C) */
#define R444_ENABLE         0x02    /*!< RGB444 output enable */
#define R444_RGBX           0x01    /*!< 1 = xxxxRRRR GGGGBBBB, 0 = RRRRGGGG BBBBxxxx */

/* MTXS (0x58) - bit N set means colour matrix coefficient N is negative */
#define MTXS_AUTO_CONTRAST  0x80    /*!< Auto contrast centre enable */

/* SCALING_DCWCTR (0x72): (vertical << 4) | horizontal, 0=/1, 1=/2, 2=/4, 3=/8 */
#define DCWCTR_DOWN(h, v)   ((((v) & 0x07) << 4) | ((h) & 0x07))
#define DCWCTR_HAVG         0x08    /*!< Horizontal average enable */
#define DCWCTR_VAVG         0x80    /*!< Vertical average enable */

/* SCALING_PCLK_DIV (0x73): 0xF0 | log2(divider) */
#define PCLK_DIV(n)         (0xF0 | ((n) & 0x0F))

/**
 * @brief One entry in a register initialisation table.
 *
 * A table is terminated by {OV7670_REG_TABLE_END, OV7670_REG_TABLE_END_VAL}.
 * An entry whose reg is ::OV7670_REG_DELAY is not written to the sensor;
 * instead val is interpreted as a delay in milliseconds.
 */
typedef struct {
    uint8_t reg;
    uint8_t val;
} ov7670_reg_entry_t;
