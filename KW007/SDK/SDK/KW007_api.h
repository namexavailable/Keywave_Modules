/**
 * ============================================================
 *  KW007_api.h — User API
 *  Keywave KW007 5.8GHz Doppler Radar Sensor
 *  KW007_SDK_v1.2
 *  Released: 2026-08-24
 *
 *  Copyright 2026 Keywave Technology
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0; see LICENSE.
 *
 *  High-level functions for application developers.
 *  No register knowledge required.
 *
 *  For direct register access or fine-grained gain control,
 *  include kw007_reg.h additionally.
 *
 *  Supported platforms (provide HAL implementation):
 *    ESP32 / ESP32-S3 / ESP32-C3  — kw007_hal_arduino.cpp
 *    Arduino AVR / SAMD / nRF52   — kw007_hal_arduino.cpp
 *    STM32 (HAL / LL)             — kw007_hal_stm32.c
 *    Windows + FTDI               — kw007_hal_ftdi.cpp
 *    Any MCU with I2C             — kw007_hal_user.c (template)
 *
 *  HARDWARE REQUIREMENT (I2C / software control mode):
 *    The Range1 pin MUST be tied to GND through a protection resistor
 *    before power-up.
 *    This switches the sensor into I2C-controlled mode; otherwise
 *    the chip runs in hardware (pin-select) mode and ignores
 *    register writes.
 *
 *    Use either the 3.3 V or the 5 V input, never both, and never
 *    apply 5 V to the 3.3 V input - doing so will damage the module.
 *
 *  BASIC USAGE:
 *    KW007_Init();
 *    KW007_SetDetectionRange(4);
 *    KW007_Data data = KW007_Read();
 *    if (data.approaching) { ... }
 * ============================================================
 */

#ifndef KW007_API_H
#define KW007_API_H

#include <stdint.h>
#include <stdbool.h>
#include "kw007_hal.h"   /* provides KW007_OK / KW007_ERR_* return codes */

/* ─── SDK VERSION ─────────────────────────────────────────────
 * Readable by the preprocessor, so application code can adapt:
 *   #if KW007_SDK_VERSION >= KW007_SDK_VERSION_AT(1,2)
 * ───────────────────────────────────────────────────────────── */
#define KW007_SDK_VERSION_MAJOR   1
#define KW007_SDK_VERSION_MINOR   2
#define KW007_SDK_VERSION_STRING  "1.2"
#define KW007_SDK_VERSION_AT(ma, mi)  (((ma) << 8) | (mi))
#define KW007_SDK_VERSION \n        KW007_SDK_VERSION_AT(KW007_SDK_VERSION_MAJOR, KW007_SDK_VERSION_MINOR)

