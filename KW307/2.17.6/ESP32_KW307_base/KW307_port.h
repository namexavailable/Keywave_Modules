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
 * KW307 SDK - Port layer
 * File: KW307_port.h
 *
 * Declares the 5 port functions that every port implementation must provide.
 * This is the ONLY platform-dependent interface in the SDK.
 *
 * To port to a new platform:
 *   1. Copy port_template/port_template.c to <your_platform>/port_xxx.c
 *   2. Implement the 5 TODO sections (same signatures as declared below).
 *   3. Add that .c file to your build - no other SDK files change.
 *   See port_template/README.md for the full checklist.
 *
 * Fault detection responsibility (REQUIRED):
 *   When kw307_port_send() or kw307_port_recv() detects a permanent transport
 *   failure (USB unplug, device removal, OS revoking the handle, etc.), the
 *   port implementation MUST close the underlying handle and update its
 *   internal state so a subsequent kw307_port_is_open() returns 0. Upper
 *   layers (kw307_read_blocking, kw307_update, application reconnect logic)
 *   trust kw307_port_is_open() to reflect reality; if the handle is left
 *   valid after a hard failure, the SDK will hang or report stale state.
 *
 * Available port implementations (compile as-is; only touch when porting
 * to a new platform or customising low-level UART behaviour):
 *   pc/windows/port_win32.c       Windows COM port
 *   example_platform/Nuvoton_M251/port_uart.c          NuMicro M251 UART (ISR ring buffer)
 *   example_platform/Arduino/port_arduino.cpp  Arduino HardwareSerial (any core)
 *
 * User configuration (* files you edit every project):
 *   pc/windows/user_config.h       COM port name / baud rate defaults
 *   example_platform/Nuvoton_M251/user_config.h         UART peripheral / baud rate / RX buffer size
 *   example_platform/Nuvoton_M251/user_isr.c            UART IRQ handler name
 *   example_platform/Arduino/user_config.h  Baud rate default
 */

#ifndef KW307_PORT_H
#define KW307_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Open and configure the UART.
 *
 * port_name : platform-specific identifier.
 *             PC  : "COM3", "\\\\.\\COM10", "/dev/ttyUSB0"
 *             MCU : pass NULL - peripheral is set in user_config.h
 * baud_rate : baud rate, typically 115200.
 * Returns   : 0 = success, negative = error.
 */
int kw307_port_open(const char* port_name, int baud_rate);

/**
 * Close the port and release all resources.
 */
void kw307_port_close(void);

/**
 * Transmit len bytes from buf. Blocking TX is acceptable.
 * Returns : bytes actually sent (>= 0), or negative on error.
 */
int kw307_port_send(const uint8_t* buf, int len);

/**
 * Receive up to max_len bytes into buf.
 *
 * timeout_ms = 0  : non-blocking, return whatever is available now.
 * timeout_ms > 0  : block up to timeout_ms ms waiting for >= 1 byte.
 * Returns : bytes received (0 = timeout / no data), negative = error.
 */
int kw307_port_recv(uint8_t* buf, int max_len, int timeout_ms);

/**
 * Query whether the port is currently usable.
 * Returns : non-zero = open, 0 = closed.
 */
int kw307_port_is_open(void);

#ifdef __cplusplus
}
#endif

#endif /* KW307_PORT_H */
