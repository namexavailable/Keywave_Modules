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
 * KW307 SDK - Composed output frame (wire CMD 0xF3) - Implementation
 * FW: 2.11.0
 *
 * Everything 0xF3 lives here: building a shape, sending it, sizing it, decoding
 * any frame built to one, and the one handler that hands each frame to the
 * right callback.
 */

#include <stddef.h>     /* NULL */
#include <string.h>     /* memset */
#include "KW307_output.h"
#include "KW307_api.h"
#include "KW307_cmd.h"
#include "KW307_io.h"

#define RG_MAX  (KW307_OUT_RG_NUM - 1u)

/* ================================================================
   Building a shape
   ================================================================ */

void kw307_out_clear(KW307OutputConfig_t* c)
{
    if (c != NULL) memset(c, 0, sizeof *c);
}

void kw307_out_presence(KW307OutputConfig_t* c, bool on)
{
    if (c != NULL) c->presence = on;
}

void kw307_out_moving_trk(KW307OutputConfig_t* c, bool on, bool full, bool speed)
{
    if (c == NULL) return;
    c->moving_trk.on    = on;
    c->moving_trk.full  = full;
    c->moving_trk.speed = speed;
}

void kw307_out_stationary(KW307OutputConfig_t* c, bool nearest, bool second)
{
    if (c == NULL) return;
    c->stationary.nearest = nearest;
    c->stationary.second  = second;
}

void kw307_out_stationary_trk(KW307OutputConfig_t* c, bool on, bool full)
{
    if (c == NULL) return;
    c->stationary_trk.on   = on;
    c->stationary_trk.full = full;
}

void kw307_out_room_max(KW307OutputConfig_t* c, bool on)
{
    if (c != NULL) c->room_max = on;
}

int kw307_out_rg(KW307OutputConfig_t* c, uint8_t start_rg, uint8_t end_rg,
                 bool moving_band, bool stationary_band, bool mag_only)
{
    if (c == NULL) return KW307_ERR_PARAM;
    if (start_rg > RG_MAX || end_rg > RG_MAX || start_rg > end_rg)
        return KW307_ERR_PARAM;
    c->rg.moving     = moving_band;
    c->rg.stationary = stationary_band;
    c->rg.start_rg   = start_rg;
    c->rg.end_rg     = end_rg;
    c->rg.mag_only   = mag_only;
    return KW307_OK;
}

void kw307_out_normal_with_spd(KW307OutputConfig_t* c)
{
    kw307_out_clear(c);
    kw307_out_presence(c, true);
    kw307_out_moving_trk(c, true, true, true);
    kw307_out_stationary(c, true, false);
}

void kw307_out_normal_with_spd_rg(KW307OutputConfig_t* c)
{
    kw307_out_normal_with_spd(c);
    (void)kw307_out_rg(c, 0u, 3u, false, true, false);
}

/* ================================================================
   Shape <-> the three descriptor bytes
   ================================================================ */

/* Wire values. KW307_F3_* in the header are the same bits, named for reading a
 * frame; these name them for writing one. */
#define BLK_RG_MOVING   KW307_F3_RG_MOVING
#define BLK_RG_STAT     KW307_F3_RG_STATIONARY
#define BLK_RG_ANY      (KW307_F3_RG_MOVING | KW307_F3_RG_STATIONARY)

/* c -> rg_sel / blocks / format. -1 when c selects nothing or a bad range.
 *
 * Sub-options of a block that is off are left out rather than sent: the module
 * would ignore them, but the frame echoes the bytes back, and a shape that
 * reads differently from an identical frame is one more thing to explain.
 *
 * With no gate block, rg_sel is sent as 0xB0 (gates 0~11), the module's own
 * default. It moves no byte of such a frame, and it is what the named modes
 * have always sent, so a typed decoder that checks all three bytes still
 * recognises them. */
