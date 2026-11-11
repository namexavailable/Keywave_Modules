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
 * KW307 SDK - User Configuration Template
 * File: example_platform/port_template/user_config.h
 *
 * * Edit this file to match your hardware. *
 *
 * port_uart.c reads these macros at compile time. Rebuild after editing.
 * No other SDK files need to change.
 */

#ifndef KW307_USER_CONFIG_H
#define KW307_USER_CONFIG_H

/* ----------------------------------------------------------------
   1. UART peripheral selector
   ----------------------------------------------------------------
   Pick which UART instance on your MCU drives the KW307 host link.
   The actual macro name / type depends on your vendor SDK.

   Examples:
     STM32 HAL    : #define KW307_UART_HANDLE   huart1   (UART_HandleTypeDef)
     NuMicro      : #define KW307_UART_PERIPH   UART1
     ESP-IDF      : #define KW307_UART_NUM      UART_NUM_1
     bare-metal   : #define KW307_UART_BASE     0x40004400u

   TODO: replace with your platform's identifier.
   ---------------------------------------------------------------- */
#define KW307_UART_PERIPH       /* TODO: e.g. UART1 */


/* ----------------------------------------------------------------
   2. Baud rate (must match MCU firmware setting — 115200 by default)
   ---------------------------------------------------------------- */
#define KW307_UART_BAUD         115200


/* ----------------------------------------------------------------
   3. RX ring buffer size (ISR-driven platforms only)
   ----------------------------------------------------------------
   * MUST be a power of 2: 64 / 128 / 256 / 512 / ...
     The buffer uses bitwise masking (index & (BUFSIZE - 1)) for wrap,
     which only works for exact powers of 2. Non-power-of-2 will
     silently corrupt received data.
   Default 256 covers the largest output frame (~205 bytes on the
   wire) with margin. Bump to 512 if your main loop cadence can lag
   a full frame behind.
   ---------------------------------------------------------------- */
#define KW307_UART_RX_BUFSIZE   256u


/* ----------------------------------------------------------------
   4. Pin / GPIO assignments (platform-specific)
   ----------------------------------------------------------------
   TODO: add macros for TX / RX pin assignments if your port_uart.c
   needs to configure GPIO MFP / alternate functions itself.

   Example (NuMicro):
     #define KW307_UART_TX_PIN_MFP   SYS_GPB_MFPL_PB1MFP_UART1_TXD
     #define KW307_UART_RX_PIN_MFP   SYS_GPB_MFPL_PB0MFP_UART1_RXD

   Leave empty if your platform configures pins elsewhere (e.g.
   CubeMX-generated MX_USART_UART_Init for STM32, or Arduino's
   automatic pin routing).
   ---------------------------------------------------------------- */


#endif /* KW307_USER_CONFIG_H */
