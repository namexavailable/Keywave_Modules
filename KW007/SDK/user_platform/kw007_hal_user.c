/**
 * ============================================================
 *  kw007_hal_user.c — HAL Template for Custom Platforms
 *  Keywave KW007 5.8GHz Doppler Radar Sensor
 *  KW007_SDK_v1.2
 *  Released: 2026-08-24
 *
 *  Copyright 2026 Keywave Technology
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0; see LICENSE.
 *
 *  This file is a blank implementation template.
 *  Fill in the four functions below to port the SDK to your
 *  platform.  No other files need to be modified.
 *
 *  Steps:
 *    1. Rename this file to match your platform
 *       e.g. kw007_hal_stm32.c / kw007_hal_nrf52.c
 *    2. Implement the four functions using your platform's I2C
 *       and delay APIs.
 *    3. Add the file to your project alongside KW007_api.h and
 *       KW007_api.c.
 *    4. Call KW007_Init() before using any other API function.
 *
 *  Reference implementations (same function signatures):
 *    kw007_hal_arduino.cpp — Arduino Wire / ESP32
 *    kw007_hal_stm32.c     — STM32 HAL (HAL_I2C_Mem_Write/Read)
 *
 *  I2C address note:
 *    devAddr is the first byte on the bus (0x32); the 7-bit
 *    address is 0x19.
 *    - API takes the first byte (STM32 HAL, bare-metal): as-is.
 *    - API takes the 7-bit address (Wire, i2c-dev): devAddr >> 1.
 *    A read uses 0x33; derive it here.
 *
 *  Return codes (defined in kw007_hal.h):
 *    KW007_OK            0x00  success
 *    KW007_ERR_I2C       0x10  I2C bus error
 *    KW007_ERR_NO_DEVICE 0x20  device not found / not ready
 *    KW007_ERR_PARAM     0x30  invalid parameter
 * ============================================================
 */

#include "kw007_hal.h"

/* ─── PLATFORM INCLUDES ──────────────────────────────────────
 * Add your platform-specific headers here.
 * Examples:
 *   #include "i2c_driver.h"
 *   #include "delay.h"
 * ──────────────────────────────────────────────────────────── */

/* TODO: add your platform includes */


/* ─── KW007_HAL_WriteReg ─────────────────────────────────────
 * Write a single byte to a sensor register over I2C.
 *
 * devAddr : write frame byte, KW007_I2C_ADDRESS = 0x32 (address 0x19)
 * reg     : register address (0x00 – 0x1F)
 * data    : value to write
 *
 * Returns : KW007_OK on success, KW007_ERR_I2C on failure.
 * ──────────────────────────────────────────────────────────── */
uint8_t KW007_HAL_WriteReg(uint8_t devAddr, uint8_t reg, uint8_t data)
{
    /* TODO: implement single-register write
     *
     * Pseudo-code:
     *   i2c_start();
     *   i2c_write(devAddr);   // or devAddr >> 1 for 7-bit APIs
     *   i2c_write(reg);
     *   i2c_write(data);
     *   i2c_stop();
     *   return success ? KW007_OK : KW007_ERR_I2C;
     */
    (void)devAddr;
    (void)reg;
    (void)data;
    return KW007_ERR_I2C;
}

/* ─── KW007_HAL_ReadReg ──────────────────────────────────────
 * Read a single byte from a sensor register over I2C.
 *
 * devAddr : write frame byte, KW007_I2C_ADDRESS = 0x32 (address 0x19)
 * reg     : register address (0x00 – 0x1F)
 * data    : pointer to store the result (must not be NULL)
 *
 * Returns : KW007_OK on success, KW007_ERR_I2C on failure.
 *
 * Protocol (standard I2C register read):
 *   1. Send START + write address + register address (repeated start)
 *   2. Send START + read address
 *   3. Read 1 byte, send NAK + STOP
 * ──────────────────────────────────────────────────────────── */
uint8_t KW007_HAL_ReadReg(uint8_t devAddr, uint8_t reg, uint8_t *data)
{
    /* TODO: implement single-register read
     *
     * Pseudo-code:
     *   i2c_start();
     *   i2c_write(devAddr);   // write phase
     *   i2c_write(reg);
     *   i2c_restart();
     *   i2c_write(devAddr | 0x01);   // read phase
     *   *data = i2c_read_nak();
     *   i2c_stop();
     *   return success ? KW007_OK : KW007_ERR_I2C;
     */
    (void)devAddr;
    (void)reg;
    (void)data;
    return KW007_ERR_I2C;
}

/* ─── KW007_HAL_WriteBlock ───────────────────────────────────
 * Write consecutive registers in a single I2C transaction
 * (burst write).  Used during KW007_Init() to load the full
 * register table (31 bytes, R00–R30) in one transaction.
 *
 * devAddr  : write frame byte, KW007_I2C_ADDRESS = 0x32 (address 0x19)
 * startReg : first register address
 * data     : pointer to data array (len bytes)
 * len      : number of bytes to write
 *
 * Returns  : KW007_OK on success, KW007_ERR_I2C on failure.
 *
 * Note: the KW007 chip auto-increments the register address
 * after each byte — no special command needed.
 * ──────────────────────────────────────────────────────────── */
uint8_t KW007_HAL_WriteBlock(uint8_t devAddr, uint8_t startReg,
                              uint8_t *data, uint8_t len)
{
    /* TODO: implement burst write
     *
     * Pseudo-code:
     *   i2c_start();
     *   i2c_write(devAddr);
     *   i2c_write(startReg);
     *   for (i = 0; i < len; i++) i2c_write(data[i]);
     *   i2c_stop();
     *   return success ? KW007_OK : KW007_ERR_I2C;
     *
     * If your platform does not support burst write, you may
     * fall back to calling KW007_HAL_WriteReg() in a loop:
     *
     *   for (uint8_t i = 0; i < len; i++) {
     *       if (KW007_HAL_WriteReg(devAddr, startReg + i, data[i]))
     *           return KW007_ERR_I2C;
     *   }
     *   return KW007_OK;
     */
    (void)devAddr;
    (void)startReg;
    (void)data;
    (void)len;
    return KW007_ERR_I2C;
}

/* ─── KW007_HAL_DelayMs ──────────────────────────────────────
 * Block for the specified number of milliseconds.
 * Used during KW007_Init() for startup timing.
 *
 * ms : delay duration in milliseconds
 * ──────────────────────────────────────────────────────────── */
void KW007_HAL_DelayMs(uint32_t ms)
{
    /* TODO: implement millisecond delay
     *
     * Examples:
     *   Arduino  : delay(ms);
     *   STM32    : HAL_Delay(ms);
     *   FreeRTOS : vTaskDelay(pdMS_TO_TICKS(ms));
     *   Bare MCU : spin loop based on CPU clock
     */
    (void)ms;
}
