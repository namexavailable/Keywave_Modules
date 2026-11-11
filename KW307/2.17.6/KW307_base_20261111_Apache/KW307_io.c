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
 * KW307 SDK - Runtime / IO Layer - Implementation
 * FW: 2.11.0
 *
 * Owns the SDK's singleton state (s_uart / callback / blocking read).
 * The feature layer (KW307_api.c) and the CMD encoder layer
 * (KW307_cmd.c) reach this state ONLY via the non-public accessor
 * kw307_default_uart() — declared inline in each consumer file, never
 * promoted to a header.
 */

#include "KW307_io.h"
#include "KW307_uart.h"
#include "KW307_port.h"
#include <string.h>


/* ================================================================
   Internal state - singleton
   ================================================================ */

static KW307Uart_t  s_uart;

/* Output callbacks — one per typed periodic frame. Each fires from the
 * receive dispatcher when a matching wire CMD byte arrives. */
static void (*s_cb_frame)        (const KW307Frame_t*);          /* CMD 0xF0 (default frame) */

/* Output-frame handler registry — feature add-ons register here (see
 * kw307_register_output_handler). Base io dispatches any output frame whose
 * wire CMD matches a registered handler, without knowing the module exists. */
#define KW307_MAX_OUTPUT_HANDLERS 4
static struct { uint8_t cmd; KW307OutputHandler_t h; } s_out[KW307_MAX_OUTPUT_HANDLERS];

/* Blocking-read state (see kw307_read_blocking). */
#define KW307_READ_BUF_MAX  64
static volatile struct {
    uint8_t  waiting_cmd;    /* 0 = idle, else CMD being awaited */
    uint8_t  got_response;   /* 1 = data frame captured */
    uint8_t  got_ack_fail;   /* 1 = ACK with non-OK status */
    uint8_t  ack_status;     /* MCU STATUS byte from last NACK (latched) */
    uint16_t resp_len;
    uint8_t  resp_buf[KW307_READ_BUF_MAX];
} s_read;

/* Blocking-write state (see kw307_write_blocking). Separate from s_read so a
 * write transaction can never consume a read's data frame or vice versa: the
 * dispatcher keys on the SOF direction, and only one of the two slots is ever
 * armed at a time. */
static volatile struct {
    uint8_t waiting_cmd;     /* 0 = idle, else CMD whose write-ACK is awaited */
    uint8_t got_ack;         /* 1 = ACK for waiting_cmd seen */
    uint8_t ack_status;      /* its STATUS byte (0 = OK) */
} s_write;

/* Write-verify mode (see kw307_set_write_verify). Off by default, so a plain
 * SDK build keeps the original fire-and-forget behaviour byte for byte. */
static struct {
    int                 timeout_ms;   /* 0 = off */
    KW307WriteResultCb  cb;
    uint8_t             degraded;     /* sticky: a write timed out, stop waiting */
    KW307WriteStats_t   stats;
} s_verify;


/* What kw307_check_fw_version() learnt about the module: 1 = firmware 2.7.6,
 * which lacks some calls. 0 until the check has run, and again after
 * kw307_init(), so an unchecked module is never refused anything. */
static uint8_t s_fw_old;

void kw307_i_fw_set_old(bool old) { s_fw_old = old ? 1u : 0u; }
bool kw307_i_fw_is_old(void)      { return s_fw_old != 0u; }

/* One wait for an answer. Only a poll that found nothing counts as a
 * millisecond - the port waits up to 1 ms for it - so a busy line makes the
 * wait longer, never shorter. The cap on all polls keeps a line that never
 * goes quiet from waiting forever. */
#define KW307_WAIT_POLL_CAP  8

typedef struct {
    int budget_ms;
    int idle;
    int polls;
} KW307Wait_t;

static void wait_begin(KW307Wait_t* w, int budget_ms)
{
    w->budget_ms = budget_ms;
    w->idle      = 0;
    w->polls     = 0;
}

/* Poll the UART once. Returns 0, without polling, once the budget is spent. */
static int wait_step(KW307Wait_t* w)
{
    int n;

    if (w->idle >= w->budget_ms ||
        w->polls >= w->budget_ms * KW307_WAIT_POLL_CAP) return 0;

    n = kw307_uart_poll(&s_uart, 1);
    w->polls++;
    if (n <= 0) w->idle++;
    return 1;
}

/* Singleton accessor — KW307_api.c and KW307_cmd.c reach the UART context
 * via this function (each declares it locally as extern). Intentionally
 * NOT exposed in any header so the singleton stays an internal concept of
 * the api/cmd/io layer triad. */