static int pack(const KW307OutputConfig_t* c, uint8_t d[3])
{
    bool    rg_on = c->rg.moving || c->rg.stationary;
    uint8_t blk = 0u, fmt = 0u;

    if (rg_on && (c->rg.start_rg > c->rg.end_rg || c->rg.end_rg > RG_MAX))
        return -1;

    if (c->rg.moving)           blk |= KW307_F3_RG_MOVING;
    if (c->rg.stationary)       blk |= KW307_F3_RG_STATIONARY;
    if (c->presence)               blk |= KW307_F3_PRESENCE;
    if (c->stationary.nearest)  blk |= KW307_F3_STATIONARY_NEAREST;
    if (c->stationary.second)   blk |= KW307_F3_STATIONARY_SECOND;
    if (c->moving_trk.on)       blk |= KW307_F3_MOVING_TRK;
    if (c->stationary_trk.on)   blk |= KW307_F3_STATIONARY_TRK;
    if (c->room_max)            blk |= KW307_F3_ROOM_MAX;
    if (blk == 0u) return -1;

    if (c->moving_trk.on && c->moving_trk.full)         fmt |= KW307_F3_MOVING_TRK_FULL;
    if (c->stationary_trk.on && c->stationary_trk.full) fmt |= KW307_F3_STATIONARY_TRK_FULL;
    if (rg_on && c->rg.mag_only)                        fmt |= KW307_F3_RG_MAG_ONLY;
    if (c->moving_trk.on && c->moving_trk.speed)        fmt |= KW307_F3_MOVING_SPEED;

    d[0] = rg_on ? (uint8_t)((c->rg.end_rg << 4) | c->rg.start_rg) : 0xB0u;
    d[1] = blk;
    d[2] = fmt;
    return 0;
}

/* Payload length the module emits for these three bytes, in its block order.
 * 0 for a gate range it never sends - the module clamps on the way in, so a
 * frame carrying one is not a frame.
 * The same function sizes a request and checks an arrival, so the two cannot
 * disagree about a shape. */
static uint16_t shape_len(uint8_t rg_sel, uint8_t blk, uint8_t fmt)
{
    uint16_t n = 5u;                                   /* frame index + shape */

    if (blk & KW307_F3_PRESENCE)              n += 1u;
    if (blk & KW307_F3_MOVING_TRK) {
        n += (fmt & KW307_F3_MOVING_TRK_FULL) ? 16u : 2u;
        if (fmt & KW307_F3_MOVING_SPEED)   n += 4u;    /* only inside the block */
    }
    if (blk & KW307_F3_STATIONARY_NEAREST) n += 8u;
    if (blk & KW307_F3_STATIONARY_SECOND)  n += 8u;
    if (blk & KW307_F3_STATIONARY_TRK)
        n += (fmt & KW307_F3_STATIONARY_TRK_FULL) ? 32u : 4u;
    if (blk & KW307_F3_ROOM_MAX)           n += 4u;
    if (blk & BLK_RG_ANY) {
        uint8_t lo = (uint8_t)(rg_sel & 0x0Fu), hi = (uint8_t)(rg_sel >> 4);
        uint8_t bands = (uint8_t)(((blk & BLK_RG_MOVING) ? 1u : 0u)
                                + ((blk & BLK_RG_STAT)   ? 1u : 0u));
        if (hi > RG_MAX || lo > hi) return 0u;
        n = (uint16_t)(n + 2u * ((fmt & KW307_F3_RG_MAG_ONLY) ? 1u : 4u)
                              * (uint16_t)(hi - lo + 1u) * bands);
    }
    return n;
}

int kw307_i_out_pack(const KW307OutputConfig_t* c, uint8_t shape[3])
{
    if (c == NULL || shape == NULL) return -1;
    return pack(c, shape);
}

uint16_t kw307_out_len(const KW307OutputConfig_t* c)
{
    uint8_t d[3];
    if (c == NULL || pack(c, d) != 0) return 0u;
    return shape_len(d[0], d[1], d[2]);
}

int kw307_set_output_config(const KW307OutputConfig_t* c)
{
    uint8_t b[4];

    if (c == NULL || pack(c, b + 1) != 0) return KW307_ERR_PARAM;
    if (kw307_i_fw_is_old()) return KW307_ERR_NOT_SUPPORTED;   /* no 0xF3 on 2.7.6 */
    /* CMD 0x11, mode and shape in one write: the module never streams a frame
     * built half to the old shape and half to the new. */
    b[0] = 0xF3u;
    return kw307_send_cmd(0x11u, b, 4u);
}

