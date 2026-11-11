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
 * KW307 SDK - Platform Port Template Header
 * File: example_platform/port_template/port_uart.h
 *
 * Companion to port_uart.c. Declare anything that port_uart.c exposes to
 * the rest of your project here. The 5 standard port functions are
 * already declared in core/KW307_port.h — do NOT re-declare them.
 *
 * The most common addition is an ISR-feed function for ISR-driven RX
 * (see Nuvoton_M251 reference). If your platform uses polling RX
 * (Arduino HardwareSerial, STM32 HAL with HAL_UART_Receive timeout),
 * you can leave this header empty.
 */

#ifndef PORT_UART_H
#define PORT_UART_H

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------
   ISR-feed (ISR-driven RX platforms only)
   ----------------------------------------------------------------
   Uncomment if your port_uart.c uses an ISR-fed ring buffer pattern.
   Then call this from your platform's UART RX interrupt for each byte:

       void UART1_IRQHandler(void) {
           while (UART_HAS_DATA(...))
               kw307_port_isr_feed();
       }

   See example_platform/Nuvoton_M251/user_isr.c for the working pattern.
   ---------------------------------------------------------------- */
/* void kw307_port_isr_feed(void); */

#ifdef __cplusplus
}
#endif

#endif /* PORT_UART_H */
