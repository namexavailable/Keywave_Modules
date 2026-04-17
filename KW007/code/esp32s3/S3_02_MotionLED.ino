/*
 * ============================================================
 *  KW007 Motion-LED — Keywave 5.8GHz Doppler Radar Sensor
 *
 *  What it does:
 *    Lights the onboard LED whenever motion (approach OR leave)
 *    is detected. LED stays on for HOLD_MS after the last
 *    detection so blinks are visible.
 *
  *  Board preset: ESP32-S3                        SDA=GPIO8 SCL=GPIO9
 *
 *  Wiring: same as 01_HelloWorld (see comments in that sketch).
 *
 *  LED behaviour by board:
 *    Arduino UNO   : LED_BUILTIN (pin 13), HIGH = on
 *    ESP32-S3      : LED_BUILTIN if defined; many boards use
 *                    a WS2812 RGB LED (this sketch handles the
 *                    simple GPIO LED case — see board notes)
 *    ESP32-C3      : LED_BUILTIN (often GPIO 8); some boards
 *                    use active-LOW logic
 *    ESP8266 D1Mini: LED_BUILTIN (GPIO 2), active-LOW
 *
 *  If the LED behaves backwards (on at idle, off on motion),
 *  flip the LED_ACTIVE_LOW define below.
 * ============================================================
 */

#include <Wire.h>
#include "KW007_api.h"

/* ---------- Board / LED config -------------------------------
 * Override these two for your board if needed.
 */
#ifndef LED_BUILTIN
  #define LED_BUILTIN 2     /* fallback for boards with no default */
#endif

/* ESP8266 and many ESP32-C3 boards have active-LOW onboard LEDs.
 * Set to 1 if your LED logic looks inverted. */
#if defined(ESP8266)
  #define LED_ACTIVE_LOW 1
#else
  #define LED_ACTIVE_LOW 0
#endif

#define HOLD_MS 400   /* keep LED on this long after last detection */

/* ---------- Helpers ----------------------------------------- */
static inline void ledWrite(bool on) {
#if LED_ACTIVE_LOW
    digitalWrite(LED_BUILTIN, on ? LOW : HIGH);
#else
    digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
#endif
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) { }
    Serial.println();
    Serial.println(F("KW007 Motion-LED"));

    pinMode(LED_BUILTIN, OUTPUT);
    ledWrite(false);

    /* Start I2C — uncomment line for your board if default fails */
    // Wire.begin();              // UNO / Nano
    // Wire.begin(8, 9);          // ESP32-S3 / ESP32-C3
    // Wire.begin(4, 5);          // ESP8266 D1 Mini
    Wire.begin(8, 9);

    if (KW007_Init() != KW007_OK) {
        Serial.println(F("Init FAILED — check wiring."));
        while (1) {
            ledWrite(true); delay(100);
            ledWrite(false); delay(100);
        }
    }

    KW007_SetDetectionRange(4);   /* ~3 m */
    Serial.println(F("Ready. LED lights on motion."));
}

void loop() {
    static uint32_t lastMotion = 0;

    KW007_Data d = KW007_Read();
    if (d.ok && (d.approaching || d.leaving)) {
        lastMotion = millis();
        Serial.print(F("Motion! RSSI="));
        Serial.println(d.rssi);
    }

    bool ledOn = (millis() - lastMotion) < HOLD_MS;
    ledWrite(ledOn);

    delay(50);
}
