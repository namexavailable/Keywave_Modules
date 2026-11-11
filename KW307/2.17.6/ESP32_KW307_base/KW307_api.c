/*
 * Copyright 2026 Keywave Technology Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * KW307 SDK - API, General - Implementation
 * FW: 2.11.0
 */

#include <stddef.h>     /* NULL */
#include "KW307_api.h"
#include "KW307_api_advanced.h"   /* the baseband attenuator a stationary level sets */
#include "KW307_cmd.h"
#include "KW307_uart.h"
#include "KW307_port.h"
#include "KW307_io.h"

/* ================================================================
   Sec.1  System CMDs (0x01 ~ 0x03 + 0x07 + 0x09)
   ================================================================ */

int kw307_radar_init(void)
{
    /* CMD 0x07 — re-init the radar. ~130 ms blocking on the MCU side. */
    uint8_t v = 0x01u;
    return kw307_send_cmd(0x07u, &v, 1);
}

int kw307_factory_reset(void)
{
    return kw307_cmd_factory_reset(kw307_default_uart());
}

int kw307_save_settings(void)
{
    return kw307_cmd_save_settings(kw307_default_uart());
}

int kw307_clear_state(void)
{
    /* CMD 0x09 — drops the current targets and presence flag. No settings and
     * no flash are touched. */
    return kw307_cmd_clear_state(kw307_default_uart());
}

static int fw_older_than_baseline(uint8_t maj, uint8_t min_, uint8_t patch)
{
    /* Compatible = MCU firmware >= the paired baseline (KW307_SDK_FW_*).
     * Compared field-by-field, highest priority first (MAJOR, then MINOR, then
     * PATCH). The first field that differs decides and short-circuits: a newer
     * field means a newer firmware, so the lower fields need not be checked. */
    if (maj  != (uint8_t)KW307_SDK_FW_MAJOR)
        return (maj  > (uint8_t)KW307_SDK_FW_MAJOR) ? 0 : 1;   /* MAJOR decides */
    if (min_ != (uint8_t)KW307_SDK_FW_MINOR)
        return (min_ > (uint8_t)KW307_SDK_FW_MINOR) ? 0 : 1;   /* MINOR decides */
    if (patch != (uint8_t)KW307_SDK_FW_PATCH)
        return (patch > (uint8_t)KW307_SDK_FW_PATCH) ? 0 : 1;  /* PATCH decides */
    return 0;       /* exactly the baseline version */
}

int kw307_check_fw_version(void)
{
    uint8_t maj = 0, min_ = 0, patch = 0;
    int old;
    int ret = kw307_cmd_read_fw_version(KW307_FW_CHECK_TIMEOUT_MS, &maj, &min_, &patch);
    if (ret != KW307_OK) return ret;   /* transport error — propagate */

    /* Remembered, so the calls this firmware lacks can say so instead of being
     * sent and silently ignored. */
    old = fw_older_than_baseline(maj, min_, patch);
    kw307_i_fw_set_old(old != 0);
    return old;
}

/* ================================================================
   Sec.2  Output (CMD 0x11)
   ================================================================ */

int kw307_set_output(uint8_t mode)
{
    /* The two 0xF3 modes are fixed shapes built from the same block calls a
     * customer composes with (KW307_output.h), so a named mode and a composed
     * one cannot encode the same request two ways.
     *
     * NORMAL_WITH_SPD   - presence, both moving trackers in full with their
     *                     mean speed, and the nearest stationary target.
     *                     34 bytes, sent as B0 2C 09.
     * NORMAL_WITH_SPD_RG- the same body plus the stationary band's gates 0~3,
     *                     each in full: distance, angle, magnitude and speed.
     *                     66 bytes, sent as 30 2E 09. */
    KW307OutputConfig_t c;

    if (mode == KW307_OUTPUT_OFF)
        return kw307_cmd_output_mode(kw307_default_uart(), KW307_WIRE_OUTPUT_OFF);
    if (mode == KW307_OUTPUT_NORMAL)
        return kw307_cmd_output_mode(kw307_default_uart(), KW307_WIRE_OUTPUT_NORMAL);
    if (mode == KW307_OUTPUT_NORMAL_WITH_SPD)
        kw307_out_normal_with_spd(&c);
    else if (mode == KW307_OUTPUT_NORMAL_WITH_SPD_RG)
        kw307_out_normal_with_spd_rg(&c);
    else
        return KW307_ERR_PARAM;
    return kw307_set_output_config(&c);
}

