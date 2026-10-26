# fall-detection-alert-system

Belt-worn fall detector for elderly care: an ESP32 detects falls (MPU6050) and SOS presses and shows
alerts with GPS location on a web dashboard it serves itself. College demo first, upgradable to real use.
Stack: Arduino C++ on ESP32 (arduino-cli, esp32 core 3.3.12), built-in libraries only; dashboard is plain HTML/CSS/JS embedded in the firmware.

## Project goal
On the real hardware: a simulated fall or an SOS press shows an alert (type, time, location) on the dashboard
within 3 s and sounds the buzzer until it is acknowledged; normal movement causes no alert.
Full design: `docs/superpowers/specs/2026-10-10-fall-detector-design.md`.

## Directory map
- `firmware/fall_detector/` — main firmware: `.ino`, `logic.h` (pure logic), `dashboard.h`, `secrets.h` (gitignored)
- `tests/` — `test_logic.cpp` laptop tests (g++) for fall detection + GPS parsing; `replay.cpp` replays recorded IMU logs
- `hardware_tests/` — module test sketches; for now they sit at the root: `module_test/`, `sim_test/`, `esp32_blink_test/`, `arduino_led_test/`
- `docs/` — design spec
- `WhatsApp Image 2026-10-09 ….jpeg` — original poster (its circuit diagram is only an illustration, not real wiring)

## Stack
- ESP32 DevKit V1 (ESP32-D0WD-V3, on `/dev/ttyUSB0`), MPU6050 (GY-521, 0x68), NEO-6M GPS, SOS button, buzzer, red LED.
  Green (18) and blue (19) LEDs are not fitted yet; Arduino Uno on `/dev/ttyACM0` mirrors green on D7.
- Pinout: MPU 21/22 + INT 34 · GPS 16/17 · SOS 33 · buzzer 25 · red 23 · green 18 · blue 19.
- **SIM800L removed from the project (2026-10-10).** No GSM/SMS.
- WiFi is 2.4 GHz only. No ML, no GPU.
- Credentials live only in `firmware/fall_detector/secrets.h`; never commit or print them.

## Build / run commands
- Compile: `arduino-cli compile --fqbn esp32:esp32:esp32 firmware/fall_detector`
- Upload: `arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 firmware/fall_detector`
- Serial: `arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200`
- Test: `g++ -std=c++17 -o /tmp/test_logic tests/test_logic.cpp && /tmp/test_logic`

## Current status
- Done: pinout; module self-test passed for MPU6050 + INT, GPS (no indoor fix), SOS, buzzer, red LED, Uno link; design approved.
- Done (2026-10-10/11): firmware + hotspot/WiFi dashboard; thresholds softened from real drops (impact 2.0 g, still 0.8 s within 3 s, tilt 15°).
- Pending: more real drop tests.
- Blockers: green/blue LEDs not owned (the code drives them anyway); folder name is not kebab-case (rename needs Saud's yes).
