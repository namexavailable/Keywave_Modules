/**
 * ============================================================
 *  kw007_hal.h — Hardware Abstraction Layer Interface
 *  Keywave KW007 5.8GHz Doppler Radar Sensor
 *  KW007_SDK_v1.2
 *  Released: 2026-08-24
 *
 *  Copyright 2026 Keywave Technology
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0; see LICENSE.
 *
 *  The user must provide a platform-specific implementation
 *  of these functions in a separate kw007_hal_xxx.c file.
 *
 *  Example implementations:
 *    User_platform/kw007_hal_arduino.cpp  — Arduino / ESP32
 *    User_platform/kw007_hal_stm32.c      — STM32 (STM32 HAL)
 *    User_platform/kw007_hal_ftdi.cpp     — Windows / FTDI (C++ Builder)
 *    User_platform/kw007_hal_user.c       — template for custom platforms
 *
 *  I2C ADDRESS
 *    devAddr is the first byte on the bus, KW007_I2C_ADDRESS = 0x32.
 *    The 7-bit address is 0x19 — shift devAddr right by one if your
 *    I2C API expects that form. A read uses 0x33, derived here.
 * ============================================================
 */

#ifndef KW007_HAL_H
#define KW007_HAL_H

#include <stdint.h>

/* ─── RETURN CODES ─────────────────────────────────────────── */
#define KW007_OK            0x00
#define KW007_ERR_I2C       0x10
#define KW007_ERR_NO_DEVICE 0x20
#define KW007_ERR_PARAM     0x30
#define KW007_ERR_VERIFY    0x40   /* read-back did not match (v1.2)     */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Write a single register.
 * devAddr : first byte on bus, 0x32 (7-bit address 0x19)
 * reg     : register address
 * data    : value to write
 * returns : 0 = success, non-zero = error
 */
uint8_t KW007_HAL_WriteReg(uint8_t devAddr, uint8_t reg, uint8_t data);

/**
 * Read a single register.
 * devAddr : first byte on bus, 0x32; the read form 0x33 is derived here
 * reg     : register address
 * data    : pointer to store result
 * returns : 0 = success, non-zero = error
 */
uint8_t KW007_HAL_ReadReg(uint8_t devAddr, uint8_t reg, uint8_t *data);

/**
 * Write consecutive registers (burst write).
 * devAddr  : first byte on bus, 0x32 (7-bit address 0x19)
 * startReg : first register address
 * data     : data array
 * len      : number of bytes to write
 * returns  : 0 = success, non-zero = error
 */
uint8_t KW007_HAL_WriteBlock(uint8_t devAddr, uint8_t startReg, uint8_t *data, uint8_t len);

/**
 * Delay for the specified number of milliseconds.
 */
void KW007_HAL_DelayMs(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* KW007_HAL_H */
