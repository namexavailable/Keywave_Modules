# AGENTS.md — notes for AI coding assistants

Instructions for an AI assistant (Claude Code, Cursor, Copilot, …) writing an application
against the **KW307** radar SDK. Everything here is behaviour you cannot infer from the
headers. Read it before writing code, and follow it over any guess.

The API itself is documented in `core/KW307_api.h` — read that too.

## The one rule to get right: test for Valid

**Use a target slot only when its state is Valid:** `state == 1`, in `moving[]` and
`stationary[]` alike. `0` is an Empty slot.

- **Never test `state != 0`.** On firmware older than 2.7.6, `moving[]` is Valid at `3`, and
  `1` / `2` are not yet confirmed.
- **Never test `mag != 0`** — `mag` is the magnitude, not presence.

This rule is repeated here because this file is often the only thing an assistant is given.
The state values are in `core/KW307_api.h` and `core/KW307_protocol.h`. **If they ever disagree with this file, the headers are correct.**

---

## Build and wire-up

Compile these, all pure C99, no OS:

```
core/KW307_io.c  core/KW307_api.c  core/KW307_api_advanced.c  core/KW307_api_tracker_optimize.c
core/KW307_output.c
core/KW307_cmd.c  core/KW307_protocol.c  core/KW307_uart.c
example_platform/<your port>/port_uart.c     (+ user_isr.c if the port is ISR-driven)
```

Include paths: `core/` and `example_platform/<your port>/`.

- **The port and baud rate come from `user_config.h`**, not from a function argument. There is
  no `kw307_open("COM5")`. Copy the port folder closest to your hardware and edit that file.
- **Do not modify anything under `core/`.** It is regenerated for each release, so your edits
  would be overwritten by the next SDK release. Put your code in your own files.
- **Getters take no timeout.** Every read waits about `KW307_READ_TIMEOUT_MS` (200 ms) and
  then returns `KW307_ERR_TIMEOUT`.
- **Do not write your own UART framing.** `kw307_uart.c` already adds the five `0x00` wake-up
  bytes, the header and the CRC-16 to every frame. Sending your own wake bytes corrupts
  the frame.

## The main loop

`kw307_update()` is non-blocking; it drains the UART and fires your callback. Call it
**at least 20 times a second** — a 30 ms timer is enough.

The port's RX buffer is 256 bytes (`KW307_UART_RX_BUFSIZE`). The default frame is 27 bytes of
payload, 34 on the wire with header and CRC, about 20 a second, so the buffer holds about seven
frames: fall behind by about 350 ms and frames are lost. The larger output modes hold fewer —
`KW307_OUTPUT_NORMAL_WITH_SPD_RG` about three, about 150 ms. Never `sleep()` in the loop for longer than that.

## Callback rules

Your `kw307_on_frame()` callback runs **inside** `kw307_update()`, on the same thread.

- **Do not call any `kw307_*` function from inside the callback.** The blocking calls
  (`kw307_read_blocking` and the setters that use it) would re-enter the receive path.
- Copy out what you need — the `KW307Frame_t*` is only valid for the duration of the call.
- Latch into a variable, act on it in the main loop. Keep the callback to assignments.
- The SDK is **single-threaded** by design. If your platform has threads, keep every
  `kw307_*` call — `kw307_update()` included — on one thread.

## What a setter's return value means

`KW307_OK` means **the command was transmitted**, not that the module accepted it. Arguments
are validated locally (`KW307_ERR_PARAM`), but by default nothing waits for the module's ACK.

```c
if (kw307_set_gain(5) == KW307_OK) { /* sent — NOT confirmed */ }
```

