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
 * KW307 SDK - UART Framing Layer - Implementation
 * FW: 2.11.0
 *
 * Pure C, no OS/platform dependencies.
 * All byte-level I/O routes through the 5 global functions in KW307_port.h.
 */

#include "KW307_uart.h"
#include "KW307_port.h"
#include <string.h>   /* memset */

/* ================================================================
   Internal: CRC-16
   Poly=0x8005, Seed=0xFFFF, MSB-first (no bit-reversal)
   Scope: CMD + LEN_H + LEN_L + DATA
   ================================================================ */
static void crc16_update(uint16_t* crc, uint8_t byte)
{
    int i;
    *crc ^= (uint16_t)byte << 8;
    for (i = 0; i < 8; i++) {
        if (*crc & 0x8000)
            *crc = (uint16_t)((*crc << 1) ^ 0x8005u);
        else
            *crc = (uint16_t)(*crc << 1);
    }
}

/* ================================================================
   Internal: run one byte through the parser state machine.
   Calls u->on_frame() when a complete valid frame is received.
   ================================================================ */
static void parse_byte(KW307Uart_t* u, uint8_t b)
{
    switch (u->state) {

        case KW307_PS_IDLE:
            if (b == KW307_SOF_H)
                u->state = KW307_PS_SOF2;
            break;

        case KW307_PS_SOF2:
            if (b == KW307_SOF_WRITE || b == KW307_SOF_READ) {
                u->sof2  = b;
                u->state = KW307_PS_CMD;
            } else if (b == KW307_SOF_H) {
                /* stay - could be AA AA 55 */
            } else {
                u->state = KW307_PS_IDLE;
            }
            break;

        case KW307_PS_CMD:
            u->cmd     = b;
            u->crc_cal = 0xFFFFu;
            crc16_update(&u->crc_cal, b);
            u->state   = KW307_PS_LEN_H;
            break;

        case KW307_PS_LEN_H:
            u->data_len = (uint16_t)((uint16_t)b << 8);
            crc16_update(&u->crc_cal, b);
            u->state    = KW307_PS_LEN_L;
            break;

        case KW307_PS_LEN_L:
            u->data_len |= b;
            crc16_update(&u->crc_cal, b);
            u->data_idx  = 0;

            if (u->data_len > KW307_MAX_RX_PAYLOAD) {
                /* Frame too large - resync */
                u->state = (b == KW307_SOF_H) ? KW307_PS_SOF2 : KW307_PS_IDLE;
            } else if (u->data_len == 0) {
                u->state = KW307_PS_CRC_H;
            } else {
                u->state = KW307_PS_DATA;
            }
            break;

        case KW307_PS_DATA:
            u->buf[u->data_idx++] = b;
            crc16_update(&u->crc_cal, b);
            if (u->data_idx >= u->data_len)
                u->state = KW307_PS_CRC_H;
            break;

        case KW307_PS_CRC_H:
            u->crc_rcv = (uint16_t)((uint16_t)b << 8);
            u->state   = KW307_PS_CRC_L;
            break;

        case KW307_PS_CRC_L:
            u->crc_rcv |= b;
            if (u->crc_rcv == u->crc_cal) {
                if (u->on_frame)
                    u->on_frame(u->sof2, u->cmd, u->buf, u->data_len, u->user);
            } else {
                if (u->on_crc_err)
                    u->on_crc_err(u->sof2, u->cmd, u->user);
            }
            /* Always reset to IDLE after CRC field */
            u->state = KW307_PS_IDLE;
            break;

        default:
            u->state = KW307_PS_IDLE;
            break;
    }
}

/* ================================================================
   Internal: build and send one frame.
   Prepends KW307_WAKE_BYTES x 0x00 before the frame.
   Frame: [AA][sof2][cmd][len_h][len_l][data...][crc_h][crc_l]
   ================================================================ */
static int send_frame(KW307Uart_t*   u,
                      uint8_t        sof2,
                      uint8_t        cmd,
                      const uint8_t* data,
                      uint16_t       len)
{
    /* Wake bytes + frame header + data + CRC = 5 + 5 + len + 2 */
    uint8_t  pkt[KW307_WAKE_BYTES + 5 + KW307_MAX_TX_PAYLOAD + 2];
    uint16_t crc = 0xFFFFu;
    uint16_t i;
    int      pkt_len;
    uint8_t  len_h = (uint8_t)((len >> 8) & 0xFFu);
    uint8_t  len_l = (uint8_t)(len & 0xFFu);

    if (!u) return -1;
    if (len > KW307_MAX_TX_PAYLOAD) return -1;

    /* Wake-up preamble */
    memset(pkt, 0x00, KW307_WAKE_BYTES);

    /* Header */
    pkt[KW307_WAKE_BYTES + 0] = KW307_SOF_H;
    pkt[KW307_WAKE_BYTES + 1] = sof2;
    pkt[KW307_WAKE_BYTES + 2] = cmd;
    pkt[KW307_WAKE_BYTES + 3] = len_h;
    pkt[KW307_WAKE_BYTES + 4] = len_l;

    /* CRC: scope = cmd + len_h + len_l + data */
    crc16_update(&crc, cmd);
    crc16_update(&crc, len_h);
    crc16_update(&crc, len_l);

    /* Data */
    for (i = 0; i < len; i++) {
        pkt[KW307_WAKE_BYTES + 5 + i] = data ? data[i] : 0u;
        crc16_update(&crc, pkt[KW307_WAKE_BYTES + 5 + i]);
    }

    /* CRC */
    pkt_len = KW307_WAKE_BYTES + 5 + (int)len;
    pkt[pkt_len++] = (uint8_t)(crc >> 8);
    pkt[pkt_len++] = (uint8_t)(crc & 0xFFu);

    return (kw307_port_send(pkt, pkt_len) == pkt_len) ? 0 : -1;
}

/* ================================================================
   Public API
   ================================================================ */

void kw307_uart_init(KW307Uart_t*   u,
                     KW307FrameCb_t on_frame,
                     void*          user)
{
    memset(u, 0, sizeof(*u));
    u->on_frame = on_frame;
    u->user     = user;
    u->state    = KW307_PS_IDLE;
}

int kw307_uart_open(KW307Uart_t* u, const char* port_name, int baud_rate)
{
    if (!u) return -1;
    return kw307_port_open(port_name, baud_rate);
}

void kw307_uart_close(KW307Uart_t* u)
{
    if (!u) return;
    kw307_port_close();
}

int kw307_uart_send_write(KW307Uart_t*   u,
                           uint8_t        cmd,
                           const uint8_t* data,
                           uint16_t       len)
{
    return send_frame(u, KW307_SOF_WRITE, cmd, data, len);
}

int kw307_uart_send_read(KW307Uart_t*   u,
                          uint8_t        cmd,
                          const uint8_t* data,
                          uint16_t       len)
{
    return send_frame(u, KW307_SOF_READ, cmd, data, len);
}

int kw307_uart_poll(KW307Uart_t* u, int timeout_ms)
{
    uint8_t tmp[64];
    int     n, i;

    if (!u) return -1;
    n = kw307_port_recv(tmp, sizeof(tmp), timeout_ms);
    if (n < 0) return n;

    for (i = 0; i < n; i++)
        parse_byte(u, tmp[i]);

    return n;
}

void kw307_uart_feed_byte(KW307Uart_t* u, uint8_t byte)
{
    if (!u) return;
    parse_byte(u, byte);
}
