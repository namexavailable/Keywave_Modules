# `example_platform/` — Pick or fill

This folder ships **reference ports** for common host platforms plus a
**skeleton** for platforms not listed. Choose based on what host MCU /
framework you're integrating with.

## Decision tree

```
Is your host one of these?
├── STM32 (Cube/HAL-generated UART)
│   → copy STM32_HAL/ to your project, edit user_config.h
├── Arduino (any core with HardwareSerial: ESP32, AVR, SAMD, RP2040...)
│   → copy Arduino/ to your sketch
├── Windows PC / IPC (COM port via USB-Serial or UART header)
│   → copy pc_win32/ to your project, set the COM port in user_config.h
├── Linux host (PC, Raspberry Pi, Jetson — /dev/ttyUSB*, /dev/ttyS*)
│   → copy pc_linux/ to your project, set the device in user_config.h
├── Raspberry Pi Pico (RP2040 / RP2350, pico-sdk, plain C)
│   → copy RPi_Pico/ to your project, edit user_config.h
├── Nuvoton NuMicro M251 (bare-metal, ISR-driven UART)
│   → copy Nuvoton_M251/ to your project, edit user_config.h
└── Anything else (Cortex-M0/M4 from another vendor, AVR,
                   bare-metal STM32 without HAL, RTOS-based system…)
    → copy port_template/, follow port_template/README.md to fill in
      the 5 port functions yourself
```

## Shared header

`KW307_mcu.h` at this folder root is a one-line recipe wrapper that
`#include`s `core/KW307_api.h`. Customers can include either header —
they're functionally equivalent. The wrapper exists so MCU / Arduino
code can write a single semantically-clear include line.

## Files in each port directory

| File | What it does |
|---|---|
| `port_uart.c` / `port_<platform>.c` (+ its `.h`) | Implements the 5 functions from `core/KW307_port.h` for this platform |
| `user_config.h` | Compile-time settings (UART peripheral ID, baud rate, RX buffer size, GPIO pins) — **the thing you edit** |
| `main.c` / `main.ino` / `main_<x>.c` | Minimal app showing init + on_frame callback |
| `user_isr.c` (ISR-driven ports) | UART RX ISR that feeds the ring buffer fed back through `kw307_port_recv()` |

## What to do once you've copied a port

1. Add `core/*.c` to your build/link target.
2. Add the per-platform `port_uart.c` (and `user_isr.c` if present).
3. Edit `user_config.h` for your board's UART peripheral / baud / pins.
4. Include `core/KW307_api.h` in your `main`.
5. Wire the SDK into your application:
   - `kw307_init()` once at startup
   - apply a demo scene, as shown in the `switch` block of
     `example_platform/<port>/main.c` (choose `OUTDOOR` / `OFFICE` / `DESK` /
     `URINAL` / `HAND_DRYER` as needed)
   - `kw307_on_frame(cb)` to register your per-frame callback
   - In your main loop: `kw307_update()` ≥ 20 Hz

The Quick start in the package `README.md` is the 5-minute walk-through. The
User Guide in `doc/` is the long one — how the radar works, the call that
changes each part of it, and the UART protocol underneath. `doc/AGENTS.md` has
the rules for an AI coding assistant.

## Reference vs skeleton — what's the difference?

- **Reference ports** (STM32_HAL, Arduino, pc_win32, pc_linux, RPi_Pico,
  Nuvoton_M251)
  have **working code** for that exact platform. Copy → edit → build.
- **`port_template/`** is a **skeleton** — the 5 port functions exist
  as bodies with `TODO:` comments. You implement each one against
  your platform's UART API. Use this when none of the references
  matches your platform.