int kw307_set_output_rg(uint8_t start_rg, uint8_t end_rg,
                        bool en_moving_trk, bool en_stationary_trk)
{
    KW307OutputConfig_t c;

    kw307_out_clear(&c);
    if (kw307_out_rg(&c, start_rg, end_rg, true, true, false) != KW307_OK)
        return KW307_ERR_PARAM;
    kw307_out_moving_trk(&c, en_moving_trk, true, false);
    kw307_out_stationary_trk(&c, en_stationary_trk, true);
    return kw307_set_output_config(&c);
}

/* ================================================================
   Decoding any shape
   ================================================================ */

static uint16_t u16be(const uint8_t* p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static int16_t  i16be(const uint8_t* p) { return (int16_t)u16be(p); }

static const uint8_t* parse_tracker(const uint8_t* p, KW307Tracker_t* t)
{
    t->distance_cm = (uint16_t)(u16be(p) / 10u);       /* wire mm -> struct cm */
    t->angle_tenth = i16be(p + 2);
    t->mag         = u16be(p + 4);
    t->state       = p[6];
    t->counter     = p[7];
    return p + 8;
}

/* One quantity of one gate in one band, wire -> struct. q is the module's
 * quantity order: distance, angle, magnitude, speed. */
static void put_gate(KW307Gate_t* g, uint8_t q, const uint8_t* p)
{
    switch (q) {
    case 0u: g->distance_cm = (uint16_t)(u16be(p) / 10u); break;
    case 1u: g->angle_tenth = i16be(p);                   break;
    case 2u: g->mag         = u16be(p);                   break;
    default: g->speed       = i16be(p);                   break;
    }
}

int kw307_dec_frame_f3(const uint8_t* data, uint16_t len, KW307FrameF3_t* out)
{
    const uint8_t* p = data;
    uint8_t blk, fmt, rg_sel;
    int k;

    if (data == NULL || out == NULL || len < 5u) return -1;
    rg_sel = data[2];  blk = data[3];  fmt = data[4];
    if (shape_len(rg_sel, blk, fmt) != len)      return -1;

    memset(out, 0, sizeof *out);
    out->frame_index = u16be(p);  p += 5;
    out->blocks = blk;
    out->format = fmt;

    /* The module's block order - its parser contract, and fixed. */
    if (blk & KW307_F3_PRESENCE) out->presence_flag = *p++;

    if (blk & KW307_F3_MOVING_TRK) {
        for (k = 0; k < 2; k++) {
            if (fmt & KW307_F3_MOVING_TRK_FULL) p = parse_tracker(p, &out->moving_trk[k]);
            else                                out->moving_trk[k].state = *p++;
        }
        if (fmt & KW307_F3_MOVING_SPEED)
            for (k = 0; k < 2; k++) { out->moving_speed[k] = i16be(p); p += 2; }
    }

    /* Second without nearest arrives straight after what precedes it, with no
     * gap - but it is still the second-nearest, so it still lands in [1]. */
    if (blk & KW307_F3_STATIONARY_NEAREST) p = parse_tracker(p, &out->stationary[0]);
    if (blk & KW307_F3_STATIONARY_SECOND)  p = parse_tracker(p, &out->stationary[1]);

    if (blk & KW307_F3_STATIONARY_TRK) {
        for (k = 0; k < 4; k++) {
            if (fmt & KW307_F3_STATIONARY_TRK_FULL) p = parse_tracker(p, &out->stationary_trk[k]);
            else                                    out->stationary_trk[k].state = *p++;
        }
    }

    if (blk & KW307_F3_ROOM_MAX) {
        out->room_max[KW307_BAND_MOVING]     = u16be(p);  p += 2;
        out->room_max[KW307_BAND_STATIONARY] = u16be(p);  p += 2;
    }

    /* Gates: quantity, then gate, then band, both bands low first - or gate,
     * then band, magnitude only. Stored by the real gate number. */
    if (blk & BLK_RG_ANY) {
        uint8_t lo = (uint8_t)(rg_sel & 0x0Fu), hi = (uint8_t)(rg_sel >> 4);
        uint8_t q, q_first = 0u, q_last = 3u, rg, band;

        out->start_rg = lo;
        out->end_rg   = hi;
        if (fmt & KW307_F3_RG_MAG_ONLY) q_first = q_last = 2u;
        for (q = q_first; q <= q_last; q++)
            for (rg = lo; rg <= hi; rg++)
                for (band = 0u; band < 2u; band++) {
                    if (!(blk & (uint8_t)(1u << band))) continue;
                    put_gate(&out->rg[rg][band], q, p);
                    p += 2;
                }
    }
    return 0;
}

/* ================================================================
   Delivery
   ================================================================ */

/* Every 0xF3 shape arrives on one CMD, and io keeps ONE handler per CMD - a
 * second registration replaces the first. So there is one handler for all of
 * them, and the frame decides where it goes: a frame of a typed callback's exact
 * shape goes to that callback when one is registered, and anything else that
 * decodes goes to the general one. A frame matching nothing registered is
 * dropped rather than mis-read, which is also what handles one still in flight
 * from the previous shape.
 *
 * Register all you want once and switch shapes as often as you like. */
static void (*s_cb_spd)(const KW307FrameSpd_t*);
static void (*s_cb_spd_rg)(const KW307FrameSpdRg_t*);
static void (*s_cb_f3)(const KW307FrameF3_t*);

/* Add-on shapes, registered by their three shape bytes,
 * so an add-on gets its frames through this one handler instead of replacing
 * it. Checked first: an exact shape match is the most specific. */
#define OUT_SHAPE_MAX  2
static struct { uint8_t shape[3]; KW307OutputHandler_t h; } s_shape[OUT_SHAPE_MAX];

static void frame_f3_dispatch(uint8_t cmd, const uint8_t* data, uint16_t len);

int kw307_i_out_on_shape(const uint8_t shape[3], KW307OutputHandler_t h)
{
    int i, free_i = -1;

    if (shape == NULL || h == NULL) return KW307_ERR_PARAM;
    for (i = 0; i < OUT_SHAPE_MAX; i++) {
        if (s_shape[i].h != NULL && memcmp(s_shape[i].shape, shape, 3) == 0) {
            s_shape[i].h = h;                          /* replace its own entry */
            return kw307_register_output_handler(0xF3u, frame_f3_dispatch);
        }
        if (s_shape[i].h == NULL && free_i < 0) free_i = i;
    }
    if (free_i < 0) return KW307_ERR_PARAM;            /* table full */
    memcpy(s_shape[free_i].shape, shape, 3);
    s_shape[free_i].h = h;
    return kw307_register_output_handler(0xF3u, frame_f3_dispatch);
}

static void frame_f3_dispatch(uint8_t cmd, const uint8_t* data, uint16_t len)
{
    /* Static, not on the stack: 272 bytes is a lot of stack on a small part,
     * and kw307_update() never re-enters this. */
    static KW307FrameF3_t f3;
    int i;

    if (data != NULL && len >= 5u) {
        for (i = 0; i < OUT_SHAPE_MAX; i++) {
            if (s_shape[i].h != NULL && memcmp(s_shape[i].shape, data + 2, 3) == 0) {
                s_shape[i].h(cmd, data, len);
                return;
            }
        }
    }

    if (s_cb_spd) {
        KW307FrameSpd_t f;
        if (kw307_dec_frame_spd(data, len, &f) == 0) { s_cb_spd(&f); return; }
    }
    if (s_cb_spd_rg) {
        KW307FrameSpdRg_t g;
        if (kw307_dec_frame_spd_rg(data, len, &g) == 0) { s_cb_spd_rg(&g); return; }
    }
    if (s_cb_f3 && kw307_dec_frame_f3(data, len, &f3) == 0) s_cb_f3(&f3);
}

void kw307_on_frame_spd(void (*cb)(const KW307FrameSpd_t*))
{
    s_cb_spd = cb;
    kw307_register_output_handler(0xF3u, frame_f3_dispatch);
}

void kw307_on_frame_spd_rg(void (*cb)(const KW307FrameSpdRg_t*))
{
    s_cb_spd_rg = cb;
    kw307_register_output_handler(0xF3u, frame_f3_dispatch);
}

void kw307_on_frame_f3(void (*cb)(const KW307FrameF3_t*))
{
    s_cb_f3 = cb;
    kw307_register_output_handler(0xF3u, frame_f3_dispatch);
}
