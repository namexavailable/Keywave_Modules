# Porting KW307 SDK to a New Platform

Only the **5 port functions** in `core/KW307_port.h` are platform-specific.
Everything above that (frame parser, CRC-16, CMD encoders, callback
dispatch, blocking read) is pure C and stays untouched.

This folder gives you a complete **skeleton** with the same file layout as
the MCU reference port `Nuvoton_M251/`. `STM32_HAL/`, `Arduino/`,
`pc_win32/`, `pc_linux/` and `RPi_Pico/` are useful references for the
five port functions too. Each file has `TODO:` comments showing exactly
what to fill in.

## Files in this folder

| File | What it is | Edit? |
|---|---|---|
| `port_uart.c` | The 5 port functions with TODO bodies | **Yes — main work** |
| `port_uart.h` | Optional ISR-feed declaration (commented out by default) | Uncomment if ISR-driven |
| `user_config.h` | Compile-time settings (UART peripheral / baud / RX buffer / pins) | **Yes — set your hardware** |
| `user_isr.c` | UART RX interrupt handler skeleton | **Yes if ISR-driven, else delete** |
| `main.c` | Minimal app showing init + on_frame callback + main loop | Use as starting point |
| `README.md` | This file | — |

## Steps

### 1. Copy the folder

```
KW307_SDK/example_platform/port_template/  →  YourProject/kw307_port/
```

Rename `port_template/` to something specific (e.g. `pico_rp2040/`,
`stm32g0_baremetal/`) if you want — names don't matter once it's in
your project.

### 2. Edit `user_config.h`

Set your UART peripheral, baud rate, RX buffer size, GPIO pins. The
file has TODO markers showing what to fill.

### 3. Implement the 5 port functions in `port_uart.c`

Open `port_uart.c` and complete each TODO block:

| Function | What it does |
|---|---|
| `kw307_port_open()`  | Init clocks / pins / UART peripheral; enable RX IRQ if ISR-driven |
| `kw307_port_close()` | Disable + release |
| `kw307_port_send()`  | Blocking TX of `len` bytes |
| `kw307_port_recv()`  | Drain up to `max_len` bytes (timeout_ms = 0 = non-blocking) |
| `kw307_port_is_open()` | Boolean status query |

**Do not rename the functions** — the upper SDK layers call these
names directly. Function signatures are fixed by `core/KW307_port.h`.

### 4. If ISR-driven, fill in `user_isr.c`

- Rename `TODO_RENAME_UARTx_IRQHandler` to your platform's actual IRQ
  vector name (e.g. `UART1_IRQHandler`, `USART2_IRQHandler`).
- Replace the placeholder FIFO-drain pattern with your vendor SDK's
  read primitives.
- Uncomment the `kw307_port_isr_feed()` declaration in `port_uart.h`.

If your platform uses polling RX (e.g. STM32 HAL polling, Arduino
HardwareSerial, ESP-IDF blocking `uart_read_bytes`), **delete
`user_isr.c`** and skip this step.

### 5. Wire into build

Add to your project's source list:
- `KW307_SDK/core/*.c` (SDK core — never changes)
- Your filled-in `port_uart.c` (and `user_isr.c` if ISR-driven)
- Your `main.c` (or your existing application)

Include paths:
- `KW307_SDK/core/`
- `KW307_SDK/example_platform/`
- The folder you copied (renamed)

### 6. Build, flash, run

If everything is correct: `kw307_init()` opens your UART, the module
auto-streams frames at ~20 Hz, and your `on_frame` callback fires
with each frame.

## Reference implementations to compare against

| Platform | Path | Style |
|---|---|---|
| NuMicro M251 | `example_platform/Nuvoton_M251/` | Bare-metal, ISR + ring buffer |
| STM32 (HAL) | `example_platform/STM32_HAL/` | CubeMX HAL, polling |
| Arduino          | `example_platform/Arduino/` | C++ wrapper around HardwareSerial |

When in doubt about a specific function, open the equivalent in the
closest reference port and see how it's done there.

## Common pitfalls

- **Power of 2 RX buffer size.** `KW307_UART_RX_BUFSIZE` MUST be a
  power of 2 (64 / 128 / 256 / 512). The ring uses bitwise masking,
  not modulo — non-power-of-2 silently corrupts data.
- **Enable the RX interrupt no later than `kw307_port_open()`** - either in
  board init before `kw307_init()`, or inside `kw307_port_open()`, which
  `kw307_init()` calls. If it is enabled any later, the first frames the
  module sends at boot are missed.
- **Call `kw307_update()` ≥ 20 Hz.** The module sends a frame every
  ~50 ms. If your main loop polls slower, the RX ring buffer overruns
  and frames are lost.
- **Match baud rate.** 115200 8N1 no flow control — both sides.
