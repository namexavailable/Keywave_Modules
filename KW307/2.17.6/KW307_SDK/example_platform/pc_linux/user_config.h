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
 * KW307 SDK - Linux User Configuration
 * File: example_platform/pc_linux/user_config.h
 *
 * *** Edit this file to match your hardware. ***
 *
 * Usage:
 *   kw307_init();   // automatically reads KW307_DEFAULT_PORT_NAME / BAUD_RATE
 *
 * These are only the fallback defaults. A host app with a device picker calls
 * kw307_example_set_port() first (see kw307_example_linux.h), and an explicit
 * kw307_port_open("/dev/ttyUSB0", 115200) overrides both.
 */

#ifndef KW307_USER_CONFIG_PC_LINUX_H
#define KW307_USER_CONFIG_PC_LINUX_H

/* ----------------------------------------------------------------
   Serial device

     /dev/ttyUSB0   USB-serial bridge (FTDI, CP210x, CH340) — the usual case
                    on a PC or IPC.
     /dev/ttyACM0   USB CDC device.
     /dev/serial0   an SoC's own UART pins. On a Raspberry Pi this is a symlink
                    the OS maintains to whichever UART is currently primary, so
                    prefer it over ttyAMA0 / ttyS0 — the same name then works on
                    every Pi model.

   Find yours:   ls -l /dev/serial* /dev/ttyUSB* /dev/ttyACM*
   or watch it appear:   dmesg -w    (then plug the adapter in)
   ---------------------------------------------------------------- */
#define KW307_DEFAULT_PORT_NAME   "/dev/ttyUSB0"

/* ----------------------------------------------------------------
   Baud rate — must match the KW307 module's firmware setting
   (typically 115200).
   ---------------------------------------------------------------- */
#define KW307_DEFAULT_BAUD_RATE   115200

#endif /* KW307_USER_CONFIG_PC_LINUX_H */
