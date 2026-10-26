# Fall Detection and Alert System

A belt-worn fall detector for elderly care. An ESP32 watches an MPU6050 motion sensor, and on a fall
or an SOS button press it sounds a buzzer and shows the alert, with GPS location, on a web dashboard
that the ESP32 serves itself. No app, no cloud, no internet needed.

![Project poster](WhatsApp%20Image%202026-10-09%20at%208.35.53%20PM.jpeg)

> The poster's circuit diagram is an illustration only. Use the pinout below. The SIM800L (SMS) was
> dropped from this build; alerts go to the WiFi dashboard.

## Hardware

| Part | ESP32 pin | Notes |
|---|---|---|
| MPU6050 (GY-521) | SDA 21, SCL 22, INT 34, VCC 3V3 | accelerometer + gyro |
| NEO-6M GPS | GPS TX → 16, GPS RX ← 17, VCC 3V3 | 9600 baud |
| SOS button | 33 ↔ GND | internal pull-up |
| Buzzer | 25 | |
| Red LED | 23 | alert active (fast blink = sensor error) |
| Green LED | 18 | solid = joined WiFi, slow blink = hotspot only |
| Blue LED | 19 | GPS has a fix |

Board: ESP32 DevKit V1. Power over USB (about 150–250 mA).

## Setup

1. Install [arduino-cli](https://arduino.github.io/arduino-cli/) and the ESP32 core:
   `arduino-cli core install esp32:esp32`
2. Copy `firmware/fall_detector/secrets.h.example` to `secrets.h` and put in your own hotspot
   password and your 2.4 GHz WiFi networks. `secrets.h` is gitignored.
3. Build and upload (change the port if yours differs):
   ```
   arduino-cli compile --fqbn esp32:esp32:esp32 firmware/fall_detector
   arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 firmware/fall_detector
   ```
4. Keep the board **flat and still for 2 seconds** after power-on while it calibrates the sensor.

## Opening the dashboard

- **Any phone:** join the WiFi `FallDetector` (password from `secrets.h`) and open **http://192.168.4.1**.
  Most phones open it automatically.
- **Same WiFi as the ESP32:** open **http://falldetector.local**, or the IP printed on the serial
  monitor (115200 baud). Some Android phones cannot open `.local` addresses; use the IP then.

The dashboard shows the alert banner with an **Acknowledge** button (stops the buzzer), GPS position,
a live movement graph, a detector log explaining why each drop did or didn't count, and alert history.
Tap "Tap to enable alert sound" so the phone beeps during an alert too.

## How a fall is detected

1. **Free fall:** total acceleration below 0.5 g for 60 ms.
2. **Impact:** above 2.0 g within 1 s.
3. **Lying down:** still for 0.8 s at any point within 3 s of the impact, and tilted at least 15°
   from how it was held before the fall.

Shaking it, sitting down hard or jumping does not trigger an alert. To demo it, hold the board
upright, drop it onto a bed so it lands flat, and leave it for a second.
All thresholds are named constants at the top of `firmware/fall_detector/logic.h`.

## Tests (on a laptop, no hardware)

```
g++ -std=c++17 -o /tmp/test_logic tests/test_logic.cpp && /tmp/test_logic
```

`tests/replay.cpp` replays a recorded sensor log (serial command `l` toggles a 100 Hz CSV stream)
through the same detector and prints the reason for each decision.

## Folders

- `firmware/fall_detector/`: main firmware (`logic.h` is the pure fall-detection + GPS logic, `dashboard.h` the web page)
- `tests/`: laptop tests and the replay tool
- `module_test/`, `esp32_blink_test/`, `arduino_led_test/`, `sim_test/`: hardware check sketches used while building
- `docs/`: design spec

## Limits

Thresholds were tuned on drops onto a bed, not on real people falling; a real wearer would need
re-tuning. Indoors the GPS usually has no fix. Alerts are kept in RAM and cleared on restart.
