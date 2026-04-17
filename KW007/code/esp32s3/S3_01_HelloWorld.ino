/*
 * ============================================================
 *  KW007 Hello-World — Keywave 5.8GHz Doppler Radar Sensor
 *
 *  What it does:
 *    Initialises the sensor, sets a medium detection range,
 *    and prints motion + RSSI to the Serial Monitor at 8 Hz.
 *
 *  Expected Serial output (115200 baud):
 *    KW007 Hello-World
 *    Init OK   model=KW007-L
 *    [   1234 ms]  APPROACH .       RSSI=187   day
 *    [   1364 ms]  .        LEAVING RSSI=152   day
 *    [   1494 ms]  .        .       RSSI= 30   day
 *
  *  Board preset: ESP32-S3                        SDA=GPIO8 SCL=GPIO9
 *
 *  Wiring (KW007 -> Board):
 *    VCC  -> 3.3V   (do NOT use 5V on ESP32/ESP8266)
 *    GND  -> GND
 *    SDA  -> see table below
 *    SCL  -> see table below
 *
 *    Board              SDA pin     SCL pin     VCC
 *    -----------------  ----------  ----------  -----
 *    Arduino UNO        A4          A5          5V *
 *    ESP32-S3           GPIO 8      GPIO 9      3.3V
 *    ESP32-C3           GPIO 8      GPIO 9      3.3V
 *    ESP8266 (D1 Mini)  D2 (GPIO4)  D1 (GPIO5)  3.3V
 *
 *    * UNO is 5V-tolerant on I2C. KW007 typically accepts 3.3V
 *      logic; check your sensor's VCC range. If using 5V VCC on
 *      UNO, add 3.3V<->5V level shifters or use 3.3V supply.
 *
 *  I2C pull-ups:
 *    Many KW007 breakout boards include 10k pull-ups on SDA/SCL.
 *    If yours does not, add 4.7k pull-ups to 3.3V on both lines.
 * ============================================================
 */

#include <Wire.h>
#include "KW007_api.h"

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) { /* wait for USB serial */ }
    Serial.println();
    Serial.println(F("KW007 Hello-World"));

    /* --- Start I2C ---
     * Uncomment ONE line for your board (or just call Wire.begin()
     * with no args to use the board's default SDA/SCL pins).
     */
    // Wire.begin();              // Arduino UNO / Nano (A4/A5)
    // Wire.begin(8, 9);          // ESP32-S3 / ESP32-C3
    // Wire.begin(4, 5);          // ESP8266 D1 Mini (D2=GPIO4, D1=GPIO5)
    Wire.begin(8, 9);    // <-- preset for this board

    /* --- Initialise sensor --- */
    uint8_t err = KW007_Init();
    if (err != KW007_OK) {
        Serial.print(F("Init FAILED  err=0x"));
        Serial.println(err, HEX);
        Serial.println(F("Check wiring, power, and pull-ups."));
        while (1) delay(1000);
    }

    /* Print detected model */
    Serial.print(F("Init OK   model="));
    switch (KW007_GetModel()) {
        case KW007_S:       Serial.println(F("KW007-S/SE")); break;
        case KW007_L:       Serial.println(F("KW007-L"));    break;
        default:            Serial.println(F("UNKNOWN"));    break;
    }

    /* Range 4 ≈ 3 m for KW007-L (good default for testing) */
    KW007_SetDetectionRange(4);

    Serial.println(F("Wave your hand in front of the sensor."));
    Serial.println();
}

void loop() {
    KW007_Data d = KW007_Read();

    if (!d.ok) {
        Serial.println(F("I2C read failed"));
        delay(500);
        return;
    }

    /* Compact one-line status */
    char line[80];
    snprintf(line, sizeof(line),
             "[%7lu ms]  %s %s RSSI=%3u   %s",
             (unsigned long)millis(),
             d.approaching ? "APPROACH" : ".       ",
             d.leaving     ? "LEAVING"  : ".      ",
             d.rssi,
             d.isDay       ? "day"      : "rest");
    Serial.println(line);

    delay(125);  /* sensor updates at ~8 Hz */
}
