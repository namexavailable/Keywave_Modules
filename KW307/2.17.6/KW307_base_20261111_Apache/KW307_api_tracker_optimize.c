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
 * KW307 SDK - API, Tracker Optimization - Implementation
 * FW: 2.11.0
 */

#include <stddef.h>     /* NULL */
#include "KW307_api.h"
#include "KW307_cmd.h"
#include "KW307_io.h"

/* Smoothing is 0~999 (0.000~0.999). */
#define TRK_SMOOTH_MAX  999

/* ---- Stationary tracker ----------------------------------------------- */

/* Three parameters, and none of them a counter. The counters are
 * kw307_set_stationary_tracker_cnt(); what is left here is everything
 * that call does not cover. */
int kw307_set_stationary_tracker_opt(uint8_t confirm_frames, int16_t smoothing,
                                     uint16_t match_distance_cm)
{
    uint16_t q  = 0u;
    uint16_t mm;
    int      rc;

    if (confirm_frames == 0u)                         return KW307_ERR_PARAM;  /* module refuses 0 */
    if (smoothing < 0 || smoothing > TRK_SMOOTH_MAX) return KW307_ERR_PARAM;
    if (match_distance_cm > 3276u)                   return KW307_ERR_PARAM;  /* x10 stays in uint16 */

    rc = kw307_i_permille_to_q15(smoothing, &q);  if (rc != KW307_OK) return rc;
    mm = (uint16_t)(match_distance_cm * 10u);     /* API uses cm; the module keeps mm */

    rc = kw307_set_parameter(0x2Cu, 0u, confirm_frames, 1u);                          if (rc) return rc;
    rc = kw307_set_parameter(0x20u, (uint8_t)(q >> 8), (uint8_t)(q & 0xFFu), 2u);    if (rc) return rc;
    return kw307_set_parameter(0x14u, (uint8_t)(mm >> 8), (uint8_t)(mm & 0xFFu), 2u);
}