#ifdef __cplusplus
extern "C" {
#endif

/* ─── GPO DELAY DURATION ───────────────────────────────────── */
/**
 * GPO pin hold-on time after motion stops.
 * Encoded in R29 bits[2:1]. Actual duration depends on the sensor variant:
 *
 *   R29[2:1]    │ KW007-S │ KW007-L / KW007-SE
 *   ────────────┼─────────┼────────────────────
 *       00      │    4 s  │   16 s
 *       01      │    2 s  │   32 s
 *       10      │    1 s  │    1 s
 *       11      │   64 s  │   64 s
 */
typedef enum {
    DELAY_SETTING1 = 0,   /* 00 — S:4s   L/SE:16s  */
    DELAY_SETTING2 = 1,   /* 01 — S:2s   L/SE:32s  */
    DELAY_SETTING3 = 2,   /* 10 — S:1s   L/SE:1s   */
    DELAY_SETTING4 = 3    /* 11 — S:64s  L/SE:64s  */
} KW007_DelayDuration;

/* ─── DATA OUTPUT ──────────────────────────────────────────── */
typedef struct {
    bool    ok;           /* false = I2C read failed                  */
    bool    approaching;  /* object approaching                       */
    bool    leaving;      /* object leaving                           */
    bool    isDay;        /* 1=operating, 0=rest                      */
    uint8_t rssi;         /* signal strength 0-255                    */
} KW007_Data;

/* ─── API ──────────────────────────────────────────────────── */

/**
 * Initialize sensor. Runs the official startup sequence.
 *
 * @pre The Range1 pin MUST be tied to GND through a protection resistor
 *      before power-up to enable
 *      I2C / software control mode. If Range1 is left floating or
 *      pulled high, the sensor runs in hardware mode and all
 *      register writes will be silently ignored.
 *
 * @return KW007_OK            Success
 * @return KW007_ERR_I2C       I2C bus failure
 * @return KW007_ERR_NO_DEVICE Sensor did not ACK (check wiring,
 *                             Range1 pin, and power supply)
 */
uint8_t KW007_Init(void);

/**
 * Read sensor output. Sensor updates at ~8Hz; poll in main loop.
 * Returns data.ok = false on I2C failure.
 */
KW007_Data KW007_Read(void);

/**
 * Set detection range 1 (close) ~ 8 (far).
 * Sets Gain (amp1 / amp2 / amp3), Threshold, and Enhanced Mode atomically.
 *
 * Register mapping:
 *   amp1       → R19[7:5]   (REG_AMP1_GAIN)
 *   amp2       → R20[2:0]   (REG_AMP2_AMP3_GAIN)
 *   amp3       → R20[4:3]   (REG_AMP2_AMP3_GAIN)
 *   Threshold  → R27        (REG_THRESHOLD)
 *   Enhanced   → R24[6]     (REG_ENHANCED_MODE)
 *
 * Approximate detection distance (stable power supply, no obstruction,
 * human target):
 *
 *   Range │  amp1      amp2      amp3    │ Threshold │ Enhanced │ ~Distance
 *         │ R19[7:5]  R20[2:0]  R20[4:3] │    R27    │  R24[6]  │
 *   ──────┼─────────────────────────────┼───────────┼──────────┼──────────
 *     1   │    3         0         0    │    45     │   off    │  ~15 cm
 *     2   │    5         0         0    │    64     │   off    │  ~50 cm
 *     3   │    7         1         0    │    90     │   off    │   ~1 m
 *     4   │    7         4         0    │    90     │   off    │   ~3 m
 *     5   │    7         7         0    │    90     │   off    │   ~5 m
 *     6   │    7         7         1    │    64     │   off    │   ~7 m
 *     7   │    7         7         3    │    32     │   off    │  ~10 m
 *     8   │    7         7         3    │    32     │   on     │  ~13 m
 *
 * KW007-S:  max ~2 m; effective range ≈ KW007-L / 6.
 * KW007-SE: max ~4 m.
 *
 * Actual range varies with power-supply stability, physical obstruction,
 * target size, and motion speed. Unstable VDD or RF interference can
 * significantly reduce effective distance.
 */
uint8_t KW007_SetDetectionRange(uint8_t range);

/**
 * Set amplifier gains directly.
 *   amp1 : 0–7  → R19[7:5] (REG_AMP1_GAIN)
 *   amp2 : 0–7  → R20[2:0] (REG_AMP2_AMP3_GAIN)
 *   amp3 : 0–3  → R20[4:3] (REG_AMP2_AMP3_GAIN)
 *
 * @return KW007_ERR_PARAM if any value is out of range.
 */
uint8_t KW007_SetGain(uint8_t amp1, uint8_t amp2, uint8_t amp3);

/**
 * Set detection threshold. Higher value = less sensitive.
 *   threshold : 0–255  → R27[7:0] (REG_THRESHOLD)
 *
 * Choosing a value: the threshold is compared against the reflected signal
 * strength, which the KW007 GUI plots on the same 0-255 scale. With the
 * area empty the chart shows the noise floor; with a target moving at the
 * maximum required distance it shows the level the threshold must stay
 * under. Set it between the two. Changing the gain moves both readings, so
 * re-check afterwards. The Programming Guide has the full procedure.
 */
uint8_t KW007_SetThreshold(uint8_t threshold);

/**
 * Enable / disable enhanced range mode.
 *   enable : true = ON, false = OFF  → R24[6] (REG_ENHANCED_MODE)
 */
uint8_t KW007_SetEnhancedMode(bool enable);

/**
 * Set GPO output hold time after motion stops.
 *   delay : KW007_DelayDuration  → R29[2:1] (REG_GPO_DELAY, MASK_GPO_DELAY)
 *
 * Actual duration depends on the sensor variant — see @ref
 * KW007_DelayDuration for the values of each.
 */
uint8_t KW007_SetGPO_DelayDuration(KW007_DelayDuration delay);

/**
 * Enable / disable slow-speed motion detection.
 *   enable : true = ON, false = OFF  → R21[5] (REG_SLOW_SPEED)
 *
 * @note Hardware logic is inverted: bit[5]=0 enables slow detect,
 *       bit[5]=1 disables it. This API hides the inversion.
 */
uint8_t KW007_SetSlowSpeedDetection(bool enable);

/**
 * Enable / disable fast-speed motion detection.
 *   enable : true = ON, false = OFF (default)
 *            → R29[0] (REG_GPO_DELAY, MASK_FAST_SPEED_DET)
 */
uint8_t KW007_SetFastSpeedDetection(bool enable);

/**
 * Set system sampling rate.
 *   use500Hz : false = 1 kHz (default), true = 500 Hz
 *              → R12[7] (REG_VCO_CTRL, MASK_SAMPLE_RATE)
 */
uint8_t KW007_SetSamplingRate(bool use500Hz);

#ifdef __cplusplus
}
#endif

#endif /* KW007_API_H */
