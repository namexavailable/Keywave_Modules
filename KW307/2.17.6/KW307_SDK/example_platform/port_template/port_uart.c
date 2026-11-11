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
 * KW307 SDK - Platform Port Template
 * File: port_template/port_template.c
 *
 * Skeleton for porting the SDK to a new platform. Fill in the 5 functions
 * below; the upper SDK layers (KW307_uart.c / KW307_api.c) stay unchanged
 * and call these through the common API defined in core/KW307_port.h.
 *
 * STEPS
 *   1. Copy this file to a new folder, e.g.  mcu/<yourboard>/port_<yourboard>.c
 *   2. Create user_config.h in the same folder with any macros you need
 *      (UART peripheral handle, baud rate, pin numbers, etc.).
 *   3. Implement the TODO sections below. Keep the function signatures
 *      exactly as declared in core/KW307_port.h.
 *   4. Add your new .c to the build and add the folder to the include path.
 *      No other SDK files need changes.
 *
 * REFERENCE IMPLEMENTATIONS (good starting points to read):
 *   example_platform/Nuvoton_M251/port_uart.c               - MCU, ISR-driven ring buffer
 *   example_platform/Arduino/port_arduino.cpp - Arduino HardwareSerial
 *   pc/windows/port_win32.c            - Windows Win32 COM
 */

#include <stdint.h>
#include "KW307_port.h"      /* add KW307_SDK/core to include path */
#include "user_config.h"     /* your per-project macros */

/* ================================================================
   Internal state (example - adjust to your platform)
   ================================================================ */
static uint8_t s_is_open = 0;
/* TODO: add handle / ring buffer / anything else your platform needs */


/* ================================================================
   1. kw307_port_open
      Called once by kw307_init(). Configure and open the UART.
      port_name : ignored on MCU (use user_config.h macros instead).
                  PC : "COM3" / "/dev/ttyUSB0" - use if non-NULL.
      baud_rate : typically 115200; 0 means "use user_config.h default".
      Returns   : 0 on success, negative on error.
   ================================================================ */
int kw307_port_open(const char* port_name, int baud_rate)
{
    (void)port_name;
    (void)baud_rate;

    /* TODO: initialise clocks / pins / UART peripheral */
    /* TODO: enable RX interrupt (if using ISR-driven RX) */

    s_is_open = 1;
    return 0;
}


/* ================================================================
   2. kw307_port_close
      Disable the UART and release resources.
   ================================================================ */
void kw307_port_close(void)
{
    /* TODO: disable RX interrupt, close peripheral */
    s_is_open = 0;
}


/* ================================================================
   3. kw307_port_send
      Transmit `len` bytes. Blocking TX is fine.
      Returns bytes sent (>=0), or negative on error.
   ================================================================ */
int kw307_port_send(const uint8_t* buf, int len)
{
    if (!s_is_open || len <= 0 || !buf) return -1;

    /* TODO: write `len` bytes from `buf` to your UART.
     *       Block until the FIFO is drained if you care about ordering. */

    return len;
}


/* ================================================================
   4. kw307_port_recv
      timeout_ms = 0 : non-blocking, return whatever is available now.
      timeout_ms > 0 : wait up to timeout_ms ms for at least 1 byte.
      Returns bytes received (0 = timeout), negative on error.
   ================================================================ */
int kw307_port_recv(uint8_t* buf, int max_len, int timeout_ms)
{
    if (!s_is_open || max_len <= 0 || !buf) return -1;

    int count = 0;
    (void)timeout_ms;

    /* TODO: drain up to `max_len` bytes from your RX source into `buf`.
     *       MCU pattern : pop from an ISR-filled ring buffer.
     *       PC  pattern : call ReadFile / read() with timeout. */

    return count;
}


/* ================================================================
   5. kw307_port_is_open
      Returns non-zero if the port is currently open.
   ================================================================ */
int kw307_port_is_open(void)
{
    return s_is_open ? 1 : 0;
}


/* ================================================================
   OPTIONAL - ISR hook (MCU only)
   ----------------------------------------------------------------
   If you use ISR-driven RX, expose a feed function here and call it
   from your platform's UART interrupt handler. See example_platform/Nuvoton_M251/port_uart.c
   + example_platform/Nuvoton_M251/user_isr.c for the M251 reference.

   void kw307_port_isr_feed(void)
   {
       uint8_t b = UART_READ(...);
       // push b into your ring buffer
   }
   ================================================================ */
