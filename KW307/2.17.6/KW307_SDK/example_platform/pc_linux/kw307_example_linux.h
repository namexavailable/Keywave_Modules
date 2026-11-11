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
 * KW307 SDK - Linux host example helpers (NOT part of the SDK core)
 * File: example_platform/pc_linux/kw307_example_linux.h
 *
 * These two convenience functions live in port_linux.c. They are OUTSIDE the
 * KW307_port.h HAL (the 5 required functions kw307_port_open/close/send/recv/
 * is_open) — a host app that shows a device picker usually wants them, an MCU
 * port never does.
 *
 * Include this header from your Linux host app to call the helpers without
 * hand-writing extern declarations. C and C++ safe.
 *
 * Typical host startup:
 *     char devs[16][KW307_EXAMPLE_PORT_NAME_MAX];
 *     int  n = kw307_example_list_ports(devs, 16);   // fill a list / menu
 *     kw307_example_set_port(devs[sel], 115200);     // the user's pick + baud
 *     kw307_init();                                  // opens that device
 */
#ifndef KW307_EXAMPLE_LINUX_H
#define KW307_EXAMPLE_LINUX_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Slot width for kw307_example_list_ports(). Wide enough for a full device path
 * such as "/dev/ttyUSB10" — Linux device names have no fixed length, unlike the
 * Windows port's "COMx", so this is a named limit rather than a bare 16.
 */
#define KW307_EXAMPLE_PORT_NAME_MAX 32

/**
 * Select which device / baud kw307_port_open(NULL, 0) will use.
 * Call BEFORE kw307_init(). Resolution order inside kw307_port_open:
 *   explicit arg > this runtime selection > user_config.h default.
 *
 * @param name  Device path, e.g. "/dev/ttyUSB0" (NULL or "" clears it).
 * @param baud  Baud rate, e.g. 115200 (<= 0 keeps the user_config.h default).
 */
void kw307_example_set_port(const char* name, int baud);

/**
 * List the serial devices currently present, as full paths. Scans /dev for the
 * prefixes that actually carry serial ports, in this order: `serial*` (the
 * distro-maintained symlink, e.g. Raspberry Pi's /dev/serial0), `ttyUSB*` (USB
 * bridges), `ttyACM*` (USB CDC), `ttyAMA*` (an SoC's own UART). The order is
 * deterministic, so an index into the result means the same thing between runs.
 *
 * Presence in the list does NOT mean the device is a KW307, or even that you may
 * open it — check that you are in the `dialout` group if open fails with
 * EACCES.
 *
 * @param out  Caller array of name slots, each KW307_EXAMPLE_PORT_NAME_MAX bytes.
 * @param max  Capacity of out (number of slots).
 * @return     Number of devices written (0 if none).
 */
int kw307_example_list_ports(char out[][KW307_EXAMPLE_PORT_NAME_MAX], int max);

#ifdef __cplusplus
}
#endif

#endif /* KW307_EXAMPLE_LINUX_H */
