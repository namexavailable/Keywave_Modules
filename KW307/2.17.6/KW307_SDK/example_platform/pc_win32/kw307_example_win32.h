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
 * KW307 SDK - Windows PC example helpers (NOT part of the SDK core)
 * File: example_platform/pc_win32/kw307_example_win32.h
 *
 * These two convenience functions live in port_win32.c. They are OUTSIDE the
 * KW307_port.h HAL (the 5 required functions kw307_port_open/close/send/recv/
 * is_open) — a PC host app that shows a COM-port picker usually wants them, an
 * MCU port never does.
 *
 * Include this header from your PC host app (console or GUI) to call the helpers
 * without hand-writing extern declarations. C and C++ (incl. C++ Builder) safe.
 *
 * Typical PC host startup:
 *     char ports[16][16];
 *     int  n = kw307_example_list_ports(ports, 16);   // fill a combo box
 *     kw307_example_set_port(ports[sel], 921600);      // user's pick + baud
 *     kw307_init();                                     // opens the selected port
 */
#ifndef KW307_EXAMPLE_WIN32_H
#define KW307_EXAMPLE_WIN32_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Select which COM port / baud kw307_port_open(NULL, 0) will use.
 * Call BEFORE kw307_init(). Resolution order inside kw307_port_open:
 *   explicit arg > this runtime selection > user_config.h default.
 *
 * @param name  Port name, e.g. "COM7" (NULL or "" clears the selection).
 * @param baud  Baud rate, e.g. 921600 (<= 0 keeps the user_config.h default).
 */
void kw307_example_set_port(const char* name, int baud);

/**
 * Enumerate the COM ports currently present (from
 * HKLM\HARDWARE\DEVICEMAP\SERIALCOMM). Fills out[0..count-1] with
 * NUL-terminated "COMx" strings.
 *
 * @param out  Caller array of fixed-width name slots (each holds "COMx" + NUL).
 * @param max  Capacity of out (number of slots).
 * @return     Number of ports written (0 if none / registry key absent).
 */
int kw307_example_list_ports(char out[][16], int max);

#ifdef __cplusplus
}
#endif

#endif /* KW307_EXAMPLE_WIN32_H */
