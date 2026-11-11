# KW307 SDK

The **KW307** is a 24 GHz presence radar module, driven by a pure-C SDK
over UART — one include (`KW307_api.h`), multi-target tracking, and configurable
range, field of view, gain, sensitivity and hold time.

## Package layout

| Path | What it is |
|------|------------|
| `LICENSE` | Apache License, Version 2.0 |
| `NOTICE` | Copyright and trademark notice — keep it with any redistribution |
| `README.md` | This file |
| `core/` | Portable C SDK source — add `core/*.c` to your build |
| `doc/` | User Guide — how the radar works, the calls that change it, and the wire |
| `example_platform/` | Reference ports (STM32, Arduino ESP32-S3, Windows, Linux, RPi Pico) + a porting skeleton |

## Quick start

You only need **one include** — `#include "KW307_api.h"`.

```c
#include "KW307_api.h"

static void on_radar(const KW307Frame_t* f) {
    bool present = f->presence_flag;                 // 1 = target present
    /* f carries the whole frame: 2 moving + 1 stationary target */
}

int main(void) {
    kw307_init();                // open the UART once at startup
    kw307_on_frame(on_radar);    // register your frame callback
    for (;;) kw307_update();     // pump the receiver in your loop (>= 20 Hz)
}
```

Pick the port that matches your host from `example_platform/` (see
`example_platform/README.md`), copy it, and edit its `user_config.h`.

## Documentation

The full **User Guide** is in [`doc/`](doc/), in two forms of the same text:
[`.html`](doc/KW307_SDK_User_Guide.html) to read, and
[`.md`](doc/KW307_SDK_User_Guide.md) to grep or hand to an assistant. It
follows the signal from the antenna to the target list — one section per
stage, each ending with the calls that change it — and finishes with the
UART protocol underneath, for a host that will not be running C.

**Using an AI coding assistant?** Point it at [`doc/AGENTS.md`](doc/AGENTS.md) first, or paste
the file into the conversation. It collects the behaviour that is not visible in the headers —
loop cadence, what a setter's return value does and does not promise, why `kw307_radar_init()`
is not "apply my settings", the mixed angle units — the things a model otherwise guesses at.

## Contributing

Issues and pull requests are welcome. Contributions submitted to this
repository are licensed under the Apache License, Version 2.0 (Section 5 of
the License).

## Licence

Copyright 2026 Keywave Technology Ltd

Licensed under the [Apache License, Version 2.0](LICENSE). You may use, modify
and redistribute this SDK, including in commercial products, provided you keep
the licence and the [`NOTICE`](NOTICE) file with it.

"Keywave", "KW307" and the Keywave logo are trademarks of Keywave Technology Ltd
and are not licensed under the Apache License. You may say your project "works
with the KW307"; please don't present a modified version as an official Keywave
release.

<https://keywavetech.co.uk/>