int kw307_get_stationary_tracker_opt(uint8_t* confirm_frames, int16_t* smoothing,
                                     uint16_t* match_distance_cm)
{
    uint8_t cc = 0u;
    int16_t q  = 0, mm = 0;
    int     rc;

    if (confirm_frames == NULL || smoothing == NULL || match_distance_cm == NULL)
        return KW307_ERR_PARAM;

    rc = kw307_i_param_read_u8 (0x2Cu, &cc, KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;
    rc = kw307_i_param_read_i16(0x20u, &q,  KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;
    rc = kw307_i_param_read_i16(0x14u, &mm, KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;
    if (mm < 0)                                           return KW307_ERR_PORT;

    *confirm_frames     = cc;
    *smoothing         = kw307_i_q15_to_permille(q);
    *match_distance_cm = (uint16_t)(mm / 10);    /* module keeps mm; the API is cm */
    return KW307_OK;
}

/* ---- Moving tracker --------------------------------------------------- */

/* The four values that decide what counts as movement, and the only place they
 * are written. kw307_set_motion_sensitivity() is a level that looks them up in
 * a table and calls this - so the table decides WHICH numbers and this decides
 * how they reach the module, once.
 *
 * All four are taken literally. An earlier call derived speed_clip and
 * min_speed_sum from the other two, and while that pairing is the sensible one
 * it is not the only legal one: the sensitivity table's own bypass entry needs
 * min_speed_sum at 0 with confirm_frames at 4, which no derivation from those
 * two produces. */
int kw307_set_motion_threshold(uint8_t confirm_frames, int16_t min_speed,
                               int16_t speed_clip, int16_t min_speed_sum)
{
    int rc;

    /* The module refuses 0 and anything above 32. */
    if (confirm_frames == 0u || confirm_frames > 32u) return KW307_ERR_PARAM;

    /* Written in the order the sensitivity level has always sent them, which
     * is not the order of the arguments. Four independent parameter writes, so
     * the module cannot tell - but hosts transcribe this sequence, and there is
     * nothing to gain from making a shipped wire order move. */
    rc = kw307_set_parameter(0x26u, (uint8_t)((uint16_t)min_speed >> 8),
                                    (uint8_t)((uint16_t)min_speed & 0xFFu), 2u);     if (rc) return rc;
    rc = kw307_set_parameter(0x1Au, (uint8_t)((uint16_t)speed_clip >> 8),
                                    (uint8_t)((uint16_t)speed_clip & 0xFFu), 2u);    if (rc) return rc;
    rc = kw307_set_parameter(0x31u, 0u, confirm_frames, 1u);                        if (rc) return rc;
    return kw307_set_parameter(0x28u, (uint8_t)((uint16_t)min_speed_sum >> 8),
                                      (uint8_t)((uint16_t)min_speed_sum & 0xFFu), 2u);
}

int kw307_get_motion_threshold(uint8_t* confirm_frames, int16_t* min_speed,
                               int16_t* speed_clip, int16_t* min_speed_sum)
{
    uint8_t cf = 0u;
    int16_t th = 0, clip = 0, sum = 0;
    int     rc;

    if (confirm_frames == NULL || min_speed == NULL ||
        speed_clip == NULL || min_speed_sum == NULL)
        return KW307_ERR_PARAM;

    rc = kw307_i_param_read_u8 (0x31u, &cf,   KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;
    rc = kw307_i_param_read_i16(0x26u, &th,   KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;
    rc = kw307_i_param_read_i16(0x1Au, &clip, KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;
    rc = kw307_i_param_read_i16(0x28u, &sum,  KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;

    *confirm_frames = cf;
    *min_speed      = th;
    *speed_clip     = clip;
    *min_speed_sum  = sum;
    return KW307_OK;
}

/* How steadily a moving target's reported position follows each measurement.
 * On its own because nothing else writes it - the sensitivity level does not,
 * and the counters do not. */
int kw307_set_moving_smoothing(int16_t smoothing)
{
    uint16_t q = 0u;
    int      rc;

    if (smoothing < 0 || smoothing > TRK_SMOOTH_MAX) return KW307_ERR_PARAM;

    rc = kw307_i_permille_to_q15(smoothing, &q);  if (rc != KW307_OK) return rc;
    return kw307_set_parameter(0x21u, (uint8_t)(q >> 8), (uint8_t)(q & 0xFFu), 2u);
}

int kw307_get_moving_smoothing(int16_t* smoothing)
{
    int16_t q = 0;
    int     rc;

    if (smoothing == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_param_read_i16(0x21u, &q, KW307_READ_TIMEOUT_MS);  if (rc != KW307_OK) return rc;
    *smoothing = kw307_i_q15_to_permille(q);
    return KW307_OK;
}

/* ---- Field of view ---------------------------------------------------- */

int kw307_set_fov_min(int16_t fov_deg)
{
    uint16_t tenth;

    if (fov_deg < 0 || fov_deg > 180) return KW307_ERR_PARAM;

    /* API uses whole degrees; the module keeps tenths. */
    tenth = (uint16_t)(fov_deg * 10);
    return kw307_set_parameter(0x52u, (uint8_t)(tenth >> 8), (uint8_t)(tenth & 0xFFu), 2u);
}

int kw307_get_fov_min(int16_t* fov_deg)
{
    int16_t v = 0;
    int     rc;

    if (fov_deg == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_param_read_i16(0x52u, &v, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;
    if (v < 0)          return KW307_ERR_PORT;

    *fov_deg = (int16_t)(v / 10);   /* module keeps tenths of a degree */
    return KW307_OK;
}

/* ---- Line of sight (PID 0x08) ------------------------------------------ */

/* There is no enable bit: the module reads a zero wedge angle as "do not do
 * this at all". So on writes the module's own default of 4.0 degrees and off
 * writes zero, and that is the whole of it.
 *
 * Which means this does not preserve a tuned wedge. Someone who has widened or
 * narrowed the angle and then switches line of sight off and on again gets 4.0
 * back, not what they set. That is the price of a switch with no state in it,
 * and it is the right price here: the alternative is an SDK that remembers a
 * number the module already stores, and a remembered number is one that can be
 * wrong. */
#define LOS_ANGLE_DEFAULT_TENTH  40u   /* 4.0 degrees */

int kw307_en_los(bool on)
{
    return kw307_set_parameter(0x08u, 0u, on ? LOS_ANGLE_DEFAULT_TENTH : 0u, 2u);
}

int kw307_get_los(bool* on)
{
    int16_t v = 0;
    int     rc;

    if (on == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_param_read_i16(0x08u, &v, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;

    *on = (v > 0);
    return KW307_OK;
}
