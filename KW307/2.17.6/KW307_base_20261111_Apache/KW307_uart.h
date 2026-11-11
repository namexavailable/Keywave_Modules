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
 * KW307 SDK - UART Framing Layer
 * FW: 2.11.0
 *
 * Platform-independent frame builder and parser.
 * All byte-level I/O is routed through the 5 global functions in KW307_port.h.
 *
 * Frame format:
 *   [AA][55/56][CMD][LEN_H][LEN_L][DATA...][CRC16_H][CRC16_L]
 *   CRC-16: Poly=0x8005, Seed=0xFFFF, scope=CMD+LEN_H+LEN_L+DATA
 *
 * Wake-up: 5x 0x00 bytes are sent automatically before every TX frame.
 *
 * --- RX usage: choose ONE approach ---
 *
 * Option A - Polling (PC / Linux / RTOS with threads):
 *   Call kw307_uart_poll() repeatedly in your loop or thread.
 *   It calls kw307_port_recv() internally.
 *
 * Option B - Byte feed (MCU bare-metal, ISR-driven):
 *   Call kw307_uart_feed_byte() for each received byte
 *   (from your UART RX interrupt or DMA callback).
 *
 * In both cases, on_frame() is called whenever a valid frame arrives.
 */

#ifndef KW307_UART_H
#define KW307_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------
   Constants
   ---------------------------------------------------------------- */
#define KW307_SOF_H         0xAAu   /* SOF[0] - fixed */
#define KW307_SOF_WRITE     0x55u   /* SOF[1] - Write / periodic output */
#define KW307_SOF_READ      0x56u   /* SOF[1] - Read request / Read response */
#define KW307_WAKE_BYTES    5       /* 0x00 preamble count before every TX frame */
#define KW307_BAUD_DEFAULT  115200

/* RX buffer: must fit the largest incoming frame. That is a 0xF3 with every
   block and every gate selected, 270 bytes - not the 197 of the plain
   range-gate profile. 512 still leaves room. */
#define KW307_MAX_RX_PAYLOAD  512

/* TX buffer (stack): command payloads are small; 128 bytes is ample. */
#define KW307_MAX_TX_PAYLOAD  128

/* ----------------------------------------------------------------
   Frame callback
   sof2 : KW307_SOF_WRITE (0x55) or KW307_SOF_READ (0x56)
   cmd  : command ID
   data : payload bytes (valid only during callback)
   len  : payload length in bytes
   user : user pointer passed to kw307_uart_init()
   ---------------------------------------------------------------- */
typedef void (*KW307FrameCb_t)(uint8_t sof2, uint8_t cmd,
                                const uint8_t* data, uint16_t len,
                                void* user);

/* ----------------------------------------------------------------
   CRC error callback (optional)
   Called when a fully-received frame fails CRC verification.
   Set u->on_crc_err after kw307_uart_init() to enable; NULL = silent.
   ---------------------------------------------------------------- */
typedef void (*KW307CrcErrCb_t)(uint8_t sof2, uint8_t cmd, void* user);

/* ----------------------------------------------------------------
   Parser state (internal - do not access directly)
   ---------------------------------------------------------------- */
typedef enum {
    KW307_PS_IDLE = 0,
    KW307_PS_SOF2,
    KW307_PS_CMD,
    KW307_PS_LEN_H,
    KW307_PS_LEN_L,
    KW307_PS_DATA,
    KW307_PS_CRC_H,
    KW307_PS_CRC_L
} KW307ParseState_t;

/* ----------------------------------------------------------------
   UART handle - zero-initialize before calling kw307_uart_init()
   ---------------------------------------------------------------- */
typedef struct {
    /* Frame callback (set by init) */
    KW307FrameCb_t  on_frame;
    void*           user;

    /* CRC error callback (optional - set after init, NULL by default) */
    KW307CrcErrCb_t on_crc_err;

    /* --- Internal parser state --- */
    KW307ParseState_t state;
    uint8_t           sof2;        /* captured SOF[1] */
    uint8_t           cmd;         /* captured CMD */
    uint16_t          data_len;    /* captured LEN */
    uint16_t          data_idx;    /* bytes received so far */
    uint16_t          crc_cal;     /* running CRC-16 */
    uint16_t          crc_rcv;     /* received CRC-16 */
    uint8_t           buf[KW307_MAX_RX_PAYLOAD];  /* RX payload accumulator */
} KW307Uart_t;

/* ----------------------------------------------------------------
   Lifecycle
   ---------------------------------------------------------------- */

/**
 * Bind callback. Call once before open().
 * on_frame : called when a valid frame is received (may be NULL for TX-only)
 * user     : opaque pointer forwarded to on_frame()
 */
void kw307_uart_init(KW307Uart_t*   u,
                     KW307FrameCb_t on_frame,
                     void*          user);

/**
 * Open the port. Returns 0 on success, negative on error.
 * Calls kw307_port_open() internally.
 * port_name : "COM3" / "\\\\.\\COM10" on PC, NULL on MCU
 * baud_rate : typically 115200
 */
int kw307_uart_open(KW307Uart_t* u, const char* port_name, int baud_rate);

/**
 * Close the port. Calls kw307_port_close() internally.
 */
void kw307_uart_close(KW307Uart_t* u);

/* ----------------------------------------------------------------
   TX
   ---------------------------------------------------------------- */

/**
 * Send a Write frame: AA 55 [cmd] [len_h] [len_l] [data...] [crc_h] [crc_l]
 * 5 wake-up bytes (0x00) are prepended automatically.
 * Returns 0 on success, negative on error.
 */
int kw307_uart_send_write(KW307Uart_t*   u,
                           uint8_t        cmd,
                           const uint8_t* data,
                           uint16_t       len);

/**
 * Send a Read request frame: AA 56 [cmd] [len_h] [len_l] [data...] [crc_h] [crc_l]
 * For zero-argument reads: data=NULL, len=0.
 * Returns 0 on success, negative on error.
 */
int kw307_uart_send_read(KW307Uart_t*   u,
                          uint8_t        cmd,
                          const uint8_t* data,
                          uint16_t       len);

/* ----------------------------------------------------------------
   RX - Option A: Polling  (PC / Linux / RTOS)
   ---------------------------------------------------------------- */

/**
 * Read available bytes from the port and run them through the parser.
 * Call repeatedly in your loop or RX thread.
 * timeout_ms : 0 = non-blocking, >0 = wait up to N ms
 * Returns number of bytes consumed (>=0), or negative on port error.
 */
int kw307_uart_poll(KW307Uart_t* u, int timeout_ms);

/* ----------------------------------------------------------------
   RX - Option B: Byte feed  (MCU bare-metal / ISR)
   ---------------------------------------------------------------- */

/**
 * Feed one received byte into the parser.
 * Call from your UART RX interrupt handler or DMA callback.
 * on_frame() may be called synchronously from within this function.
 */
void kw307_uart_feed_byte(KW307Uart_t* u, uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif /* KW307_UART_H */
