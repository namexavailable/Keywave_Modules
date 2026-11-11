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
 * KW307 SDK - Raspberry Pi Pico Platform Implementation (pico-sdk UART)
 * File: example_platform/RPi_Pico/port_pico.c
 *
 * Implements the 5 functions declared in core/KW307_port.h, so every other SDK
 * layer compiles unchanged. RP2040 (Pico / Pico W) and RP2350 (Pico 2).
 *
 * RX is interrupt-driven into a ring buffer — the same shape as the Nuvoton M251
 * reference port. The main loop can therefore spend time drawing, blinking or
 * talking to a network without losing radar bytes, as long as it calls
 * kw307_update() often enough to drain the ring.
 *
 * UART, pins, baud and ring size are set in user_config.h.
 *
 * Build: pico-sdk, C11 (the SDK sources are C99 and compile fine under it).
 *        Link libraries: pico_stdlib hardware_uart hardware_irq
 */

#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"

#include "KW307_port.h"      /* add <sdk>/core to the include path */
#include "user_config.h"

/* The ring index wraps with a mask, so the size must be a power of two.
 * RP2040 is Cortex-M0+ with no divide instruction: a '%' here would pull in a
 * software division routine and run in the interrupt handler. */
#if (KW307_UART_RX_BUFSIZE & (KW307_UART_RX_BUFSIZE - 1u)) != 0u
#error "KW307_UART_RX_BUFSIZE must be a power of two"
#endif
#define RX_MASK (KW307_UART_RX_BUFSIZE - 1u)

/* ================================================================
   Internal state
   ================================================================ */
static volatile uint8_t  s_rx[KW307_UART_RX_BUFSIZE];
static volatile uint16_t s_head;      /* written by the ISR only  */
static volatile uint16_t s_tail;      /* written by the main loop only */
static bool              s_is_open;
static int               s_irq;

/* ================================================================
   UART RX interrupt — push every arriving byte into the ring.
   ================================================================ */
static void kw307_uart_isr(void)
{
    while (uart_is_readable(KW307_UART_ID)) {
        uint8_t  b    = (uint8_t)uart_getc(KW307_UART_ID);
        uint16_t next = (uint16_t)((s_head + 1u) & RX_MASK);

        if (next != s_tail) {            /* room: store it */
            s_rx[s_head] = b;
            s_head       = next;
        }
        /* Full: drop the byte. Dropping is the right call — the alternative is
         * overwriting unread bytes, which corrupts a frame the parser is part
         * way through. A dropped byte fails that frame's CRC and the parser
         * resynchronises on the next header. If this happens at all, the main
         * loop is not calling kw307_update() often enough. */
    }
}

/* ================================================================
   1. kw307_port_open
      port_name is ignored (the UART is fixed in user_config.h).
      baud_rate <= 0 means "use the user_config.h default".
   ================================================================ */
int kw307_port_open(const char* port_name, int baud_rate)
{
    (void)port_name;

    if (s_is_open) kw307_port_close();       /* re-open is allowed */

    if (baud_rate <= 0) baud_rate = KW307_DEFAULT_BAUD_RATE;

    s_head = 0;
    s_tail = 0;

    uart_init(KW307_UART_ID, (uint)baud_rate);
    gpio_set_function(KW307_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(KW307_UART_RX_PIN, GPIO_FUNC_UART);

    /* 8N1, no flow control. The KW307 protocol is binary, so there is nothing
     * to translate and no handshaking to honour. */
    uart_set_format(KW307_UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(KW307_UART_ID, false, false);

    /* FIFO OFF, deliberately: with it on, the RX interrupt only fires once the
     * FIFO passes a threshold, so the last few bytes of a burst can sit there
     * until more data arrives. Off means one interrupt per byte — about 540 a
     * second at 115200 with this frame rate, which is nothing for a 125 MHz
     * core, and it removes a whole class of "the last frame arrives late"
     * behaviour. */
    uart_set_fifo_enabled(KW307_UART_ID, false);

    s_irq = (KW307_UART_ID == uart0) ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(s_irq, kw307_uart_isr);
    irq_set_enabled(s_irq, true);
    uart_set_irq_enables(KW307_UART_ID, true, false);   /* RX only, no TX irq */

    s_is_open = true;
    return 0;
}

/* ================================================================
   2. kw307_port_close
   ================================================================ */
void kw307_port_close(void)
{
    if (!s_is_open) return;

    uart_set_irq_enables(KW307_UART_ID, false, false);
    irq_set_enabled(s_irq, false);
    irq_remove_handler(s_irq, kw307_uart_isr);
    uart_deinit(KW307_UART_ID);

    s_head    = 0;
    s_tail    = 0;
    s_is_open = false;
}

/* ================================================================
   3. kw307_port_send
      Blocking TX is fine per the HAL contract, and simplest here.
   ================================================================ */
int kw307_port_send(const uint8_t* buf, int len)
{
    if (!s_is_open || !buf || len <= 0) return -1;

    uart_write_blocking(KW307_UART_ID, buf, (size_t)len);
    return len;
}

/* ================================================================
   4. kw307_port_recv
      timeout_ms = 0 : return whatever the ring holds now
      timeout_ms > 0 : wait up to timeout_ms for at least 1 byte
   ================================================================ */
int kw307_port_recv(uint8_t* buf, int max_len, int timeout_ms)
{
    int count = 0;

    if (!s_is_open || !buf || max_len <= 0) return -1;

    if (timeout_ms > 0 && s_head == s_tail) {
        absolute_time_t deadline = make_timeout_time_ms((uint32_t)timeout_ms);
        while (s_head == s_tail && !time_reached(deadline))
            tight_loop_contents();       /* the ISR fills the ring meanwhile */
    }

    /* Drain. Only the main loop moves s_tail, so no critical section is needed:
     * the ISR writes s_head and reads s_tail, we write s_tail and read s_head,
     * and both are single 16-bit accesses. */
    while (count < max_len && s_tail != s_head) {
        buf[count++] = s_rx[s_tail];
        s_tail       = (uint16_t)((s_tail + 1u) & RX_MASK);
    }
    return count;
}

/* ================================================================
   5. kw307_port_is_open
   ================================================================ */
int kw307_port_is_open(void)
{
    return s_is_open ? 1 : 0;
}