KW307Uart_t* kw307_default_uart(void) { return &s_uart; }


/* Forward declaration */
static void on_frame(uint8_t sof2, uint8_t cmd,
                     const uint8_t* data, uint16_t len, void* user);


/* ================================================================
   Lifecycle
   ================================================================ */

int kw307_init(void)
{
    memset(&s_uart, 0, sizeof(s_uart));
    s_cb_frame          = NULL;
    /* Full reset of the blocking-read slot so a previous session's NACK status
     * or response bytes can't leak into the new connection (e.g. when the app
     * reconnects after a USB unplug). */
    s_read.waiting_cmd  = 0u;
    s_read.got_response = 0u;
    s_read.got_ack_fail = 0u;
    s_read.ack_status   = 0u;
    s_read.resp_len     = 0u;
    memset((void*)s_read.resp_buf, 0, sizeof(s_read.resp_buf));
    s_write.waiting_cmd = 0u;
    s_write.got_ack     = 0u;
    s_write.ack_status  = 0u;
    s_fw_old            = 0u;   /* a new connection may be a different module */

    kw307_uart_init(&s_uart, on_frame, NULL);

    /* port_name=NULL, baud_rate=0: each platform reads from user_config.h */
    if (kw307_uart_open(&s_uart, NULL, 0) != 0)
        return KW307_ERR_PORT_OPEN;

    return KW307_OK;
}

uint8_t kw307_last_nack_status(void)
{
    return s_read.ack_status;   /* latched until next NACK overwrites */
}

int kw307_close(void)
{
    kw307_uart_close(&s_uart);
    /* Clear the blocking-read slot too so a subsequent reconnect via
     * kw307_init() starts from a clean state — guards against a stray frame
     * arriving in the window between close and the next init landing in a
     * stale capture slot. */
    s_read.waiting_cmd  = 0u;
    s_read.got_response = 0u;
    s_read.got_ack_fail = 0u;
    s_write.waiting_cmd = 0u;
    s_write.got_ack     = 0u;
    return KW307_OK;
}

bool kw307_is_open(void)
{
    return kw307_port_is_open() != 0;
}


/* ================================================================
   Output delivery
   ================================================================ */

void kw307_on_frame        (void (*cb)(const KW307Frame_t*))        { s_cb_frame     = cb; }

int kw307_register_output_handler(uint8_t cmd, KW307OutputHandler_t handler)
{
    int free_i = -1;
    if (cmd == 0u || handler == NULL) return KW307_ERR_PARAM;
    for (int i = 0; i < KW307_MAX_OUTPUT_HANDLERS; i++) {
        if (s_out[i].cmd == cmd) { s_out[i].h = handler; return KW307_OK; }  /* replace existing */
        if (s_out[i].cmd == 0u && free_i < 0) free_i = i;                    /* remember a free slot */
    }
    if (free_i < 0) return KW307_ERR_PARAM;   /* table full */
    s_out[free_i].cmd = cmd;
    s_out[free_i].h   = handler;
    return KW307_OK;
}


/* ================================================================
   RX update (MCU / Arduino main loop)
   ================================================================ */

int kw307_update(void)
{
    int r = kw307_uart_poll(&s_uart, 0);
    return r < 0 ? KW307_ERR_PORT : r;
}


/* ================================================================
   Blocking read - generic synchronous request/response
   ================================================================ */

int kw307_read_blocking(uint8_t cmd,
                        const uint8_t* req_data, uint16_t req_len,
                        uint8_t* resp_buf, uint16_t resp_buf_max,
                        uint16_t* resp_len,
                        int timeout_ms)
{
    KW307Wait_t w;

    if (!resp_buf || resp_buf_max == 0)     return KW307_ERR_PARAM;
    if (req_len > 0 && !req_data)           return KW307_ERR_PARAM;
    if (cmd == 0u)                          return KW307_ERR_PARAM;

    if (!kw307_port_is_open()) return KW307_ERR_PORT_CLOSED;

    /* Arm capture slot */
    s_read.got_response = 0u;
    s_read.got_ack_fail = 0u;
    s_read.resp_len     = 0u;
    s_read.waiting_cmd  = cmd;

    if (kw307_uart_send_read(&s_uart, cmd, req_data, req_len) != 0) {
        s_read.waiting_cmd = 0u;
        return KW307_ERR_PORT_SEND;
    }

    /* Poll until response, rejection, port-down, or timeout. */
    wait_begin(&w, timeout_ms);
    while (wait_step(&w)) {
        if (s_read.got_response) {
            uint16_t n = (s_read.resp_len < resp_buf_max)
                       ?  s_read.resp_len : resp_buf_max;
            for (uint16_t i = 0; i < n; i++) resp_buf[i] = s_read.resp_buf[i];
            if (resp_len) *resp_len = n;
            s_read.waiting_cmd = 0u;
            return KW307_OK;
        }
        if (s_read.got_ack_fail) {
            s_read.waiting_cmd = 0u;
            return KW307_ERR_NACK;   /* MCU rejected — see kw307_last_nack_status() */
        }
        /* Fail fast if the port dies mid-wait (USB unplug, device removal) —
         * the port implementation's send/recv error path invalidates the
          * handle, so the wait ends with PORT_CLOSED instead of running out
          * the full timeout_ms only to report TIMEOUT. */
        if (!kw307_port_is_open()) {
            s_read.waiting_cmd = 0u;
            return KW307_ERR_PORT_CLOSED;
        }
    }

    s_read.waiting_cmd = 0u;
    return KW307_ERR_TIMEOUT;
}


