# Fall Detector — Design

Date: 2026-10-10 · Status: waiting for Saud's review

## Goal
A belt-worn fall detector for elderly care. The ESP32 detects falls (MPU6050) and SOS presses,
and shows each alert with GPS location on a web dashboard it serves itself.
College demo first, built so it can be upgraded for real use later.

**Done means (on the real hardware):**
- Simulated fall (board dropped onto a pillow/bed) → dashboard alert within 3 s, buzzer sounds.
- SOS press → dashboard alert within 1 s.
- Normal handling (walking, sitting down, picking it up) → no alert.
- "Acknowledge" on the dashboard stops the buzzer and clears the red LED.

## Decisions made with Saud
- **SIM800L removed** (2026-10-10). No SMS or calls; alerts go to the WiFi dashboard only.
- **Dashboard runs on the ESP32**, no laptop server.
- Blue and green LEDs are not owned yet; the code drives their pins anyway.
- Purpose assumed "demo first, upgradable" (not explicitly confirmed).

## Hardware and pinout (ESP32 DevKit V1)
| Part | Pin(s) | Notes |
|---|---|---|
| MPU6050 (GY-521) | SDA 21, SCL 22, INT 34, VCC 3V3 | INT wired and tested; unused by v1 logic |
| NEO-6M GPS | TX→16, RX←17, VCC 3V3 | 9600 baud NMEA |
| SOS button | 33 ↔ GND | internal pull-up, pressed = LOW |
| Buzzer | 25 | `tone()` so active or passive buzzers both sound |
| Red LED | 23 | alert active |
| Green LED | 18 | connection status; Uno D7 mirrors it until a real LED is fitted |
| Blue LED | 19 | GPS fix; nothing fitted yet |
| Battery sense | 35 | not wired, not used in v1 |

Power: USB for now; the whole board draws about 150–250 mA.

## Network
- **Own hotspot, always on:** SSID `FallDetector`, WPA2 password from `secrets.h`.
  Dashboard at `http://192.168.4.1`; a captive portal makes phones open it automatically.
- **Joins known WiFi when in range** (WiFiMulti, list in `secrets.h`): a college/phone
  hotspot and the home WiFi. The ESP32 is 2.4 GHz only and cannot join a 5 GHz network.
  When joined, the dashboard is also at the router-assigned IP (printed on serial and shown on
  the dashboard) and at `http://falldetector.local`.
- Secrets live only in `firmware/fall_detector/secrets.h` (gitignored); `secrets.h.example`
  is committed with placeholders.

## Device behaviour
| Output | Meaning |
|---|---|
| Green (18) | solid = joined a WiFi network; slow blink = hotspot only |
| Blue (19) | on = GPS has a fix |
| Red (23) | on = alert active (until acknowledged); fast blink = IMU error |
| Buzzer | beeps while an alert is active; auto-silences after 60 s (red LED stays on) |

- **SOS press** (debounced) → SOS alert immediately.
- **New alert while one is active** → logged as a new alert, becomes the active one, buzzer restarts.
- There is no cancel on the device: it would share the SOS button, so a person who really fell
  could cancel their own alert by mistake. The caregiver acknowledges from the dashboard.

## Fall detection
- MPU6050 at **±8 g** (the self-test used ±2 g, which clips impacts at 2 g), sampled at 100 Hz.
- **Boot calibration:** device held still for 2 s → gyro offsets, plus an accel scale so a still
  device reads |a| = 1.00 g (the module reads 1.11 g raw). If it moves during calibration it
  retries; after 3 tries it uses the defaults and says so on serial.
- **State machine:**
  1. **Watching:** |a| < `FREEFALL_G` (0.5 g) for ≥ `FREEFALL_MS` (60 ms) → free-fall.
     Remember the gravity direction from the last second before it.
  2. **Free-fall:** |a| > `IMPACT_G` (2.0 g) within `IMPACT_WINDOW_MS` (1000 ms) → impact;
     otherwise go back to Watching.
  3. **Impact:** within `CONFIRM_MS` (3000 ms) it must lie still for `STILL_MS` (800 ms) in one go
     (|a| within 1 ± `STILL_TOL_G` 0.3 g and gyro < `STILL_DPS` 60 °/s), and the gravity direction
     during that stretch must have rotated ≥ `ANGLE_DEG` (15°) from before the fall → **FALL alert**
     about 1–2 s after the hit. Bouncing, getting up or being picked up break the still stretch.
     (Softened 2026-10-11 after real drops: pillow bounce lasts ~1.1 s, testers pick the board up after
     ~1.2 s, and hand-held drops tip only 19–36°. The old fixed 1 s + 2 s window missed all of them. Later the same day:
     `IMPACT_G` 2.5 → 2.0 g and `ANGLE_DEG` 30 → 15°, because drops hit 2.7–12 g but tipped only 19–29°;
     flat→flat drops measured 3–14°.)
