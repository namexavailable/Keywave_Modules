/**
 * ============================================================
 *  kw007_hal_stm32.c — HAL for STM32 (STM32 HAL library)
 *  Keywave KW007 5.8GHz Doppler Radar Sensor
 *  KW007_SDK_v1.2
 *  Released: 2026-08-24
 *
 *  Copyright 2026 Keywave Technology
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0; see LICENSE.
 *
 *  Usage:
 *    1. Generate I2C peripheral with STM32CubeMX (e.g. hi2c1)
 *    2. Call KW007_HAL_SetI2C(&hi2c1) before KW007_Init()
 *    3. Call KW007_Init()
 *
 *  Example:
 *    KW007_HAL_SetI2C(&hi2c1);
 *    KW007_Init();
 *
 *  I2C address note:
 *    STM32 HAL Mem_Write/Mem_Read takes the first byte on the bus,
 *    so devAddr = 0x32 is passed through unchanged (address 0x19).
 * ============================================================
 */

#include "stm32xxxx_hal.h"   /* replace xxxx with your STM32 series, e.g. stm32f4xx_hal.h */
#include "kw007_hal.h"

#define KW007_HAL_TIMEOUT_MS   10

static I2C_HandleTypeDef *s_hi2c = NULL;

/**
 * Set the I2C handle to use. Must be called before KW007_Init().
 * Example: KW007_HAL_SetI2C(&hi2c1);
 */
void KW007_HAL_SetI2C(I2C_HandleTypeDef *hi2c)
{
    s_hi2c = hi2c;
}

uint8_t KW007_HAL_WriteReg(uint8_t devAddr, uint8_t reg, uint8_t data)
{
    if (!s_hi2c) return KW007_ERR_I2C;
    return (HAL_I2C_Mem_Write(s_hi2c,
                              devAddr,
                              reg,
                              I2C_MEMADD_SIZE_8BIT,
                              &data,
                              1,
                              KW007_HAL_TIMEOUT_MS) == HAL_OK) ? KW007_OK : KW007_ERR_I2C;
}

uint8_t KW007_HAL_ReadReg(uint8_t devAddr, uint8_t reg, uint8_t *data)
{
    if (!s_hi2c) return KW007_ERR_I2C;
    return (HAL_I2C_Mem_Read(s_hi2c,
                             devAddr,
                             reg,
                             I2C_MEMADD_SIZE_8BIT,
                             data,
                             1,
                             KW007_HAL_TIMEOUT_MS) == HAL_OK) ? KW007_OK : KW007_ERR_I2C;
}

uint8_t KW007_HAL_WriteBlock(uint8_t devAddr, uint8_t startReg, uint8_t *data, uint8_t len)
{
    if (!s_hi2c) return KW007_ERR_I2C;
    return (HAL_I2C_Mem_Write(s_hi2c,
                              devAddr,
                              startReg,
                              I2C_MEMADD_SIZE_8BIT,
                              data,
                              len,
                              KW007_HAL_TIMEOUT_MS) == HAL_OK) ? KW007_OK : KW007_ERR_I2C;
}

void KW007_HAL_DelayMs(uint32_t ms)
{
    HAL_Delay(ms);
}
