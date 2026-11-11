# KW307 ESP32 WiFi dashboard (Arduino)

Arduino sketch for an ESP32 that drives the KW307 over UART and serves a
dashboard over WiFi. Open `KW307_base.ino` in the Arduino IDE.

## Dependencies

Not included in this repository; install them separately:

- **ESP32 Arduino core** (Espressif): provides `WiFi`, `WebServer`, `ESPmDNS` and `DNSServer`.
- **WebSockets** library by Markus Sattler (`WebSocketsServer.h`), installable from the
  Arduino Library Manager. It is licensed under LGPL-2.1, separately from this project.

## Before you deploy

The access point defaults to SSID `KW307-<board>` and password `12345678`
(`KW307WiFi.h`, `board_config.h`). Change the password for anything beyond bench use.

## Licence

Apache License, Version 2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
"Keywave", "KW307" and the Keywave logo are trademarks of Keywave Technology Ltd
and are not licensed under the Apache License.