/* ================================================================
   Blocking write - synchronous Write + ACK
   ----------------------------------------------------------------
   The ACK carries only [CMD][STATUS], so it is matched to the write by
   order alone. Two rules keep that sound:
     1. Arm before send, so a fast ACK cannot be missed.
     2. After a timeout, keep polling briefly and discard a late ACK, so it
        is not taken as the next write's answer.
   ================================================================ */

/* Extra grace, in ms, spent swallowing a late ACK after a timeout (rule 2). */
#define KW307_WRITE_DRAIN_MS  30

int kw307_write_blocking(uint8_t cmd,
                         const uint8_t* data, uint16_t len,
                         int timeout_ms)
{
    KW307Wait_t w;

    if (cmd == 0u)                return KW307_ERR_PARAM;
    if (len > 0 && !data)         return KW307_ERR_PARAM;
    if (timeout_ms <= 0)          return KW307_ERR_PARAM;
    if (!kw307_port_is_open())    return KW307_ERR_PORT_CLOSED;

    /* Rule 1: arm, then send. */
    s_write.got_ack     = 0u;
    s_write.ack_status  = 0u;
    s_write.waiting_cmd = cmd;

    if (kw307_uart_send_write(&s_uart, cmd, data, len) != 0) {
        s_write.waiting_cmd = 0u;
        return KW307_ERR_PORT_SEND;
    }

    wait_begin(&w, timeout_ms);
    while (wait_step(&w)) {
        if (s_write.got_ack) {
            uint8_t st = s_write.ack_status;
            s_write.waiting_cmd = 0u;
            return (st == KW307_STATUS_OK) ? KW307_OK : KW307_ERR_NACK;
        }
        /* Fail fast if the port dies mid-wait (USB unplug), same as the read
         * path — no point waiting out the timeout to report the wrong reason. */
        if (!kw307_port_is_open()) {
            s_write.waiting_cmd = 0u;
            return KW307_ERR_PORT_CLOSED;
        }
    }

    /* Rule 2: the ACK may be a few ms behind. Keep the slot armed so on_frame
     * still recognises it, and throw it away rather than let the next write
     * inherit it. A late ACK found here is NOT promoted to success: the caller
     * has already been kept waiting the full budget, and reporting a timeout it
     * can act on beats a success that arrived after the deadline. */
    wait_begin(&w, KW307_WRITE_DRAIN_MS);
    while (kw307_port_is_open() && wait_step(&w)) {
        if (s_write.got_ack) break;
    }
    s_write.waiting_cmd = 0u;
    s_write.got_ack     = 0u;
    return KW307_ERR_TIMEOUT;
}


/* ================================================================
   Write-verify mode - every SDK write passes through here
   ----------------------------------------------------------------
   With verify off this is a plain send. With it on, each write waits for its
   ACK and is counted; after the first timeout the rest of the batch is sent
   without waiting, until kw307_write_stats_reset().
   ================================================================ */

void kw307_set_write_verify(int timeout_ms, KW307WriteResultCb cb)
{
    s_verify.timeout_ms = (timeout_ms > 0) ? timeout_ms : 0;
    s_verify.cb         = cb;
    s_verify.degraded   = 0u;
}

void kw307_write_stats_reset(void)
{
    memset(&s_verify.stats, 0, sizeof(s_verify.stats));
    s_verify.degraded = 0u;
}

void kw307_write_stats_get(KW307WriteStats_t* out)
{
    if (out) *out = s_verify.stats;
}

/* Write-verify is on and still waiting for ACKs - i.e. a refused write will
 * be seen, and stop a multi-write call. */
