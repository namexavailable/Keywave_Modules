/**
 * ============================================================
 *  kw007_reg.c — Register-Level Implementation
 *  Keywave KW007 5.8GHz Doppler Radar Sensor
 *  KW007_SDK_v1.2
 *  Released: 2026-08-24
 *
 *  Copyright 2026 Keywave Technology
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0; see LICENSE.
 *
 *  Low-level I2C register operations and default register table.
 *  Used internally by KW007_api.c and directly by engineers.
 * ============================================================
 */

#include "kw007_reg.h"

/* ─── DEFAULT REGISTER VALUES (R00-R30) ─────────────────────
 * R00-R07: read-only, written as 0x00 (ignored by chip)
 * R08-R30: writable
 * R31 is not written
 */
const uint8_t kw007_default_regs[KW007_REG_COUNT] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* R00-R07 */
    0xFC, 0x30, 0x88, 0x03, 0x54, 0x4B, 0x68, 0x55,  /* R08-R15 */
    0x98, 0xA0, 0x00, 0xEA, 0xF7, 0x5F, 0x02, 0x30,  /* R16-R23 */
    0xA0, 0x20, 0x9A, 0x30, 0x55, 0x75, 0x42         /* R24-R30 */
};

/* ─── REGISTER HELPERS ──────────────────────────────────────── */

uint8_t kw007_reg_write(uint8_t reg, uint8_t value)
{
    return KW007_HAL_WriteReg(KW007_I2C_ADDRESS, reg, value);
}

uint8_t kw007_reg_read(uint8_t reg, uint8_t *value)
{
    return KW007_HAL_ReadReg(KW007_I2C_ADDRESS, reg, value);
}

uint8_t kw007_reg_write_block(uint8_t startReg, uint8_t *data, uint8_t len)
{
    return KW007_HAL_WriteBlock(KW007_I2C_ADDRESS, startReg, data, len);
}

/* The HAL's status is returned unchanged rather than collapsed into
 * KW007_ERR_I2C: a HAL that distinguishes "no device" from "bus error" is
 * the only thing that can, and flattening it here threw that away. */
uint8_t kw007_reg_rmw(uint8_t reg, uint8_t clearMask, uint8_t setBits)
{
    uint8_t val = 0;
    uint8_t status = kw007_reg_read(reg, &val);
    if (status != KW007_OK) return status;

    val &= ~clearMask;
    val |= setBits;
    return kw007_reg_write(reg, val);
}

/* ─── KW007_WriteReg / KW007_ReadReg ─────────────────────── */
uint8_t KW007_WriteReg(uint8_t reg, uint8_t value)
{
    return kw007_reg_write(reg, value);
}

uint8_t KW007_ReadReg(uint8_t reg, uint8_t *value)
{
    return kw007_reg_read(reg, value);
}

uint8_t KW007_VerifyReg(uint8_t reg, uint8_t mask, uint8_t expected)
{
    uint8_t val = 0;
    uint8_t status = kw007_reg_read(reg, &val);
    if (status != KW007_OK) return status;

    return ((val & mask) == (expected & mask)) ? KW007_OK : KW007_ERR_VERIFY;
}
