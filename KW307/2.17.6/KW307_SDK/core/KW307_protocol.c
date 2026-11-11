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
 * KW307 SDK - Protocol Layer - Implementation
 * FW: 2.11.0
 *
 * Decode only: parse Big-Endian wire bytes into host-endian structs.
 * CMD encode + send lives in KW307_cmd.c (internal cmd layer).
 */

#include <stddef.h>     /* NULL */
#include "KW307_protocol.h"

/* ================================================================
   Internal helpers
   ================================================================ */

static int16_t dec_i16be(const uint8_t* p)
{
    return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint16_t dec_u16be(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/* ================================================================
   Internal: parse one Tracker from wire bytes
   ================================================================ */
static const uint8_t* parse_tracker(const uint8_t* p, KW307Tracker_t* t)
{
    t->distance_cm = (uint16_t)(dec_u16be(p) / 10u);   /* wire mm -> struct cm */
    p += 2;
    t->angle_tenth = dec_i16be(p);     p += 2;
    t->mag         = dec_u16be(p);     p += 2;
    t->state       = *p++;
    t->counter     = *p++;
    return p;
}

/* ================================================================
   Decode - periodic radar frame (default, wire CMD 0xF0)
   ================================================================ */
int kw307_dec_frame(const uint8_t* data, uint16_t len, KW307Frame_t* out)
{
    const uint8_t* p = data;
    if (len != 27) return -1;

    out->frame_index = dec_u16be(p);  p += 2;
    out->presence_flag  = *p++;
    /* Wire layout: 2 moving-tracker blocks then 1 stationary block; all three
     * share the same 8-byte tracker format, so parse_tracker handles all. */
    p = parse_tracker(p, &out->moving[0]);
    p = parse_tracker(p, &out->moving[1]);
    p = parse_tracker(p, &out->stationary[0]);
    return 0;
}

/* The shape kw307_set_output(KW307_OUTPUT_NORMAL_WITH_SPD) asks for. Named here
   rather than pulled from KW307_api_advanced.h so the base decoder does not
   depend on the advanced header: these are wire values and they are fixed.
     blk 0x2C = presence + moving trackers + nearest stationary target
     fmt 0x09 = moving trackers in full, each with its mean speed            */
#define FSPD_LEN  34u
#define FSPD_BLK  0x2Cu
#define FSPD_FMT  0x09u

int kw307_dec_frame_spd(const uint8_t* data, uint16_t len, KW307FrameSpd_t* out)
{
    const uint8_t* p = data;

    if (data == NULL || out == NULL) return -1;
    if (len != FSPD_LEN)             return -1;

    out->frame_index = dec_u16be(p);  p += 2;

    /* Reject a frame built to a different descriptor instead of mis-parsing it.
       The module keeps streaming while a new configuration is applied, so a
       frame already in flight can arrive after the switch, and the shape it
       carries is the only way to tell.

       p[0] is rg_sel and is NOT checked: blk 0x2C selects no range-gate block,
       so the gate range cannot move a single byte of this layout. blk and fmt
       together fix it completely. */
    if (p[1] != FSPD_BLK || p[2] != FSPD_FMT) return -1;
    p += 3;

    out->presence_flag = *p++;
    /* Same 8-byte tracker block as 0xF0 - that equivalence is the whole point
       of this shape - but the speed block sits BETWEEN the moving trackers and
       the stationary target, so the stationary slot is read after it, not
       before. */
    p = parse_tracker(p, &out->moving[0]);
    p = parse_tracker(p, &out->moving[1]);
    out->moving_speed[0] = dec_i16be(p);  p += 2;
    out->moving_speed[1] = dec_i16be(p);  p += 2;
    p = parse_tracker(p, &out->stationary[0]);

    return 0;
}

/* The shape kw307_set_output(KW307_OUTPUT_NORMAL_WITH_SPD_RG) asks for.
     rg_sel 0x30 = gates 0~3          (hi nibble 3, lo nibble 0)
     blk    0x2E = FSPD_BLK + the stationary range-gate block
     fmt    0x09 = FSPD_FMT, and deliberately no MAG_ONLY

   rg_sel IS checked here, unlike in kw307_dec_frame_spd: this shape carries a
   range-gate block, so the gate range is exactly what sets the frame's length,
   and a frame built for a different range would parse to the wrong offsets
   rather than be rejected. All three bytes matter now.

   fmt is what separates this from the parking frame, which is the same blk with
   MAG_ONLY set: four gates of magnitude, 38 bytes. Accepting 0x0D here would
   read that frame's 8 bytes of magnitude as this frame's 32 bytes of four
   quantities, and the length check alone would not catch it -- which is why the
   descriptor is checked and not just the size. */
#define FSPDRG_LEN     66u
#define FSPDRG_RG_SEL  0x30u
#define FSPDRG_BLK     0x2Eu
#define FSPDRG_FMT     0x09u
#define FSPDRG_GATES   4u

int kw307_dec_frame_spd_rg(const uint8_t* data, uint16_t len, KW307FrameSpdRg_t* out)
{
    const uint8_t* p = data;
    int i;

    if (data == NULL || out == NULL) return -1;
    if (len != FSPDRG_LEN)           return -1;

    out->frame_index = dec_u16be(p);  p += 2;

    if (p[0] != FSPDRG_RG_SEL || p[1] != FSPDRG_BLK || p[2] != FSPDRG_FMT) return -1;
    p += 3;

    /* Bytes [5~33] are KW307FrameSpd_t's body, in the same order, for the same
       reason it matches 0xF0's: one reader works on all three. */
    out->presence_flag = *p++;
    p = parse_tracker(p, &out->moving[0]);
    p = parse_tracker(p, &out->moving[1]);
    out->moving_speed[0] = dec_i16be(p);  p += 2;
    out->moving_speed[1] = dec_i16be(p);  p += 2;
    p = parse_tracker(p, &out->stationary[0]);

    /* The range-gate block is quantity-major, not gate-major: four distances,
       then four angles, then four magnitudes, then four speeds. One band only,
       so there is no second bank interleaved. */
    /* mm on the wire, cm in the struct - exactly what parse_tracker does to the
       tracker slots above, so one frame does not carry two units. */
    for (i = 0; i < (int)FSPDRG_GATES; i++) { out->rg_distance_cm[i] = (uint16_t)(dec_u16be(p) / 10u); p += 2; }
    for (i = 0; i < (int)FSPDRG_GATES; i++) { out->rg_angle_tenth[i] = dec_i16be(p); p += 2; }
    for (i = 0; i < (int)FSPDRG_GATES; i++) { out->rg_mag[i]         = dec_u16be(p); p += 2; }
    for (i = 0; i < (int)FSPDRG_GATES; i++) { out->rg_speed[i]       = dec_i16be(p); p += 2; }

    return 0;
}

/* ================================================================
   Decode - ACK frame (CMD 0xAC, LEN=2)
   Returns STATUS byte, or -1 if not a valid ACK.
   ================================================================ */
int kw307_dec_ack(uint8_t cmd, const uint8_t* data, uint16_t len,
                  uint8_t* acked_cmd_out)
{
    if (cmd != KW307_CMD_ACK) return -1;
    if (len  != 2)            return -1;
    if (acked_cmd_out) *acked_cmd_out = data[0];
    return (int)data[1];   /* STATUS */
}
