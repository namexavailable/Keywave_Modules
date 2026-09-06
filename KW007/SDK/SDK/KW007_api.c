/**
 * ============================================================
 *  KW007_api.c — User API Implementation
 *  Keywave KW007 5.8GHz Doppler Radar Sensor
 *  KW007_SDK_v1.2
 *  Released: 2026-08-24
 *
 *  Copyright 2026 Keywave Technology
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0; see LICENSE.
 *
 *  High-level functions for application developers.
 *  Register-level operations are in kw007_reg.c.
 * ============================================================
 */

#include <string.h>
#include "KW007_api.h"
#include "kw007_reg.h"

/* ─── KW007_Init ─────────────────────────────────────────────
 * Official 4-step startup sequence:
 *   1. Write R09 = 0x10  (fuse mode — enables R01 VCO bank readout)
 *   2. Read  R01[5:0]    (factory-burned VCO bank)
 *   3. Set   R12[5:0]    = VCO bank value in init array
 *   4. Write R00-R30     with R09 = 0x30 (I2C manual mode)
 */
uint8_t KW007_Init(void)
{
    uint8_t vcoBank = 0;
    uint8_t status  = KW007_OK;
    uint8_t regs[KW007_REG_COUNT];

    /* Step 1: clear R09[5] so R01 returns the burned VCO bank. Derived from
     * the default table rather than written as a literal, so the two cannot
     * drift apart. */
    status = kw007_reg_write(REG_MANUAL_MODE,
                             kw007_default_regs[REG_MANUAL_MODE] & ~MASK_EFUSE_MODE);
    if (status != KW007_OK) return status;

    /* Step 2: read factory VCO bank */
    status = kw007_reg_read(REG_VCO_FUSE, &vcoBank);
    if (status != KW007_OK) return status;
    vcoBank &= MASK_VCO_BANK;

    /* Step 3: patch VCO bank into register array
     * R12 layout:
     *   bit[7]    = Sampling Rate (keep default)
     *   bit[6]    = keep set
     *   bits[5:0] = VCO bank
     */
    memcpy(regs, kw007_default_regs, KW007_REG_COUNT);
    regs[REG_VCO_CTRL] = (kw007_default_regs[REG_VCO_CTRL] & MASK_SAMPLE_RATE)
                       | MASK_RESERVED_R12 | vcoBank;

    /* Step 4: write R00-R30; R09 already has bit[5] set in the table */
    return kw007_reg_write_block(0x00, regs, KW007_REG_COUNT);
}

/* ─── KW007_Read ─────────────────────────────────────────── */
KW007_Data KW007_Read(void)
{
    KW007_Data data;
    uint8_t flags = 0, rssi = 0;

    memset(&data, 0, sizeof(data));
    data.ok = false;

    if (kw007_reg_read(REG_FLAGS, &flags)) return data;
    if (kw007_reg_read(REG_RSSI,  &rssi))  return data;

    data.ok          = true;
    data.approaching = (flags & FLAG_APPROACHING) != 0;
    data.leaving     = (flags & FLAG_LEAVING)     != 0;
    data.isDay       = (flags & FLAG_DAY_NIGHT)   != 0;
    data.rssi        = rssi;

    return data;
}

/* ─── KW007_SetDetectionRange ────────────────────────────── */
uint8_t KW007_SetDetectionRange(uint8_t range)
{
    typedef struct { uint8_t a1, a2, a3, th; bool enhanced; } Preset;
    static const Preset presets[9] = {
        {0, 0, 0,  0, false},  /* 0: unused           */
        {3, 0, 0, 45, false},  /* 1: ~15 cm (27 dB) */
        {5, 0, 0, 64, false},  /* 2: ~50 cm (32 dB) */
        {7, 1, 0, 90, false},  /* 3:  ~1 m  (44 dB) */
        {7, 4, 0, 90, false},  /* 4:  ~3 m  (53 dB) */
        {7, 7, 0, 90, false},  /* 5:  ~5 m  (64 dB) */
        {7, 7, 1, 64, false},  /* 6:  ~7 m  (68 dB) */
        {7, 7, 3, 32, false},  /* 7: ~10 m  (76 dB) */
        {7, 7, 3, 32, true },  /* 8: ~13 m  (76 dB + enhanced) */
    };
    if (range < 1 || range > 8) return KW007_ERR_PARAM;
    const Preset *p = &presets[range];
    uint8_t err;
    if ((err = KW007_SetGain(p->a1, p->a2, p->a3))) return err;
    if ((err = KW007_SetThreshold(p->th)))            return err;
    if ((err = KW007_SetEnhancedMode(p->enhanced)))   return err;
    return KW007_OK;
}