int kw307_get_output(uint8_t* mode)
{
    uint8_t  v = 0u;
    uint16_t n = 0u;
    int      rc;

    if (mode == NULL) return KW307_ERR_PARAM;

    rc = kw307_read_blocking(0x11u, NULL, 0u, &v, 1u, &n, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;
    if (n < 1u)         return KW307_ERR_PORT;

    /* One byte in, one answer out, and no guessing in between.
     *
     * Every 0xF3 shape answers this same byte, so EXTENDED is the whole of
     * what the wire knows. This used to answer with the shape the SDK had
     * noted for itself, which is right while this SDK is the only thing
     * configuring the module and is a confident wrong answer the moment it is
     * not -- a dashboard, a second host, a power cycle. A reader cannot tell
     * the two apart, which makes the useful case the dangerous one.
     *
     * The shape is in the first bytes of every 0xF3 frame, where it cannot be
     * stale, so nothing is lost by not repeating it here. */
    if      (v == KW307_WIRE_OUTPUT_OFF)    *mode = KW307_OUTPUT_OFF;
    else if (v == KW307_WIRE_OUTPUT_NORMAL) *mode = KW307_OUTPUT_NORMAL;
    else if (v == 0xF3u)                    *mode = KW307_OUTPUT_EXTENDED;
    else                                    *mode = KW307_VALUE_NONE;
    return KW307_OK;
}

/* Frame delivery for every 0xF3 shape - kw307_on_frame_spd(),
 * kw307_on_frame_spd_rg() and kw307_on_frame_f3() - is in KW307_output.c, so
 * one handler owns the CMD. */

/* ================================================================
   Sec.3  Device config — power / gain / range / FOV / sensitivity / filters / trackers / hold
   ================================================================ */

int kw307_set_gain(uint8_t index)
{
    if (index < 1u || index > 8u) return KW307_ERR_PARAM;
    return kw307_cmd_gain(kw307_default_uart(), index);
}

int kw307_get_gain(uint8_t* index)
{
    uint8_t  v = 0u;
    uint16_t n = 0u;
    int      rc;

    if (index == NULL) return KW307_ERR_PARAM;

    rc = kw307_read_blocking(0x12u, NULL, 0u, &v, 1u, &n, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;
    if (n < 1u)         return KW307_ERR_PORT;

    /* 1~8 is the whole answer space. Anything else is not a gain index, so say
     * the read failed rather than hand back a value the caller would act on. */
    if (v < 1u || v > 8u) return KW307_ERR_PORT;

    *index = v;
    return KW307_OK;
}

int kw307_set_detect_range(uint16_t min_cm, uint16_t max_cm)
{
    /* min must be strictly less than max, or no valid detection window exists.
     *
     * Upper cap 1200 cm because that is the module: twelve range gates at 100
     * cm each, so 1200 is the furthest distance a gate can report. The cap used
     * to be 3270, which is where the mm value (x10) stops fitting in an int16 -
     * an arithmetic limit standing in for a product one. Anything between the
     * two was accepted, sent, and then described a detection window reaching
     * past the end of the bands: no error, no targets out there either.
     *
     * The 200 cm gate mode that could have filled it is retired. */
    if (min_cm >= max_cm) return KW307_ERR_PARAM;
    if (max_cm > 1200u)   return KW307_ERR_PARAM;
    /* API uses cm; wire is mm -> ×10 before sending. */
    return kw307_cmd_detect_range(kw307_default_uart(),
                                  (uint16_t)(min_cm * 10u),
                                  (uint16_t)(max_cm * 10u));
}

int kw307_get_detect_range(uint16_t* min_cm, uint16_t* max_cm)
{
    int16_t mn = 0, mx = 0;
    int     rc;

    if (min_cm == NULL || max_cm == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_param_read_i16(0x0Fu, &mn, KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;
    rc = kw307_i_param_read_i16(0x10u, &mx, KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;
    if (mn < 0 || mx < 0) return KW307_ERR_PORT;

    /* The module works in mm; the API is cm, as on the way in. */
    *min_cm = (uint16_t)(mn / 10);
    *max_cm = (uint16_t)(mx / 10);
    return KW307_OK;
}

int kw307_set_detect_fov(int16_t fov_deg, uint8_t shrink_rate_deg)
{
    /* fov_deg: 1~180 (physical antenna front hemisphere); SDK pre-multiplies
     * x10 into 0.1 deg on the wire. shrink_rate_deg upper limit 25 keeps the
     * x10 result inside uint8_t (25*10 = 250). */
    if (fov_deg <= 0 || fov_deg > 180) return KW307_ERR_PARAM;
    if (shrink_rate_deg > 25)          return KW307_ERR_PARAM;
    return kw307_cmd_detect_fov(kw307_default_uart(),
                                 (int16_t)(fov_deg * 10),
                                 (uint8_t)(shrink_rate_deg * 10));
}

int kw307_get_detect_fov(int16_t* fov_deg, uint8_t* shrink_rate_deg)
{
    int16_t fov = 0, shrink = 0;
    int     rc;

    if (fov_deg == NULL || shrink_rate_deg == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_param_read_i16(0x11u, &fov,    KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;
    rc = kw307_i_param_read_i16(0x12u, &shrink, KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;
    if (fov < 0 || shrink < 0) return KW307_ERR_PORT;

    /* The module works in tenths of a degree; the API is whole degrees. */
    *fov_deg         = (int16_t)(fov / 10);
    *shrink_rate_deg = (uint8_t)(shrink / 10);
    return KW307_OK;
}

/* ---- Sensitivity levels -------------------------------------------------
 * One row per level, L0 (most sensitive) to L9 (least). The setters write a
 * row; the getters look the module up in the same table. */

/* Motion levels. */
static const struct { uint8_t khpf; int16_t spd_th; uint8_t pts; } MOT_T[10] = {
    { 4u,  250, 4u },  /* L0  most sensitive */
    { 3u,  250, 4u },  /* L1 */
    { 3u,  250, 8u },  /* L2 */
    { 3u,  500, 4u },  /* L3 */
    { 2u,  500, 4u },  /* L4 */
    { 2u,  500, 8u },  /* L5 */
    { 2u, 1000, 4u },  /* L6 */
    { 1u,  500, 8u },  /* L7 */
    { 1u, 1000, 4u },  /* L8 */
    { 1u, 1000, 8u },  /* L9  least sensitive */
};

/* Stationary levels. atten is the baseband attenuator: the top three take
 * 6 dB out at the receiver rather than raising a detection floor, so that a
 * level and kw307_set_mag_threshold() are never two writers of one value. */
static const struct { uint8_t khpf, nd, lpf, atten; } STA_T[10] = {
    { 7u, 3u,  8u, 0u },  /* L0  most sensitive */
    { 7u, 2u,  8u, 0u },  /* L1 */
    { 7u, 1u,  8u, 0u },  /* L2 */
    { 7u, 0u,  8u, 0u },  /* L3 */
    { 6u, 0u,  8u, 0u },  /* L4 */
    { 5u, 0u,  8u, 0u },  /* L5 */
    { 4u, 0u, 16u, 0u },  /* L6 */
    { 4u, 0u, 16u, 1u },  /* L7 */
    { 3u, 0u, 32u, 1u },  /* L8 */
    { 2u, 0u, 64u, 1u },  /* L9  least sensitive */
};

int kw307_set_motion_sensitivity(int8_t level)
{
    /* -1 sits one step below level 0: instead of a row of the table it is a
     * fixed set that reacts to any motion however slow. */
    const int any = (level < 0);
    int rc;

    if (level < -1 || level > 9) return KW307_ERR_PARAM;

    /* Through the call that owns the pair. The table says which corner a
     * level means; that call is what a corner IS. */
    rc = any ? kw307_set_moving_hpf_corner(7u, 3u)
             : kw307_set_moving_hpf_corner(MOT_T[level].khpf, 0u);
    if (rc) return rc;

    {
        /* The table decides which four numbers a level means; the call below
         * is the only thing that writes them, so a level and a direct
         * setting cannot disagree. */
        int16_t spd_th = any ? (int16_t)1 : MOT_T[level].spd_th;
        uint8_t pts    = any ? 4u : MOT_T[level].pts;
        int32_t lim = (int32_t)spd_th * 2;              if (lim > 32767) lim = 32767;
        int32_t sum = any ? 0 : (int32_t)spd_th * pts;  if (sum > 32767) sum = 32767;
        rc = kw307_set_motion_threshold(pts, spd_th, (int16_t)lim, (int16_t)sum);
    }
    return rc;
}

int kw307_get_motion_sensitivity(int8_t* level)
{
    uint8_t dop[6];
    int16_t spd = 0, lim = 0, sum = 0;
    uint8_t pts = 0u;
    uint8_t i;
    int     rc;

    if (level == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_dop_read(dop, KW307_READ_TIMEOUT_MS);                            if (rc != KW307_OK) return rc;
    /* The same four the setter wrote, read by the same call that wrote them. */
    rc = kw307_get_motion_threshold(&pts, &spd, &lim, &sum);
    if (rc != KW307_OK) return rc;

    /* Every value the setter writes must match, so a later call that changed
     * one of them reads as no level. */
    *level = KW307_LEVEL_NONE;
    if (dop[0] == 7u && dop[1] == 3u) {
        if (spd == 1 && pts == 4u && lim == 2 && sum == 0) *level = (int8_t)-1;
        return KW307_OK;
    }
    for (i = 0u; i < 10u; i++) {
        if (dop[0] == MOT_T[i].khpf && dop[1] == 0u &&
            spd == MOT_T[i].spd_th && pts == MOT_T[i].pts &&
            lim == MOT_T[i].spd_th * 2 && sum == MOT_T[i].spd_th * MOT_T[i].pts) {
            *level = (int8_t)i;
            break;
        }
    }
    return KW307_OK;
}

int kw307_set_stationary_sensitivity(int8_t level)
{
    int rc;

    if (level < 0 || level > 9) return KW307_ERR_PARAM;

    /* Each corner setter sends the whole six-tuple, so two commands go out
     * and the module spends about one frame on the new high-pass with the
     * old low-pass. Harmless for a filter corner. */
    rc = kw307_set_stationary_hpf_corner(STA_T[level].khpf, STA_T[level].nd);  if (rc) return rc;
    rc = kw307_set_stationary_lpf_corner(STA_T[level].lpf);                    if (rc) return rc;

    /* The top three levels take 6 dB out at the receiver instead of raising a
     * detection floor. A level used to write the floors as well, which made
     * kw307_set_mag_threshold() and the level two writers of one value: pick a
     * level and the floor you set went back to the table's, pick a floor and
     * the level stopped meaning what it says. Detection floors are one call's
     * now and a level does not touch them.
     *
     * The cost is that the attenuator is ahead of the band split, so this
     * quietens the moving band by the same 6 dB. That is the trade: the top of
     * the stationary ladder is rare, and a knob that only ever moves its own
     * value is worth more than one that is stationary-only but shared.
     *
     * Through the call that owns the attenuator, which leaves the equalizer
     * gain alone - a stationary level has no say in it. */
    return kw307_set_baseband_att(STA_T[level].atten ? 1u : 0u);
}

int kw307_get_stationary_sensitivity(int8_t* level)
{
    uint8_t dop[6];
    uint8_t att = 0u;
    uint8_t hit[10];
    uint8_t n_hit = 0u;
    uint8_t i;
    int     rc;

    if (level == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_dop_read(dop, KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;

    for (i = 0u; i < 10u; i++) {
        if (dop[3] == STA_T[i].khpf && dop[4] == STA_T[i].nd && dop[5] == STA_T[i].lpf)
            hit[n_hit++] = i;
    }

    *level = KW307_LEVEL_NONE;
    if (n_hit == 1u) {
        *level = (int8_t)hit[0];
        return KW307_OK;
    }
    if (n_hit == 0u) return KW307_OK;

    /* L6 and L7 are the same filter and differ only in the 6 dB the top three
     * levels take out at the receiver, so the attenuator decides between them.
     * kw307_set_baseband_att() writes the same value, and a module set by hand
     * to an attenuation neither level uses reads as no level rather than as the
     * nearer one. */
    rc = kw307_get_baseband_att(&att);  if (rc != KW307_OK) return rc;
    for (i = 0u; i < n_hit; i++) {
        if (att == (uint8_t)(STA_T[hit[i]].atten ? 1u : 0u)) {
            *level = (int8_t)hit[i];
            break;
        }
    }
    return KW307_OK;
}

/* The most either hold time takes: the module refuses a frame count above
 * 32767 (a negative int16 on the wire). */
#define HOLD_FRAMES_MAX  32767u

int kw307_set_presence_hold_time(uint16_t frames)
{
    /* Presence hold (debounce): how long presence_flag stays asserted after
     * the last detection, in the module's own unit - frames, sent as they
     * are. 20 frames = 1 s at the normal 20 Hz.
     * Default: 220 frames (11 s). 0 releases on the first frame without a
     * target, the same as 1: the flag sets in the frame a target is seen,
     * whatever the hold. */
    if (frames > HOLD_FRAMES_MAX) return KW307_ERR_PARAM;
    return kw307_set_parameter(0x37u,
                                (uint8_t)(frames >> 8),
                                (uint8_t)(frames & 0xFFu), 2u);
}

int kw307_get_presence_hold_time(uint16_t* frames)
{
    int16_t v = 0;
    int     rc;

    if (frames == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_param_read_i16(0x37u, &v, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;

    *frames = (uint16_t)v;
    return KW307_OK;
}

int kw307_set_mag_threshold(int16_t baseline_moving, int16_t baseline_stationary)
{
    /* Negative values return KW307_ERR_PARAM. */
    if (baseline_moving < 0 || baseline_stationary < 0) return KW307_ERR_PARAM;
    return kw307_cmd_signal_threshold(kw307_default_uart(), baseline_moving, baseline_stationary);
}

int kw307_get_mag_threshold(int16_t* baseline_moving,
                            int16_t* baseline_stationary)
{
    int rc;

    if (baseline_moving == NULL || baseline_stationary == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_param_read_i16(0x24u, baseline_moving,     KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;
    return kw307_i_param_read_i16(0x25u, baseline_stationary, KW307_READ_TIMEOUT_MS);
}

int kw307_set_moving_tracker_cnt(uint8_t cnt_limit, uint8_t display_th)
{
    /* display_th must be <= cnt_limit (equal allowed). Beyond that the target never
     * reaches Valid and always reads Empty. This is the only check. */
    if (display_th > cnt_limit) return KW307_ERR_PARAM;
    return kw307_cmd_moving_tracker_cnt(kw307_default_uart(), cnt_limit, display_th);
}

int kw307_get_moving_tracker_cnt(uint8_t* cnt_limit, uint8_t* display_th)
{
    int rc;

    if (cnt_limit == NULL || display_th == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_param_read_u8(0x2Eu, cnt_limit, KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;
    return kw307_i_param_read_u8(0x2Fu, display_th, KW307_READ_TIMEOUT_MS);
}

int kw307_set_stationary_tracker_cnt(uint8_t cnt_limit, uint8_t display_th)
{
    int rc;

    if (display_th > cnt_limit) return KW307_ERR_PARAM;

    /* The module keeps a second counter of the same kind, on its own
     * parameter, and it answers the same question this call does: how long a
     * stationary target stays reported once it stops being seen. Both start
     * life on 64. Offering it as a third argument would ask the caller to
     * decide which of the two a number belongs to, and there is no answer
     * useful to them, so it follows cnt_limit.
     *
     * Written first: if the pair below fails the caller retries the whole
     * call, and the two end up consistent either way. */
    rc = kw307_set_parameter(0x33u, 0u, cnt_limit, 1u);  if (rc != KW307_OK) return rc;

    return kw307_cmd_stationary_tracker_cnt(kw307_default_uart(), cnt_limit, display_th);
}

int kw307_get_stationary_tracker_cnt(uint8_t* cnt_limit, uint8_t* display_th)
{
    int rc;

    if (cnt_limit == NULL || display_th == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_param_read_u8(0x29u, cnt_limit, KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;
    return kw307_i_param_read_u8(0x2Au, display_th, KW307_READ_TIMEOUT_MS);
}

int kw307_set_RangeGate_IIR(int16_t moving, int16_t stationary)
{
    uint16_t qm = 0u, qs = 0u;
    int      rc;

    rc = kw307_i_permille_to_q15(moving,     &qm); if (rc != KW307_OK) return rc;
    rc = kw307_i_permille_to_q15(stationary, &qs); if (rc != KW307_OK) return rc;

    rc = kw307_set_parameter(0x4Au, (uint8_t)(qm >> 8),
                                    (uint8_t)(qm & 0xFFu), 2u); if (rc) return rc;
    return kw307_set_parameter(0x4Bu, (uint8_t)(qs >> 8),
                                      (uint8_t)(qs & 0xFFu), 2u);
}

int kw307_get_RangeGate_IIR(int16_t* moving, int16_t* stationary)
{
    int16_t qm = 0, qs = 0;
    int     rc;

    if (moving == NULL || stationary == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_param_read_i16(0x4Au, &qm, KW307_READ_TIMEOUT_MS); if (rc != KW307_OK) return rc;
    rc = kw307_i_param_read_i16(0x4Bu, &qs, KW307_READ_TIMEOUT_MS); if (rc != KW307_OK) return rc;

    *moving     = kw307_i_q15_to_permille(qm);
    *stationary = kw307_i_q15_to_permille(qs);
    return KW307_OK;
}

int kw307_set_presence_hold_zone(bool on, int16_t distance_cm, int16_t fov_deg)
{
    /* distance_cm: MCU x10 -> mm. Cap at uint16 mm representable range. */
    if (distance_cm < 0 || distance_cm > 3276) return KW307_ERR_PARAM;
    /* fov_deg: 0~180 (physical antenna front hemisphere); SDK pre-multiplies
     * x10 into 0.1 deg on the wire. */
    if (fov_deg     < 0 || fov_deg     > 180 ) return KW307_ERR_PARAM;

    int rc;
    rc = kw307_set_parameter(0x5Au, 0u, on ? 1u : 0u, 1u);
    if (rc) return rc;
    rc = kw307_set_parameter(0x5Bu,
                              (uint8_t)((uint16_t)distance_cm >> 8),
                              (uint8_t)((uint16_t)distance_cm & 0xFFu), 2u);
    if (rc) return rc;
    uint16_t fov_tenth = (uint16_t)(fov_deg * 10);
    return kw307_set_parameter(0x5Cu,
                                (uint8_t)(fov_tenth >> 8),
                                (uint8_t)(fov_tenth & 0xFFu), 2u);
}

int kw307_get_presence_hold_zone(bool* on, int16_t* distance_cm, int16_t* fov_deg)
{
    uint8_t en  = 0u;
    int16_t fov = 0;
    int     rc;

    if (on == NULL || distance_cm == NULL || fov_deg == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_param_read_u8 (0x5Au, &en,          KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;
    rc = kw307_i_param_read_i16(0x5Bu, distance_cm,  KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;
    rc = kw307_i_param_read_i16(0x5Cu, &fov,         KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;

    *on      = (en != 0u);
    *fov_deg = (int16_t)(fov / 10);   /* module keeps tenths of a degree */
    return KW307_OK;
}

int kw307_set_stationary_release_time(uint8_t enable, uint16_t frames)
{
    if (enable > 1u)     return KW307_ERR_PARAM;
    if (frames > HOLD_FRAMES_MAX) return KW307_ERR_PARAM;
    /* Frames, sent as they are; 20 frames = 1 s at the normal 20 Hz. */
    int rc = kw307_set_parameter(0x36u,
                                  (uint8_t)(frames >> 8),
                                  (uint8_t)(frames & 0xFFu), 2u);
    if (rc) return rc;
    return kw307_set_parameter(0x40u, 0u, enable, 1u);
}

int kw307_get_stationary_release_time(uint8_t* enable, uint16_t* frames)
{
    uint8_t en = 0u;
    int16_t v  = 0;
    int     rc;

    if (enable == NULL || frames == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_param_read_i16(0x36u, &v,  KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;
    rc = kw307_i_param_read_u8 (0x40u, &en, KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;

    *frames = (uint16_t)v;
    *enable = (en != 0u) ? 1u : 0u;
    return KW307_OK;
}

/* Defaults: spd_th=300 (0..1000), dev_th=20 cm (0..50); higher = reject more. */
int kw307_set_advanced_clutter_rejection(bool en, int16_t spd_th, int16_t dev_th)
{
    int rc;
    if (spd_th < 0 || dev_th < 0) return KW307_ERR_PARAM;
    if (dev_th > 6553)            return KW307_ERR_PARAM;   /* the module refuses more */
    rc = kw307_set_parameter(0x59u, 0u, en ? 1u : 0u, 1u);                       if (rc) return rc;
    rc = kw307_set_parameter(0x5Du, (uint8_t)((uint16_t)spd_th >> 8),
                                    (uint8_t)((uint16_t)spd_th & 0xFFu), 2u);     if (rc) return rc;
    return kw307_set_parameter(0x5Eu, (uint8_t)((uint16_t)dev_th >> 8),
                                      (uint8_t)((uint16_t)dev_th & 0xFFu), 2u);
}

int kw307_get_advanced_clutter_rejection(bool* en, int16_t* spd_th,
                                         int16_t* dev_th)
{
    uint8_t e = 0u;
    int     rc;

    if (en == NULL || spd_th == NULL || dev_th == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_param_read_u8 (0x59u, &e,      KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;
    rc = kw307_i_param_read_i16(0x5Du, spd_th,  KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;
    rc = kw307_i_param_read_i16(0x5Eu, dev_th,  KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;

    *en = (e != 0u);
    return KW307_OK;
}

int kw307_set_power_mode(uint8_t mode)
{
    if (mode > KW307_POWER_PA_OFF) return KW307_ERR_PARAM;
    return kw307_cmd_power_mode(kw307_default_uart(), mode);
}

int kw307_get_power_mode(uint8_t* mode)
{
    uint8_t  v = 0u;
    uint16_t n = 0u;
    int      rc;

    if (mode == NULL) return KW307_ERR_PARAM;

    rc = kw307_read_blocking(0x10u, NULL, 0u, &v, 1u, &n, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;
    if (n < 1u)         return KW307_ERR_PORT;

    /* Returned as it comes back, name or no name: a newer module may know modes
     * this build does not, and refusing one would hide it. */
    *mode = v;
    return KW307_OK;
}

int kw307_en_noise_floor_report(bool on)
{
    /* An Empty slot reports the noise floor in mag instead of 0. */
    return kw307_set_parameter(0x6Cu, 0u, on ? 1u : 0u, 1u);
}

int kw307_get_noise_floor_report(bool* on)
{
    uint8_t v = 0u;
    int     rc;

    if (on == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_param_read_u8(0x6Cu, &v, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;

    *on = (v != 0u);
    return KW307_OK;
}

int kw307_en_gpio_presence_flag(bool on)
{
    /* Re-enabling applies the presence the radar already holds; detection
     * never stopped. Not persisted. */
    return kw307_set_parameter(0xFFu, 0u, on ? 1u : 0u, 1u);
}

int kw307_get_gpio_presence_flag(bool* on)
{
    uint8_t v = 0u;
    int     rc;

    if (on == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_param_read_u8(0xFFu, &v, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;

    *on = (v != 0u);
    return KW307_OK;
}

/* ================================================================
   Sec.4  Registers (CMD 0xB0 DSP, 0xB2 analogue)
   ================================================================ */

/* [addr][value] both ways; the module also checks the address window. */
static int reg_write(uint8_t cmd, uint8_t addr, uint8_t value)
{
    uint8_t b[2];

    b[0] = addr;
    b[1] = value;
    return kw307_send_cmd(cmd, b, 2u);
}

static int reg_read(uint8_t cmd, uint8_t addr, uint8_t* value)
{
    uint8_t  r[2] = { 0u, 0u };
    uint16_t n    = 0u;
    int      rc;

    if (value == NULL) return KW307_ERR_PARAM;

    rc = kw307_read_blocking(cmd, &addr, 1u, r, 2u, &n, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;
    if (n < 2u || r[0] != addr) return KW307_ERR_PORT;

    *value = r[1];
    return KW307_OK;
}

int kw307_set_dsp_reg(uint8_t addr, uint8_t value)
{
    if (addr < KW307_DSP_REG_FIRST || addr > KW307_DSP_REG_LAST) return KW307_ERR_PARAM;
    return reg_write(0xB0u, addr, value);
}

int kw307_get_dsp_reg(uint8_t addr, uint8_t* value)
{
    if (addr < KW307_DSP_REG_FIRST || addr > KW307_DSP_REG_LAST) return KW307_ERR_PARAM;
    return reg_read(0xB0u, addr, value);
}

int kw307_set_analog_reg(uint8_t addr, uint8_t value)
{
    if (addr < KW307_ANALOG_REG_FIRST || addr > KW307_ANALOG_REG_LAST) return KW307_ERR_PARAM;
    return reg_write(0xB2u, addr, value);
}

int kw307_get_analog_reg(uint8_t addr, uint8_t* value)
{
    if (addr < KW307_ANALOG_REG_FIRST || addr > KW307_ANALOG_REG_LAST) return KW307_ERR_PARAM;
    return reg_read(0xB2u, addr, value);
}
