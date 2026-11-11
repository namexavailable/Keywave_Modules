# `RPi_Pico/` — Raspberry Pi Pico port

Reference port for **RP2040** (Pico / Pico W) and **RP2350** (Pico 2) using
**pico-sdk** — plain C, no MicroPython. The onboard LED follows presence and every
transition is printed over USB serial.

RX is interrupt-driven into a ring buffer, the same shape as the Nuvoton M251
port, so your main loop can do other work without losing radar bytes.

## Files in this folder

| File | What it is | Edit? |
|---|---|---|
| `port_pico.c` | The 5 port functions on pico-sdk `hardware/uart` + an RX ISR ring buffer | No — works as-is |
| `user_config.h` | UART instance, TX/RX pins, baud, ring size | **Yes — set your pins** |
| `main.c` | The demo: LED = presence, prints transitions | Use as the starting point for your app |
| `CMakeLists.txt` | pico-sdk build | Only to rename the target |
| `README.md` | This file | — |

## Wiring — 4 lines

| KW307 | Pico pin | Pico signal |
|---|---|---|
| 5V | **40** | VBUS (5 V, present when USB is plugged in) |
| GND | **38** | GND (any GND works; 38 is next to VBUS) |
| **RXD** | **1** | GP0 — UART0 **TX** |
| **TXD** | **2** | GP1 — UART0 **RX** |

> **TX goes to RX.** GP0 is the Pico *transmitting*, so it goes to the module's
> **RXD**; GP1 is the Pico *receiving*, so it goes to the module's **TXD**. Wired
> straight through, everything initialises perfectly and no frame ever arrives —
> the most common first-time fault.

Two power notes:

- **VBUS is only live when USB is connected.** Powering the Pico from VSYS
  instead (a battery, say) leaves VBUS and the module unpowered. Feed the
  module from your 5 V source directly in that case.
- The module draws more than the 3V3 regulator on a Pico is meant to supply, so
  take 5 V from VBUS — not 3V3 (pin 36).

Pico GPIO is **3.3 V logic** while the module runs off 5 V. Confirm the module's
TXD idles at 3.3 V before connecting GP1 on a new build variant — a 5 V line
would damage the Pico input.

## Build

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cd example_platform/RPi_Pico
mkdir build && cd build
cmake ..                    # Pico 2 (RP2350): cmake .. -DPICO_BOARD=pico2
make -j4
```

Flash: hold **BOOTSEL** while plugging the Pico in, then drag `kw307_pico.uf2`
onto the `RPI-RP2` drive that appears.

## Run

Open the USB serial port (`/dev/ttyACM0`, or the new COM port on Windows) at any
baud — USB CDC ignores it:

```
KW307 SDK Pico demo
  module firmware v2.11.0
  streaming

ABSENT                                   (fc=12)
PRESENT  dist= 187 cm  angle=+12.4 deg   (fc=48)
ABSENT                                   (fc=263)
```

`fc` is the frame's `frame_index`.

## ⚠️ printf must not share the radar's UART

`pico_enable_stdio_uart` is set to **0** in `CMakeLists.txt`, and that is required
rather than a preference: pico-sdk sends stdio to `uart0` by default, which is the
UART the radar uses. Leaving it on puts printf and the radar on the same pins —
corrupted frames, and printf text arriving at the module's RX.

If you need a serial console *and* the radar, move the radar to `uart1` in
`user_config.h` (GP4/GP5 or GP8/GP9) and re-enable stdio on `uart0`.

## How the port works

The 5 functions in `core/KW307_port.h` are the only thing the SDK calls into the
platform; every other layer is portable C.

| `KW307_port.h` function | pico-sdk implementation |
|---|---|
| `kw307_port_open(name, baud)` | `uart_init` → `gpio_set_function` ×2 → `uart_set_format(8,1,none)` → `uart_set_hw_flow(false,false)` → FIFO **off** → `irq_set_exclusive_handler` → `uart_set_irq_enables(rx)` |
| `kw307_port_close()` | disable the IRQ, remove the handler, `uart_deinit` |
| `kw307_port_send(buf, len)` | `uart_write_blocking` |
| `kw307_port_recv(buf, max, ms)` | drain the ISR ring buffer; when `ms > 0` and the ring is empty, wait on `make_timeout_time_ms` / `time_reached` |
| `kw307_port_is_open()` | a cached flag |

Three decisions worth knowing:

- **The RX FIFO is deliberately disabled.** With it on, the interrupt only fires
  once the FIFO crosses a threshold, so the tail of a burst can sit unread until
  more data arrives. Off gives one interrupt per byte — about 680 a second at this
  frame rate, nothing for a 125 MHz core — and removes a whole class of "the last
  frame arrives late" behaviour.
- **The ring size must be a power of two**, and there is a `#error` if it is not.
  The index wraps with a mask because RP2040 is Cortex-M0+ with no divide
  instruction; a `%` would pull a software division routine into the ISR.
- **No critical section in `kw307_port_recv`.** The ISR only writes `s_head` and
  the main loop only writes `s_tail`, both single 16-bit accesses, so the
  single-producer / single-consumer ring needs no locking.

When the ring does fill, the ISR **drops** the new byte rather than overwriting
unread data: a dropped byte fails one frame's CRC and the parser resynchronises,
whereas overwriting corrupts a frame already half-parsed. If it happens at all,
the main loop is not calling `kw307_update()` often enough.

## Notes

- **Call `kw307_update()` at least 20×/s.** The demo loop sleeps 5 ms (~200 Hz),
  which leaves the core almost entirely free for your own work.
- **Pico W LED**: there is no `PICO_DEFAULT_LED_PIN` on a Pico W — its LED is on
  the wireless chip. `main.c` compiles either way; use the `cyw43` driver if you
  want the LED there, or just watch the USB output.
- **One module per UART instance.** The port keeps one static ring, so it drives
  one KW307. Two modules means a second port instance — ask us.
- **Baud rate** must match the module's firmware (typically 115200). Wrong baud →
  `kw307_check_fw_version()` reports a transport error.

## See also

- `example_platform/Arduino/` — the other small-MCU port (HardwareSerial)
- `example_platform/Nuvoton_M251/` — bare-metal, ISR ring buffer (same shape)
- `example_platform/pc_linux/` — Linux hosts, incl. Raspberry Pi (the Linux board)
- `example_platform/port_template/` — empty skeleton for a new platform
- `doc/AGENTS.md` — rules for an AI coding assistant working against this SDK