/* ─── KW007_SetGain ─────────────────────────────────────── */
uint8_t KW007_SetGain(uint8_t amp1, uint8_t amp2, uint8_t amp3)
{
    uint8_t r19 = 0, r20 = 0, status = KW007_OK;

    /* Out of range is an error, not something to mask away: masking turns
     * amp1 = 9 into 1 and reports success. */
    if (amp1 > 7 || amp2 > 7 || amp3 > 3) return KW007_ERR_PARAM;

    /* Checked one at a time. OR-ing the codes together would be wrong as
     * well as unclear - KW007_ERR_I2C | KW007_ERR_NO_DEVICE is the value of
     * KW007_ERR_PARAM. */
    status = kw007_reg_read(REG_AMP1_GAIN, &r19);
    if (status != KW007_OK) return status;
    status = kw007_reg_read(REG_AMP2_AMP3_GAIN, &r20);
    if (status != KW007_OK) return status;

    r19 = (r19 & ~MASK_AMP1) | (amp1 << 5);
    r20 = (r20 & ~(MASK_AMP2 | MASK_AMP3)) | (amp3 << 3) | amp2;

    status = kw007_reg_write(REG_AMP1_GAIN, r19);
    if (status != KW007_OK) return status;
    return kw007_reg_write(REG_AMP2_AMP3_GAIN, r20);
}

/* ─── KW007_SetThreshold ─────────────────────────────────── */
uint8_t KW007_SetThreshold(uint8_t threshold)
{
    return kw007_reg_write(REG_THRESHOLD, threshold);
}

/* ─── KW007_SetEnhancedMode ──────────────────────────────── */
uint8_t KW007_SetEnhancedMode(bool enable)
{
    return kw007_reg_rmw(REG_ENHANCED_MODE,
                         MASK_ENHANCED,
                         enable ? MASK_ENHANCED : 0);
}

/* ─── KW007_SetGPO_DelayDuration ─────────────────────────────── */
uint8_t KW007_SetGPO_DelayDuration(KW007_DelayDuration delay)
{
    /* A C enum accepts any int, so the range is checked rather than masked */
    if ((uint8_t)delay > DELAY_SETTING4) return KW007_ERR_PARAM;

    return kw007_reg_rmw(REG_GPO_DELAY,
                         MASK_GPO_DELAY,
                         (uint8_t)((uint8_t)delay << 1));
}

/* ─── KW007_SetSlowSpeedDetection ────────────────────────── */
uint8_t KW007_SetSlowSpeedDetection(bool enable)
{
    /* bit[5]=0 = slow detect ON; bit[5]=1 = slow detect OFF */
    return kw007_reg_rmw(REG_SLOW_SPEED,
                         MASK_SLOW_DET,
                         enable ? 0 : MASK_SLOW_DET);
}

/* ─── KW007_SetFastSpeedDetection ───────────────────────── */
uint8_t KW007_SetFastSpeedDetection(bool enable)
{
    return kw007_reg_rmw(REG_GPO_DELAY,
                         MASK_FAST_SPEED_DET,
                         enable ? MASK_FAST_SPEED_DET : 0);
}

/* ─── KW007_SetSamplingRate ──────────────────────────────── */
uint8_t KW007_SetSamplingRate(bool use500Hz)
{
    return kw007_reg_rmw(REG_VCO_CTRL,
                         MASK_SAMPLE_RATE,
                         use500Hz ? MASK_SAMPLE_RATE : 0);
}

