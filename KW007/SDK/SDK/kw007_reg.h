/**
 * ============================================================
 *  kw007_reg.h — Register-Level Interface
 *  Keywave KW007 5.8GHz Doppler Radar Sensor
 *  KW007_SDK_v1.2
 *  Released: 2026-08-24
 *
 *  Copyright 2026 Keywave Technology
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0; see LICENSE.
 *
 *  Low-level register definitions and I2C helpers.
 *  Most users should use KW007_api.h instead.
 *  Include this header for direct register access.
 *
 *  REGISTER MAP SUMMARY
 *  ─────────────────────────────────────────────────────────
 *  R01  REG_VCO_FUSE       bits[5:0] = factory VCO bank (read after R09=0x10)
 *  R05  REG_RSSI           signal strength (read-only)
 *  R06  REG_FLAGS          motion/day flags (read-only)
 *  R09  REG_MANUAL_MODE    bit[5]=I2C/fuse mode select
 *  R12  REG_VCO_CTRL       bit[7]=sample rate, bits[5:0]=VCO bank
 *  R19  REG_AMP1_GAIN      bits[7:5]=Gain1 (amp1)
 *  R20  REG_AMP2_AMP3_GAIN bits[2:0]=Gain2 (amp2), bits[4:3]=Gain3 (amp3)
 *  R21  REG_SLOW_SPEED     bit[5]=slow movement detection
 *  R24  REG_ENHANCED_MODE  bit[6]=enhanced range
 *  R27  REG_THRESHOLD      detection threshold 0-255
 *  R29  REG_GPO_DELAY      bits[2:1]=LightFlagDelay, bit[0]=fast speed detection
 * ============================================================
 */

#ifndef KW007_REG_H
#define KW007_REG_H

#include <stdint.h>
#include "kw007_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── I2C ADDRESS ─────────────────────────────────────────────────
 * The KW007 address is factory-fixed; one KW007 per bus.
 *
 *   7-bit target address   0x19          <- this is the address
 *   first byte on bus      0x32 write / 0x33 read
 *
 * Pass whichever form your I2C API expects:
 *   0x19   Arduino Wire, Linux i2c-dev, smbus, ESP-IDF, Zephyr
 *   0x32   STM32 HAL, bare-metal drivers, USB-I2C bridges
 *
 * Passing 0x32 to an API that expects 0x19 puts 0x64 on the bus and
 * every transfer is NACKed. See Programming Guide section 2.1.
 * ────────────────────────────────────────────────────────────── */
#define KW007_I2C_ADDRESS        0x32   /* first byte on bus, write */
#define KW007_I2C_ADDRESS_7BIT   0x19   /* 7-bit target address     */

/* ─── REGISTER COUNT ──────────────────────────────────────── */
#define KW007_REG_COUNT     31     /* R00-R30; R31 is not written */

/* ─── REGISTER ADDRESSES ───────────────────────────────────── */
#define REG_VCO_FUSE        0x01
#define REG_RSSI            0x05
#define REG_FLAGS           0x06
#define REG_MANUAL_MODE     0x09
#define REG_VCO_CTRL        0x0C
#define REG_AMP1_GAIN       0x13
#define REG_AMP2_AMP3_GAIN  0x14
#define REG_SLOW_SPEED      0x15
#define REG_ENHANCED_MODE   0x18
#define REG_THRESHOLD       0x1B
#define REG_GPO_DELAY       0x1D

/* ─── FLAG BITS (R06) ──────────────────────────────────────── */
#define FLAG_DAY_NIGHT      (1 << 3)   /* 1=day(operating), 0=night(rest) */
#define FLAG_APPROACHING    (1 << 2)   /* 1=approaching motion detected   */
#define FLAG_LEAVING        (1 << 1)   /* 1=leaving motion detected       */

/* ─── REGISTER BIT MASKS ───────────────────────────────────── */
/* R09 (REG_MANUAL_MODE) */
#define MASK_EFUSE_MODE     0x20   /* bit[5]    — 0=FUSE, 1=I2C           */
/* R12 (REG_VCO_CTRL) */
#define MASK_SAMPLE_RATE    0x80   /* bit[7]    — 0=1kHz, 1=500Hz         */
#define MASK_RESERVED_R12   0x40   /* bit[6]    — keep set                */
#define MASK_VCO_BANK       0x3F   /* bits[5:0] — VCO bank                */
/* R19 (REG_AMP1_GAIN) */
#define MASK_AMP1           0xE0   /* bits[7:5] — Amplifier 1 Gain        */
/* R20 (REG_AMP2_AMP3_GAIN) */
#define MASK_AMP2           0x07   /* bits[2:0] — Amplifier 2 Gain        */
#define MASK_AMP3           0x18   /* bits[4:3] — Amplifier 3 Gain        */
/* R21 (REG_SLOW_SPEED) */
#define MASK_SLOW_DET       0x20   /* bit[5]    — 0=slow ON, 1=slow OFF   */
/* R24 (REG_ENHANCED_MODE) */
#define MASK_ENHANCED       0x40   /* bit[6]    — enhanced range mode     */
/* R29 (REG_GPO_DELAY) */
#define MASK_FAST_SPEED_DET 0x01   /* bit[0]    — 0=fast speed OFF, 1=ON  */
#define MASK_GPO_DELAY      0x06   /* bits[2:1] — GPO Delay               */

/* ─── DEFAULT REGISTER TABLE ──────────────────────────────── */
extern const uint8_t kw007_default_regs[KW007_REG_COUNT];

/* ─── REGISTER HELPERS ────────────────────────────────────── */

/** Write single register via I2C */
uint8_t kw007_reg_write(uint8_t reg, uint8_t value);

/** Read single register via I2C */
uint8_t kw007_reg_read(uint8_t reg, uint8_t *value);

/** Write consecutive registers (burst) */
uint8_t kw007_reg_write_block(uint8_t startReg, uint8_t *data, uint8_t len);

/** Read-Modify-Write: clear clearMask bits, then set setBits */
uint8_t kw007_reg_rmw(uint8_t reg, uint8_t clearMask, uint8_t setBits);

/* ─── ENGINEER API ────────────────────────────────────────── */

/** Write register directly. */
uint8_t KW007_WriteReg(uint8_t reg, uint8_t value);

/** Read register directly. Result stored in *value. */
uint8_t KW007_ReadReg(uint8_t reg, uint8_t *value);

/**
 * Read a register back and compare the bits selected by mask.
 *
 * The configuration functions do not verify their own writes - a write that
 * the bus acknowledges is reported as success. Where a design needs to be
 * sure a setting took effect, call this afterwards with the same mask the
 * setting uses, for example:
 *
 *   KW007_SetThreshold(90);
 *   if (KW007_VerifyReg(REG_THRESHOLD, 0xFF, 90) != KW007_OK) { ... }
 *
 * @return KW007_OK          the selected bits match
 * @return KW007_ERR_VERIFY  they do not
 * @return other             the error reported by the HAL
 */
uint8_t KW007_VerifyReg(uint8_t reg, uint8_t mask, uint8_t expected);

#ifdef __cplusplus
}
#endif

#endif /* KW007_REG_H */