- `tests/replay.cpp` replays a recorded log through the same detector and explains each decision.
- All thresholds are named constants at the top of `logic.h`: these are the tuning settings,
  set from real recordings.
- **Logging mode:** typing `l` in the serial monitor toggles a 100 Hz CSV stream (`t_ms,ax,ay,az,dps`),
  so real movements can be recorded and the thresholds tuned from data. Typing `a` acknowledges an
  alert (useful before the dashboard exists).
- Calibration treats the accel error as lying along gravity while calibrating, which is right for
  this module's Z offset when it boots flat; a 6-side calibration is the upgrade path if needed.

## GPS
- GGA sentence parsing in `logic.h`: fix flag, satellite count, latitude/longitude in decimal degrees.
- The last good fix is kept with its time. An alert carries that location and its age, or
  "no fix yet".

## Web server and dashboard
Built-in ESP32 libraries only: `WebServer`, `DNSServer`, `ESPmDNS`, `WiFi`, `Wire`.

**Endpoints**
- `GET /` → the dashboard: one HTML page with inline CSS/JS from `dashboard.h`. It has no
  external files, so it works on the hotspot with no internet.
- `GET /api/state?since=<seq>` → JSON: uptime, network (mode, SSID, IP, RSSI), GPS
  (fix, sats, lat, lon, age), IMU status, |a| samples newer than `seq` (25 Hz ring buffer,
  last 5 s), the active alert, and the last 10 alerts.
- `POST /api/ack` → acknowledges the active alert (stops the buzzer, red LED off).
- Any unknown URL → redirect to `/` (captive portal).

**Dashboard page**
- Status bar: device online/offline (offline after 3 s with no reply), network, signal strength.
- Live |a| graph for the last 20 s (plain canvas), so the fall spike shows live.
- GPS card: fix, satellites, coordinates, "Open in Google Maps" link.
- Alert banner: big and red, FALL or SOS, time, location, plays a beep, **Acknowledge** button.
- "Tap to enable sound" button (browsers block sound until the user taps once); tap again to mute.
- History of the last 10 alerts (kept in RAM, cleared when the ESP32 restarts).
- **Detector log** (added during testing): the last 8 detector decisions in plain words, e.g.
  "not a fall: tipped 9 deg (need 15)", so a tester can see why a drop
  did or didn't count without a laptop on the serial port.
- Polls every 500 ms.

## Error handling
- IMU missing at boot → dashboard shows "IMU ERROR", red LED fast-blinks, fall detection is off;
  it retries every 5 s.
- GPS sends nothing → "GPS: no data"; data but no fix → "searching (N sats)".
- WiFi drops → WiFiMulti reconnects; the hotspot keeps working regardless.
- An alert always fires locally (buzzer + red LED) even if nobody has the dashboard open.

## Testing
1. **On the laptop:** `tests/test_logic.cpp` (g++, asserts) feeds made-up sample sequences into the
   fall detector (fall, sit-down, walk, drop then pick up) and real GGA sentences into the parser.
2. **On the real hardware:**
   - Logging mode: record walking, sitting, standing up, and 5 drops onto a pillow → tune the thresholds.
   - 10 simulated falls → expect at least 9 detected.
   - 2 minutes of normal handling → 0 false alerts.
   - SOS → alert in under 1 s; Acknowledge → buzzer stops.
   - GPS fix near a window or outside.
   - Dashboard from the laptop (home WiFi) and from a phone (hotspot).

## Out of scope for v1
SMS/GSM, Telegram/phone push, cloud hosting, dashboard login, battery sense, deep sleep,
cancelling an alert from the device, keeping alerts across reboots, an embedded map.

## Risks
- Pillow drops are not human falls; thresholds tuned on drops will need re-tuning on a real
  wearer. This is an honest limit of a demo.
- A phone on the hotspot shows "No internet" (choose "Stay connected"); a locked phone stops updating.
- No GPS fix indoors is normal.
- Laptop on 5 GHz and ESP32 on 2.4 GHz of the same router are normally on the same network;
  if the router isolates them, use the hotspot.

## File layout
```
firmware/fall_detector/
  fall_detector.ino    pins, sensors, network, web server, LEDs/buzzer
  logic.h              fall detection + GGA parsing (pure C++, host-testable)
  dashboard.h          the web page (raw string)
  secrets.h            hotspot + WiFi credentials (gitignored)
  secrets.h.example    placeholder copy (committed)
tests/test_logic.cpp   host tests for logic.h
hardware_tests/        existing test sketches (module_test, esp32_blink_test, arduino_led_test)
docs/superpowers/specs/ this file
```
