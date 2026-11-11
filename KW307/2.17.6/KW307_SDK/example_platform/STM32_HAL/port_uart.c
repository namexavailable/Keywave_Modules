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
 * KW307 SDK - STM32 HAL UART Platform Implementation
 * File: example_platform/STM32_HAL/port_uart.c
 *
 * Implements the 5 global functions declared in KW307_port.h using the
 * standard STM32 HAL UART driver in interrupt mode (1 byte per IRQ).
 *
 * TX : HAL_UART_Transmit() with timeout (blocking).
 * RX : HAL_UART_Receive_IT() + HAL_UART_RxCpltCallback() re-arming loop.
 *
 * Works on any STM32 family (F0/F1/F3/F4/F7/G0/G4/H7/L0/L4/U5) that uses
 * the HAL driver. The UART peripheral must be initialised by CubeMX-
 * generated code (MX_USARTx_UART_Init()) BEFORE kw307_init() is called.
 *
 * References:
 *   - ST HAL UART FAQ:  https://community.st.com/t5/stm32-mcus/faq-stm32-hal-uart-driver-api-and-callbacks/ta-p/49301
 *   - CubeMX user guide UM1718 chapter "UART configuration"
 */

#include <stdint.h>
#include <string.h>
#include "KW307_port.h"     /* add KW307_SDK/core to include path */
#include "user_config.h"
#include "port_uart.h"

/* ================================================================
   Ring buffer (power-of-2 size, lock-free single-producer/consumer)
   ================================================================ */
#ifndef KW307_UART_RX_BUFSIZE
#define KW307_UART_RX_BUFSIZE   128u
#endif

#define RX_MASK  ((uint16_t)(KW307_UART_RX_BUFSIZE - 1u))

static uint8_t           s_rx_buf[KW307_UART_RX_BUFSIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;

static uint8_t  s_rx_byte;          /* 1-byte landing pad for HAL_UART_Receive_IT */
static uint8_t  s_is_open;


/* ================================================================
   Push one byte into the ring buffer (called from HAL RX callback)
   ================================================================ */
void kw307_port_isr_feed_byte(uint8_t byte)
{
    if ((uint16_t)(s_rx_head - s_rx_tail) < KW307_UART_RX_BUFSIZE) {
        s_rx_buf[s_rx_head & RX_MASK] = byte;
        s_rx_head++;
    }
    /* Buffer full: byte dropped. Increase KW307_UART_RX_BUFSIZE if needed. */
}


/* ================================================================
   HAL weak override: called by HAL after each 1-byte RX completes.
   Pushes the byte to the ring buffer and re-arms the next RX IT.
   ================================================================ */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == KW307_STM32_UART_HANDLE) {
        kw307_port_isr_feed_byte(s_rx_byte);
        (void)HAL_UART_Receive_IT(huart, &s_rx_byte, 1);
    }
}


/* ================================================================
   kw307_port_open
   CubeMX has already done HAL_UART_Init(); we just arm the RX IT.
   ================================================================ */
int kw307_port_open(const char* port_name, int baud_rate)
{
    (void)port_name;   /* not used: UART is selected in user_config.h */
    (void)baud_rate;   /* not used: baud is set by CubeMX MX_USARTx_UART_Init() */

    s_rx_head = 0;
    s_rx_tail = 0;

    /* Arm the first 1-byte RX interrupt. HAL_UART_RxCpltCallback will
     * re-arm itself from then on. */
    if (HAL_UART_Receive_IT(KW307_STM32_UART_HANDLE, &s_rx_byte, 1) != HAL_OK)
        return -1;

    s_is_open = 1u;
    return 0;
}


/* ================================================================
   kw307_port_close
   ================================================================ */
void kw307_port_close(void)
{
    HAL_UART_AbortReceive(KW307_STM32_UART_HANDLE);
    s_rx_head = 0;
    s_rx_tail = 0;
    s_is_open = 0u;
}


/* ================================================================
   kw307_port_send
   Blocking TX with a 100 ms per-call timeout. len is small (< 300 B).
   ================================================================ */
int kw307_port_send(const uint8_t* buf, int len)
{
    if (!s_is_open || len <= 0 || !buf) return -1;

    if (HAL_UART_Transmit(KW307_STM32_UART_HANDLE,
                          (uint8_t*)buf, (uint16_t)len, 100) != HAL_OK)
        return -1;

    return len;
}


/* ================================================================
   kw307_port_recv
   Drain ring buffer into caller's buf. Supports simple timeout.
   ================================================================ */
int kw307_port_recv(uint8_t* buf, int max_len, int timeout_ms)
{
    int count = 0;

    if (!s_is_open || max_len <= 0 || !buf) return -1;

    if (timeout_ms > 0) {
        uint32_t start = HAL_GetTick();
        while ((uint16_t)(s_rx_head - s_rx_tail) == 0) {
            if ((HAL_GetTick() - start) >= (uint32_t)timeout_ms) break;
        }
    }

    while (count < max_len && (uint16_t)(s_rx_head - s_rx_tail) > 0) {
        buf[count++] = s_rx_buf[s_rx_tail & RX_MASK];
        s_rx_tail++;
    }
    return count;
}


/* ================================================================
   kw307_port_is_open
   ================================================================ */
int kw307_port_is_open(void)
{
    return s_is_open ? 1 : 0;
}
