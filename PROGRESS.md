# PROGRESS — fall-detection-alert-system

Session save-file. Re-read at session start; update after each step.

## Build steps (from the approved spec)
1. ✅ **Logic on the laptop:** `firmware/fall_detector/logic.h` (fall detector + GGA parser) + `tests/test_logic.cpp`; all 7 fall scenarios + GPS tests pass (2026-10-10).
2. 🔨 **Device firmware** (`fall_detector.ino`): IMU ±8 g + self-calibration, fall detection, SOS, GPS, LEDs, buzzer, alerts, serial `l` log / `a` ack. On the board: calibration ✅ (1.115 g → 1.00 g), SOS → alert → `a` ack ✅, 5 min normal handling → 0 false alerts ✅. **Drops failed:** the I2C read dropped out mid-fall every time (jumper wires shaking loose), and the old code then waited 5 s → no alert. Fix uploaded: retry next tick, bus reset after 3 bad reads, config check every 1 s, I2C at 100 kHz. Wiring fixed → impacts 2.5–10 g recorded cleanly. Replay (`tests/replay.cpp`) on real drops: pillow bounce lasts ~900 ms and one wobble killed detection → SETTLE_MS 500→1000, new STILL_FRACTION 70%. Remaining "no alert" was correct: all drops were flat→flat (no tip-over). Re-test with upright→flat drops pending.
3. ✅ **Hotspot + WiFi + dashboard** (2026-10-10): hotspot `FallDetector` (192.168.4.1), joins the home WiFi (192.168.1.39), `http://falldetector.local` resolves on the laptop. `/api/state`, `/api/ack`, incremental graph polling all checked with curl; page renders on phone + laptop sizes; alert banner/history/log checked with a fake-data server. Dashboard adds a live |a| graph and a "detector log" explaining each drop. Flash use 78%.
4. 🔨 Real tests: SOS → alert → ack ✅. Drops (2026-10-11): Saud's drops were all rejected; the data showed the board lay still only ~1.2 s before being picked up, while the detector waited 3 s.
5. 🔨 **Tuning (2026-10-11):** rule changed from "70% still in a fixed 1 s + 2 s window, tipped 50°" to "lies still 0.8 s in one go within 3 s of the hit, tipped 30°". Replay of all recordings: 1 of Saud's 3 latest drops now alerts (the other two: landed the same way up, 9°, and picked up after 0.3 s); old flat→flat drops and normal handling still give 0 alerts. New laptop test "short fall, picked up after 1.2 s → alert". Dashboard sound button now toggles on/off. Device log after that: 1 FALL caught (8.8 g); 3 more drops hit 2.7–11.9 g but tipped only 29°/19°/6° → softened again: `IMPACT_G` 2.5→2.0 g, `ANGLE_DEG` 30→15° (flat→flat drops measured 3–14°, still rejected). Tests + replay pass, uploaded, dashboard shows the 2.0 g line. Waiting for Saud's re-test.

## Notes
- `secrets.h` holds the WiFi + hotspot passwords (chmod 600, gitignored). Never print it.
- Laptop now on the home 2.4 GHz WiFi, the same network the ESP32 will join.
- To simulate a fall: hold the board **upright** (like on a belt), drop it so it lands **flat**. A flat drop that lands flat is correctly not a fall (no tip-over).
- 2026-10-12: Saud's phone hotspot set to the same name/password as the college hotspot (2.4 GHz ch 6, WPA2/WPA3) → ESP32 joined it on its own (10.95.115.78, −54 dBm), `falldetector.local` works on it.
- Not a git repo yet; init when Saud asks to publish.
