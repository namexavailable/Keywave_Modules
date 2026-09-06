/**
 * ============================================================
 *  kw007_hal_arduino.cpp — HAL for Arduino / ESP32
 *  Keywave KW007 5.8GHz Doppler Radar Sensor
 *  KW007_SDK_v1.2
 *  Released: 2026-08-24
 *
 *  Copyright 2026 Keywave Technology
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0; see LICENSE.
 *
 *  Usage:
 *    1. Add this file together with KW007_api.h / KW007_api.c
 *    2. Call Wire.begin(sda, scl) in setup() to init I2C
 *    3. Call KW007_Init()
 *
 *  Common Wire.begin() pins:
 *    ESP32 original : Wire.begin(21, 22)
 *    ESP32-S3 Mini  : Wire.begin(8, 9)
 *    RP2040 Pico W  : Wire.begin(4, 5)
 *    STM32 Nucleo   : Wire.begin(PB7, PB6)
 *    SAMD21 Nano33  : Wire.begin()
 *    ESP8266 D1Mini : Wire.begin(4, 5)
 * ============================================================
 */

#include <Wire.h>
#include "kw007_hal.h"

extern "C" {

/* Wire takes the 7-bit address and adds the R/W bit itself, so shift
 * the first-byte form down: 0x32 >> 1 = 0x19. */
#define ADDR7(addr8)  ((addr8) >> 1)

uint8_t KW007_HAL_WriteReg(uint8_t devAddr, uint8_t reg, uint8_t data)
{
    Wire.beginTransmission(ADDR7(devAddr));
    Wire.write(reg);
    Wire.write(data);
    return Wire.endTransmission() == 0 ? KW007_OK : KW007_ERR_I2C;
}

uint8_t KW007_HAL_ReadReg(uint8_t devAddr, uint8_t reg, uint8_t *data)
{
    if (!data) return KW007_ERR_I2C;
    Wire.beginTransmission(ADDR7(devAddr));
    Wire.write(reg);
    if (Wire.endTransmission(false)) return KW007_ERR_I2C;
    Wire.requestFrom(ADDR7(devAddr), (uint8_t)1);
    if (Wire.available()) {
        *data = Wire.read();
        return KW007_OK;
    }
    return KW007_ERR_I2C;
}

uint8_t KW007_HAL_WriteBlock(uint8_t devAddr, uint8_t startReg, uint8_t *data, uint8_t len)
{
    Wire.beginTransmission(ADDR7(devAddr));
    Wire.write(startReg);
    Wire.write(data, len);
    return Wire.endTransmission() == 0 ? KW007_OK : KW007_ERR_I2C;
}

void KW007_HAL_DelayMs(uint32_t ms)
{
    delay(ms);
}

} /* extern "C" */
