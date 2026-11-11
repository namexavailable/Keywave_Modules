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
 * KW307 SDK - Runtime / IO Layer
 * FW: 2.11.0
 *
 * The MINIMUM surface needed to drive the SDK and receive radar frames.
 *
 *   - kw307_init()                  once at startup
 *   - kw307_update()                every main-loop iteration
 *   - kw307_on_frame()              register your callback to consume frames
 *
 * BASIC USAGE (no configuration, the module runs on its saved settings):
 *   #include "KW307_io.h"
 *
 *   static void on_radar(const KW307Frame_t* f) {
 *       GPIO_SET(LED, f->presence_flag);          // presence on/off
 *       // f->moving[0].distance_cm / .angle_tenth also available
 *   }
 *
 *   int main(void) {
 *       kw307_init();
 *       kw307_on_frame(on_radar);
 *       while (1) kw307_update();
 *   }
 *
 * For configuration calls (gain, range, FOV, sensitivity, factory reset,
 * firmware-version check, ...) additionally #include "KW307_api.h" — the
 * feature layer that sits on top of this one.
 */

#ifndef KW307_IO_H
#define KW307_IO_H

#include <stdint.h>
#include <stdbool.h>
#include "KW307_protocol.h"   /* KW307Frame_t */

#ifdef __cplusplus
extern "C" {
#endif


/* ================================================================
   Error codes
   KW307_ERR_PORT (-1) is the generic UART failure; the sub-codes below give
   finer diagnosis. `if (rc != KW307_OK)` catches every error; switch on a
   specific value when you need to tell failures apart.
   ================================================================ */
typedef enum {
    KW307_OK              =   0,   /* success */

    /* --- Port (UART) layer --- */
    KW307_ERR_PORT        =  -1,   /* generic UART failure */
    KW307_ERR_PORT_OPEN   = -10,   /* kw307_port_open() returned non-zero */
    KW307_ERR_PORT_CLOSED = -11,   /* API called before init, or port lost */
    KW307_ERR_PORT_SEND   = -12,   /* kw307_port_send returned short / negative */
    KW307_ERR_PORT_RECV   = -13,   /* kw307_port_recv returned negative */

    /* --- Argument / capability --- */
    KW307_ERR_PARAM       =  -2,   /* argument out of valid range */
    KW307_ERR_NOT_IMPL    =  -3,   /* feature not yet implemented in this SDK build */
    KW307_ERR_NOT_SUPPORTED = -4,  /* the module's firmware (2.7.6) does not have this
                                      call; known once kw307_check_fw_version() has run */

    /* --- Blocking-response layer (kw307_read_blocking and friends) --- */
    KW307_ERR_TIMEOUT     =  -5,   /* no response in time */
    KW307_ERR_NACK        = -20,   /* the module answered with an error status; see kw307_last_nack_status() */
    KW307_ERR_RESP_LEN    = -21,   /* response payload shorter than expected */
    KW307_ERR_RESP_CRC    = -22,   /* response CRC-16 mismatch */
} KW307Err_t;


/* ================================================================
   Lifecycle
   ================================================================ */

/**
 * Initialise the SDK and open the UART to the module. Must be called once before any other
 * SDK function. Port name and baud rate are read from user_config.h.
 *
 * Returns KW307_OK on success.
 * Returns KW307_ERR_PORT_OPEN if the port cannot be opened.
 */
int  kw307_init(void);

/**
 * After a KW307_ERR_NACK return, fetch the reason code the module gave
 * (0x01=invalid value, 0x02=unknown CMD, 0x03=CRC error,
 * 0x04=invalid length, etc.). Returns 0 if no NACK has occurred since last
 * clear; the value is latched until the next NACK overwrites it.
 */
uint8_t kw307_last_nack_status(void);

/** Close the port. */
int  kw307_close(void);

/** Returns true if the port is currently open. */
bool kw307_is_open(void);


/* ================================================================
   Output delivery — register a callback; it fires on every radar
   frame. The callback runs in the same context as kw307_update()
   (typically your main loop on MCU). Keep it short — latch fields
   into your own variables and act on them in the main loop.
   ================================================================ */

/**
 * Register a per-frame callback for the default 27-byte frame
 * (KW307_OUTPUT_NORMAL). Pass NULL to unregister. The pointer is valid only
 * during the callback — copy it out if you need it later. Called from
 * inside kw307_update().
 *
 * Use a target slot only when its state is Valid — see KW307Tracker_t.state.
 */
void kw307_on_frame(void (*cb)(const KW307Frame_t*));

/* Additional periodic-output frame callbacks may be provided by optional
 * feature modules, which register themselves via the extension point below. */


/* ================================================================
   Output-frame extension point (for optional feature modules)
   ----------------------------------------------------------------
   A feature module registers a handler for its own periodic-output wire CMD;
   base io dispatches matching frames to it without needing to know the module
   exists. The default frame (CMD 0xF0) is delivered via kw307_on_frame() and
   does not use this hook.
   Returns KW307_OK, or KW307_ERR_PARAM (cmd==0, NULL handler, or table full).
   ================================================================ */
typedef void (*KW307OutputHandler_t)(uint8_t cmd, const uint8_t* data, uint16_t len);
int kw307_register_output_handler(uint8_t cmd, KW307OutputHandler_t handler);


/* ================================================================
   RX update — call repeatedly on MCU / Arduino
   ----------------------------------------------------------------
   Pulls bytes from the platform UART, runs them through the frame
   parser, and fires registered callbacks for any complete frame.
   Non-blocking — returns immediately when the RX buffer is empty.

   Returns number of bytes consumed (>=0), or KW307_ERR_PORT on error.
   ================================================================ */
int kw307_update(void);


/* Every getter, and every setter that reads the module before it writes,
 * waits about KW307_READ_TIMEOUT_MS for the answer, then returns
 * KW307_ERR_TIMEOUT. A wait always ends; nothing blocks forever. */
#define KW307_READ_TIMEOUT_MS      200
#define KW307_FW_CHECK_TIMEOUT_MS  300


/* ================================================================
   Blocking read (synchronous request/response)
   ----------------------------------------------------------------
   Sends a Read request (AA 56) and blocks until the module responds
   or timeout_ms expires. Intended for setup-time queries and MCU
   main-loop usage. Typed Read wrappers in KW307_api.h use this
   internally — call directly only for CMDs that have no typed wrapper.
   ================================================================ */
int kw307_read_blocking(uint8_t cmd,
                        const uint8_t* req_data, uint16_t req_len,
                        uint8_t* resp_buf, uint16_t resp_buf_max,
                        uint16_t* resp_len,
                        int timeout_ms);


/* ================================================================
   Blocking write (synchronous Write + ACK)
   ----------------------------------------------------------------
   Sends a Write (AA 55) and waits for the module's ACK. The plain setters
   only report that the bytes left the host; use this when a rejected
   setting must be seen.

   Returns KW307_OK             the module accepted it
           KW307_ERR_NACK       the module rejected it; kw307_last_nack_status()
                                gives why (0x01 invalid value, 0x02 unknown
                                CMD, 0x03 CRC, 0x04 bad length)
           KW307_ERR_TIMEOUT    no ACK within timeout_ms (250 is a safe value
                                for settings; kw307_radar_init() needs about
                                1000 and kw307_factory_reset() about 3000)
           KW307_ERR_PORT_SEND / KW307_ERR_PORT_CLOSED / KW307_ERR_PARAM

   An accepted write is not proof the value took effect - read it back. Do
   not retry factory reset, save or clear-state after a timeout: whether the
   module acted is unknown. Plain parameter writes are safe to retry.
   ================================================================ */
int kw307_write_blocking(uint8_t cmd,
                         const uint8_t* data, uint16_t len,
                         int timeout_ms);


/* ================================================================
   Write verify mode (opt-in, off by default)
   ----------------------------------------------------------------
   Makes every write the SDK sends - including each one inside a setter -
   wait for its ACK, with no change at the call site. For a configuration
   tool or a production fixture; leave it off in a product's running loop.

     kw307_set_write_verify(250, on_write_failed);   // 250 ms per write
     kw307_write_stats_reset();                      // start of the batch
     ... your kw307_set_*() calls ...
     KW307WriteStats_t st; kw307_write_stats_get(&st);

   A rejection (KW307_ERR_NACK) is returned. A timeout is counted and passed to
   the callback but not returned, and every write after it in the batch is
   sent without waiting (`unverified`) until kw307_write_stats_reset(). Judge
   a batch by the counters. The callback fires once per failed write; keep it
   short and make no SDK calls from it. `status` is meaningful for a NACK only.

   Keep kw307_radar_init() and kw307_factory_reset() out of a verified batch,
   or raise the timeout for them: they take far longer than 250 ms, and a
   timeout there would leave the rest of the batch unverified.
   ================================================================ */
typedef void (*KW307WriteResultCb)(uint8_t cmd, int rc, uint8_t status);

typedef struct {
    uint16_t sent;         /* writes issued while verify was on */
    uint16_t verified;     /* ACK STATUS_OK */
    uint16_t rejected;     /* KW307_ERR_NACK — module rejected the value */
    uint16_t timed_out;    /* KW307_ERR_TIMEOUT — no ACK in budget */
    uint16_t unverified;   /* sent after a timeout suspended verification, outcome unknown */
} KW307WriteStats_t;

/** timeout_ms 0 disables the mode (the default). cb may be NULL. */
void kw307_set_write_verify(int timeout_ms, KW307WriteResultCb cb);

/** Zero the counters and resume verification after a timeout. Call per batch. */
void kw307_write_stats_reset(void);

/** Copy the current counters out. */
void kw307_write_stats_get(KW307WriteStats_t* out);


#ifdef __cplusplus
}
#endif

#endif /* KW307_IO_H */