int kw307_i_write_verify_on(void)
{
    return (s_verify.timeout_ms != 0 && !s_verify.degraded) ? 1 : 0;
}

int kw307_write_checked(uint8_t cmd, const uint8_t* data, uint16_t len)
{
    int rc;

    if (s_verify.timeout_ms == 0) {                 /* verify off — original path */
        return kw307_uart_send_write(&s_uart, cmd, data, len);
    }

    s_verify.stats.sent++;

    if (s_verify.degraded) {                        /* stop waiting, keep sending */
        s_verify.stats.unverified++;
        return kw307_uart_send_write(&s_uart, cmd, data, len);
    }

    rc = kw307_write_blocking(cmd, data, len, s_verify.timeout_ms);

    if      (rc == KW307_OK)            s_verify.stats.verified++;
    else if (rc == KW307_ERR_NACK)      s_verify.stats.rejected++;
    else if (rc == KW307_ERR_TIMEOUT) { s_verify.stats.timed_out++; s_verify.degraded = 1u; }

    /* Report first, so the callback sees failures in wire order. */
    if (rc != KW307_OK && s_verify.cb)
        s_verify.cb(cmd, rc, (rc == KW307_ERR_NACK) ? kw307_last_nack_status() : 0u);

    /* A timeout is counted but not returned, so a setter's remaining writes
     * still go out. A refusal is returned. */
    if (rc == KW307_ERR_TIMEOUT) return 0;

    return rc;
}


/* ================================================================
   Internal frame router - dispatches incoming output frames to the
   registered callback, and captures blocking-read responses.
   ================================================================ */

static void on_frame(uint8_t sof2, uint8_t cmd,
                     const uint8_t* data, uint16_t len,
                     void* user)
{
    (void)user;

    /* --- Output frames (AA 55) ---
     * Dispatched by wire CMD byte. Each frame type only fires its own
     * callback; an unregistered callback simply drops the frame. */
    if (cmd == KW307_CMD_OUT_FRAME) {
        KW307Frame_t frame;
        if (kw307_dec_frame(data, len, &frame) == 0 && s_cb_frame) {
            s_cb_frame(&frame);
        }
        return;
    }
    /* --- Write ACKs (AA 55 AC): capture for kw307_write_blocking ---
     * Checked before the output-handler registry below because CMD 0xAC is the
     * ACK opcode, never a periodic output, so no feature module can claim it.
     * Unlike the read path this latches SUCCESS too: a write has no data frame
     * behind it, so the ACK is the whole response and an OK one is what the
     * caller is waiting for. */
    if (sof2 == KW307_SOF_WRITE && cmd == KW307_CMD_ACK && len >= 2u) {
        if (s_write.waiting_cmd != 0u && data[0] == s_write.waiting_cmd) {
            s_write.ack_status = data[1];
            s_write.got_ack    = 1u;
            if (data[1] != KW307_STATUS_OK) s_read.ack_status = data[1];  /* kw307_last_nack_status() */
        }
        return;   /* an unawaited ACK (late arrival from a timed-out write) is dropped here */
    }

    /* --- Output frames (AA 55) claimed by an optional feature module via
     *     kw307_register_output_handler. Base io stays unaware of them. --- */
    if (sof2 == KW307_SOF_WRITE) {
        for (int i = 0; i < KW307_MAX_OUTPUT_HANDLERS; i++) {
            if (s_out[i].cmd == cmd && s_out[i].h) {
                s_out[i].h(cmd, data, len);
                return;
            }
        }
    }

    /* --- Read responses / ACKs (AA 56): capture for kw307_read_blocking --- */
    if (sof2 == KW307_SOF_READ && s_read.waiting_cmd != 0u) {
        if (cmd == KW307_CMD_ACK && len >= 2u) {
            /* ACK for read: data[0]=acked_cmd, data[1]=status.
             * Success ACK (status=0) is followed by a separate data frame;
             * failure ACK means no data frame will arrive. Latch the status
             * byte so the caller can fetch it via kw307_last_nack_status(). */
            if (data[0] == s_read.waiting_cmd && data[1] != KW307_STATUS_OK) {
                s_read.ack_status   = data[1];
                s_read.got_ack_fail = 1u;
            }
        } else if (cmd == s_read.waiting_cmd) {
            uint16_t n = (len < KW307_READ_BUF_MAX) ? len : KW307_READ_BUF_MAX;
            for (uint16_t i = 0; i < n; i++) s_read.resp_buf[i] = data[i];
            s_read.resp_len     = n;
            s_read.got_response = 1u;
        }
    }
}
