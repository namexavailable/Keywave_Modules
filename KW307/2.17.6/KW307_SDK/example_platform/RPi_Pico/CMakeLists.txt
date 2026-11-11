# Copyright 2026 Keywave Technology Ltd
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# KW307 SDK - Raspberry Pi Pico demo build
#
#   export PICO_SDK_PATH=/path/to/pico-sdk
#   mkdir build && cd build
#   cmake ..            # add -DPICO_BOARD=pico2  for a Pico 2 (RP2350)
#   make -j4
#   -> kw307_pico.uf2   drag onto the RPI-RP2 drive (hold BOOTSEL while plugging in)

cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(kw307_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)        # the SDK sources are C99 and build fine under C11
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# The SDK lives two levels up from this example folder.
set(KW307_SDK ${CMAKE_CURRENT_LIST_DIR}/../..)

add_executable(kw307_pico
    main.c
    port_pico.c
    ${KW307_SDK}/core/KW307_io.c
    ${KW307_SDK}/core/KW307_api.c
    ${KW307_SDK}/core/KW307_api_advanced.c
    ${KW307_SDK}/core/KW307_api_tracker_optimize.c
    ${KW307_SDK}/core/KW307_output.c
    ${KW307_SDK}/core/KW307_cmd.c
    ${KW307_SDK}/core/KW307_protocol.c
    ${KW307_SDK}/core/KW307_uart.c
)

target_include_directories(kw307_pico PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}        # user_config.h
    ${KW307_SDK}/core                # KW307_*.h
)

target_link_libraries(kw307_pico
    pico_stdlib
    hardware_uart
    hardware_irq
)

# printf goes over USB, NOT over UART.
#
# This is not a preference — it is required. pico-sdk routes stdio to uart0 by
# default, which is the UART the radar is on (user_config.h), so leaving it
# enabled would have printf and the radar driving the same pins. Symptoms are
# ugly: corrupted frames, and printf output appearing on the module's RX line.
pico_enable_stdio_usb(kw307_pico 1)
pico_enable_stdio_uart(kw307_pico 0)

# .uf2 / .elf / .bin / .map
pico_add_extra_outputs(kw307_pico)
