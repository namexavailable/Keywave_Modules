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
 * KW307 SDK - UART ISR Hook Template (ISR-driven RX only)
 * File: example_platform/port_template/user_isr.c
 *
 * * DELETE this file if your platform uses polling RX *
 *   (e.g. Arduino HardwareSerial, STM32 HAL polling, ESP-IDF blocking read).
 *
 * For ISR-driven RX (preferred on bare-metal MCUs), the platform's UART
 * RX interrupt vector must drain the hardware FIFO into the SDK's ring
 * buffer by calling kw307_port_isr_feed() for each received byte.
 *
 * STEPS
 *   1. Rename the function to match your platform's IRQ vector name
 *      (e.g. UART0_IRQHandler / USART1_IRQHandler / IRQ_HANDLER_UART2).
 *   2. Replace the UART_HAS_DATA / UART_READ macros below with your
 *      vendor SDK's equivalents.
 *   3. Enable the interrupt in your application's main() *before* calling
 *      kw307_init() (e.g. NVIC_EnableIRQ(UART1_IRQn) on Cortex-M).
 *   4. Uncomment the `kw307_port_isr_feed()` declaration in port_uart.h.
 */

#include "port_uart.h"
#include "user_config.h"


/* ----------------------------------------------------------------
   UART RX Interrupt Handler
   * Rename function to match the IRQ vector for the UART peripheral
   * selected in user_config.h (KW307_UART_PERIPH).
   ---------------------------------------------------------------- */
void TODO_RENAME_UARTx_IRQHandler(void)
{
    /* TODO: drain the hardware RX FIFO into the SDK ring buffer.
     *
     * Generic pattern:
     *
     *   while (UART_HAS_RX_DATA(KW307_UART_PERIPH)) {
     *       uint8_t b = UART_READ_BYTE(KW307_UART_PERIPH);
     *       kw307_port_isr_feed(b);   // or signature your port_uart.c expects
     *   }
     *
     * Vendor-specific snippets:
     *   NuMicro M251 :  while (!UART_GET_RX_EMPTY(UART1))   kw307_port_isr_feed();
     *   STM32 HAL    :  HAL_UART_RxCpltCallback() routes here (one byte at a time)
     *   ESP-IDF      :  uart_isr_register() with custom ISR
     *   Bare ARM     :  read from UART->DR / UART->RDR / UART->RBR per vendor
     */

    /* TODO: implement the FIFO drain above. */
}
