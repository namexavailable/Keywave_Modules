/**
 * ============================================================
 *  kw007_hal_ftdi.cpp — HAL for Windows + FTDI USB-I2C
 *  Keywave KW007 5.8GHz Doppler Radar Sensor
 *  KW007_SDK_v1.2
 *  Released: 2026-08-24
 *
 *  Copyright 2026 Keywave Technology
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0; see LICENSE.
 *
 *  Uses the existing I2C_Sys2 driver (I2C_Sys2.h / I2C_Sys2.cpp).
 *  Intended for use in C++ Builder to evaluate the SDK.
 *
 *  Usage:
 *    1. Add KW007_api.h, KW007_api.c, and this file to the project
 *    2. Open FTDI device via the HAL before calling KW007_Init():
 *         KW007_HAL_FTDI_Open();
 *    3. Call KW007_Init()
 *    4. Close FTDI device when done:
 *         KW007_HAL_FTDI_Close();
 *
 *  I2C address note:
 *    I2C_Sys2 takes the first byte on the bus, so
 *    KW007_I2C_ADDRESS = 0x32 is passed through unchanged.
 * ============================================================
 */

#include <windows.h>
#include "../i2c/I2C_Sys2.h"
#include "kw007_hal.h"

/* ── FTDI open/close state ──────────────────────────────────
 * Tracked here so HAL functions can return KW007_ERR_NO_DEVICE
 * instead of crashing with FT_Write(NULL, ...) when the device
 * has not been opened yet.
 * ──────────────────────────────────────────────────────────── */
static bool s_ftdi_open = false;

/**
 * Open the FTDI USB-I2C device and initialise MPSSE.
 * Must be called before KW007_Init().
 * Returns true on success.
 */
bool KW007_HAL_FTDI_Open(void)
{
    s_ftdi_open = USB_I2C_Init_FTDI();
    return s_ftdi_open;
}

/**
 * Close the FTDI USB-I2C device.
 * Call this when the application is done (e.g. on form close).
 */
void KW007_HAL_FTDI_Close(void)
{
    USB_Close_I2C_FTDI();
    s_ftdi_open = false;
}

/* ── HAL implementation ─────────────────────────────────────── */
extern "C" {

uint8_t KW007_HAL_WriteReg(uint8_t devAddr, uint8_t reg, uint8_t data)
{
    if (!s_ftdi_open) return KW007_ERR_NO_DEVICE;
    return I2C_Write_Reg(devAddr, reg, data) ? KW007_OK : KW007_ERR_I2C;
}

uint8_t KW007_HAL_ReadReg(uint8_t devAddr, uint8_t reg, uint8_t *data)
{
    if (!s_ftdi_open) return KW007_ERR_NO_DEVICE;
    /* I2C_Sys2 places the result at buf[reg], not buf[0], so the
     * buffer must span the whole register range. */
    UINT8 buf[256] = {0};
    if (!I2C_pageRead_Reg(devAddr, reg, buf, 1))
        return KW007_ERR_I2C;
    *data = buf[reg];
    return KW007_OK;
}

uint8_t KW007_HAL_WriteBlock(uint8_t devAddr, uint8_t startReg, uint8_t *data, uint8_t len)
{
    if (!s_ftdi_open) return KW007_ERR_NO_DEVICE;
    /* I2C_Sys2 reads the source at src[startReg + i], so the payload
     * is staged at that offset - which means the offset plus the length
     * has to fit the buffer. */
    UINT8 buf[256] = {0};
    if ((UINT16)startReg + len > sizeof(buf))
        return KW007_ERR_PARAM;

    for (UINT8 i = 0; i < len; i++)
        buf[startReg + i] = data[i];
    return I2C_pageWrite_Reg(devAddr, startReg, buf, len) ? KW007_OK : KW007_ERR_I2C;
}

void KW007_HAL_DelayMs(uint32_t ms)
{
    Sleep(ms);
}

} /* extern "C" */