If the firmware rejects a value it answers a NACK, which you read afterwards with
`kw307_last_nack_status()` (`0x01` invalid value, `0x02` unknown CMD, `0x03` CRC, `0x04`
length). For a hard confirmation, read the setting back, or wait for the ACK:
`kw307_write_blocking()` for one write, `kw307_set_write_verify()` for every write the SDK
sends (for configuration and production tools, not a product's running loop).
`kw307_check_fw_version()` is the exception — it is a blocking read and does return a
real verdict.

## `kw307_radar_init()` is not "apply my settings"

This call is often misunderstood.

- **No setter needs it.** Settings take effect from the next frame. A full configuration —
  gain, range, FOV, both sensitivities, hold time, thresholds, tracker counters — is
  applied with no init call anywhere.
- **It returns every setting to what was last saved, so it DISCARDS anything you set but did
  not save.** Calling it after configuring therefore reverts your configuration.

Treat it as a recovery action (~130 ms, blocking). To drop stale targets without touching
settings, use `kw307_clear_state()` instead — it is much cheaper and keeps your configuration.

## Persistence

`kw307_save_settings()` writes flash. **Do not call it in a loop or once per settings change**
— flash has a limited number of write cycles. Configure, then save once.

Saving takes up to about 1 s, and a command sent before it has finished can be lost. Wait
that long before the next call, or send the save with
`kw307_write_blocking(KW307_CMD_SAVE_SETTINGS, ...)` and a 1000 ms timeout.

Every setting — `kw307_set_equalizer_order()` and `kw307_set_digital_gain()` included —
works the same way: unsaved, it is lost on a power cycle **and** on `kw307_radar_init()`;
saved, it survives both; `kw307_factory_reset()` returns it to the default.

These are **never saved**, whatever you do — set them again after every power-up:

- the four background switches — `kw307_set_moving_background()`,
  `kw307_set_stationary_background()` and the two `..._env_subtraction()`. A re-init, a
  factory reset and a power cycle all put both bands back to background-update on with
  environment subtraction on.
- `kw307_en_gpio_presence_flag()` and `kw307_en_stationary_hold()`.
- the output mode — the module always boots streaming `KW307_OUTPUT_NORMAL`, and
  `kw307_factory_reset()` switches back to it too.

## Units — they are not uniform

| Value | Unit |
|---|---|
| `distance_cm` (frame) | cm (the wire carries mm; the SDK divides by 10, truncating) |
| `angle_tenth` (frame) | **0.1°** — `-350` is −35.0° |
| `kw307_set_detect_range()` | cm |
| `kw307_set_detect_fov()` | **whole degrees** — pass `30`, not `300` |
| `kw307_set_presence_hold_zone()` | cm and **whole degrees** |
| presence hold time / stationary release time | **frames** — 20 a second, so 11 s is `220` |
| tracker counters | frames (20 a second, so 32 frames is about 1.6 s) |

The frame reports tenths of a degree while every setter takes whole degrees. Mixing these up
does not fail — it silently configures an angle ten times off.

The hold and release times took seconds before SDK 2.17.1. Seconds still compile —
`kw307_set_presence_hold_time(11)` is 11 frames, about half a second. 1 s = 20 frames at the
normal 20 Hz. The hold time takes 1~32767 frames (0 acts as 1), the release time 0~32767;
32767 is about 27 min.

## Reading a frame

```c
KW307Frame_t: frame_index, presence_flag, moving[2], stationary[1]
```

- `stationary` has **exactly one** element. `stationary[1]` is out of bounds — the symmetry
  with `moving[2]` is misleading.
- For plain presence, `presence_flag` alone is enough: the module already holds it over short
  dropouts (`kw307_set_presence_hold_time()`). Read the slots only when you need positions.
- An Empty slot has every field zeroed. Test Valid all the same.
- `counter` runs 0~`cnt_limit`, which you set with `kw307_set_moving_tracker_cnt()` /
  `kw307_set_stationary_tracker_cnt()` (defaults 32 moving, 64 stationary) — not a fixed 0~32.
  Scale any fade or confidence display against the value you configured.
- **`angle_tenth`: `0` is straight ahead, and the magnitude is the off-axis angle — but do not
  assume which side is positive.** KW307 ships in two chip packages whose antenna geometry is
  mirrored, so the same target at the same spot reports `+250` on one and `-250` on the other.
  The module does not tell you which one it is. So:
  1. Put a target clearly off to one side, note the sign, and write it down for that hardware.
  2. Keep **one** constant in *your* code — `#define MY_ANGLE_SIGN (+1)` or `(-1)` — and apply
     it in exactly one place, where you draw or make a left/right decision.
  3. Store and log the raw `angle_tenth`, not the flipped value, so captured data stays
     comparable across builds. Negate the angle at draw time only.

  Anything that only needs distance, presence, or "how far off-axis" is unaffected — this
  matters solely when left and right must be told apart.
- Writing your own transport instead of using `kw307_on_frame()`? `kw307_dec_frame()` requires
  `len == 27` exactly and returns `-1` otherwise.

## Custom frames (`KW307FrameF3_t`)

Selected with `kw307_set_output_config()` / `kw307_set_output_rg()`, received with
`kw307_on_frame_f3()`.

- **Every block is in the struct, sent or not.** What the frame did not carry is zero. Test
  `f->blocks & KW307_F3_*` before reading a block — a zero value does not mean "not sent".
- `moving_trk[]` and `stationary[]` follow the Valid rule above, exactly like `moving[]` and
  `stationary[]` in the default frame.
- **`stationary_trk[]` does not.** Its state is the raw tracker state: `0` Empty, `1` Checking,
  `2` Birth, `3` Valid. Only `3` is a target to show. It is for tuning a board; a
  product reads `stationary[]`.
- `rg[gate][band]` is indexed by the real gate number (`rg[5]` is gate 5), band
  `KW307_BAND_MOVING` or `KW307_BAND_STATIONARY`. Gates outside the range you chose are zero.
- **Send the shape again after the module resets** — it comes back on the default frame.
- Check the size with `kw307_out_len()` before choosing a shape. The largest is 270 bytes of
  payload, 277 on the wire — more than the whole 256-byte RX buffer — about 5.5 KB a second at 20
  frames a second. It needs 115200 baud, and the buffer then holds only about 45 ms of stream: call
  `kw307_update()` more often than that, or raise `KW307_UART_RX_BUFSIZE` in `user_config.h`.

## Power down

`kw307_set_power_mode(KW307_POWER_DOWN)` stops the stream and stops sensing, at the lowest current. The module
still accepts commands, but **sensing does not resume until
`kw307_set_power_mode(KW307_POWER_NORMAL)`** — make that the first call after power-down, and
do not rely on other settings or getters until it has been sent.

## Firmware pairing

`kw307_check_fw_version()` returns `0` compatible, `1` firmware older than this
SDK's paired baseline, `<0` link error. It is worth calling once at startup and **acting on
the result** — the bundled examples call it but do not act on the result, only to keep
them short; production code should handle it.
After it finds 2.7.6, the calls that firmware lacks return `KW307_ERR_NOT_SUPPORTED` instead
of being sent.

A successful check also proves the link works, so a failure here means wiring, power or
baud rate, not a bad setting.
