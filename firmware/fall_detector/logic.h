// Pure logic (no Arduino calls): fall detection + NMEA GGA parsing.
// Tested on the laptop by tests/test_logic.cpp — run it after changing anything here.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Fall detection tuning settings: set these from real recordings (LOG_IMU mode) ----
const float FREEFALL_G = 0.5f;           // |a| below this = falling
const uint32_t FREEFALL_MS = 60;         // ...for at least this long
const float IMPACT_G = 2.0f;             // |a| above this = hit the ground (real drops: 2.7-12 g)
const uint32_t IMPACT_WINDOW_MS = 1000;  // impact must come this soon after free-fall
const uint32_t STILL_MS = 800;           // after the hit it must lie still this long in one go...
const uint32_t CONFIRM_MS = 3000;        // ...finishing within this long (pillow bounce ~1.1 s, quick pick-up ~1.2 s)
const float STILL_TOL_G = 0.3f;          // still = |a| within 1 g ± this
const float STILL_DPS = 60;              // ...and turning slower than this (deg/s)
const float ANGLE_DEG = 15;              // body must have tipped over at least this much (hand drops: 19-36, flat->flat: 3-14)

struct FallDetector {
  enum State { WATCHING, FREEFALL, IMPACT } state = WATCHING;
  float gx = 0, gy = 0, gz = 1;  // gravity direction while upright (slow average)
  float bx = 0, by = 0, bz = 1;  // gravity direction just before the fall
  float sx = 0, sy = 0, sz = 0;  // sum of the still samples after impact
  uint32_t stillStart = 0;       // when the current still stretch began
  uint32_t longestMs = 0;        // longest still stretch since impact (for the log)
  bool lying = false;            // inside a still stretch
  uint32_t t0 = 0;               // start of the current phase
  uint32_t fallStart = 0;
  bool falling = false;
  float peakG = 0;               // biggest impact, reported with the alert

  // Feed one sample: accel in g, rotation speed in deg/s, time in ms.
  // Returns true exactly once when a fall is confirmed.
  bool update(float ax, float ay, float az, float dps, uint32_t t) {
    float g = sqrtf(ax * ax + ay * ay + az * az);
    switch (state) {
      case WATCHING:
        if (g >= FREEFALL_G) {
          falling = false;
          if (fabsf(g - 1) < 0.2f) {  // only learn "upright" while not shaking
            gx += 0.02f * (ax - gx);
            gy += 0.02f * (ay - gy);
            gz += 0.02f * (az - gz);
          }
        } else if (!falling) {
          falling = true;
          fallStart = t;
        } else if (t - fallStart >= FREEFALL_MS) {
          state = FREEFALL;
          t0 = t;
          falling = false;
          bx = gx, by = gy, bz = gz;
        }
        return false;

      case FREEFALL:
        if (g > IMPACT_G) {
          state = IMPACT;
          t0 = t;
          peakG = g;
          lying = false;
          longestMs = 0;
        } else if (t - t0 > IMPACT_WINDOW_MS) {
          state = WATCHING;
        }
        return false;

      case IMPACT:
        if (g > peakG) peakG = g;
        if (fabsf(g - 1) <= STILL_TOL_G && dps <= STILL_DPS) {
          if (!lying) lying = true, stillStart = t, sx = sy = sz = 0;
          sx += ax, sy += ay, sz += az;
          if (t - stillStart > longestMs) longestMs = t - stillStart;
          if (t - stillStart >= STILL_MS) {  // lying still: fall only if it also tipped over
            state = WATCHING;
            return angleDeg(bx, by, bz, sx, sy, sz) >= ANGLE_DEG;
          }
        } else {
          lying = false;  // bouncing, getting up, being picked up
        }
        if (t - t0 > CONFIRM_MS) state = WATCHING;
        return false;
    }
    return false;
  }

  // After an IMPACT ends without a fall: why, in plain words.
  void whyNot(char *buf, size_t n) const {
    if (longestMs < STILL_MS)
      snprintf(buf, n, "not a fall: lay still only %lu ms (need %lu)", (unsigned long)longestMs, (unsigned long)STILL_MS);
    else
      snprintf(buf, n, "not a fall: tipped %d deg (need %d)", (int)angleDeg(bx, by, bz, sx, sy, sz), (int)ANGLE_DEG);
  }

  static float angleDeg(float ax, float ay, float az, float bx, float by, float bz) {
    float d = sqrtf(ax * ax + ay * ay + az * az) * sqrtf(bx * bx + by * by + bz * bz);
    if (d == 0) return 0;
    float c = (ax * bx + ay * by + az * bz) / d;
    return acosf(c > 1 ? 1 : c < -1 ? -1 : c) * 57.29578f;
  }
};

// ---- GPS ----
struct GpsFix {
  bool valid = false;       // true = has a position fix
  int sats = 0;
  double lat = 0, lon = 0;  // decimal degrees, south/west negative
};

// "ddmm.mmmm" (or "dddmm.mmmm") + hemisphere -> decimal degrees.
inline double nmeaToDeg(const char *v, char hemi) {
  double x = atof(v);
  int deg = (int)(x / 100);
  double d = deg + (x - deg * 100) / 60.0;
  return (hemi == 'S' || hemi == 'W') ? -d : d;
}

// True if the *XX checksum matches, so lines garbled on the wire are rejected.
inline bool nmeaChecksumOk(const char *line) {
  const char *star = strchr(line, '*');
  if (*line != '$' || !star || !star[1] || !star[2]) return false;
  uint8_t sum = 0;
  for (const char *p = line + 1; p < star; p++) sum ^= (uint8_t)*p;
  return sum == strtol(star + 1, nullptr, 16);
}

// Parses one GGA sentence ($GPGGA / $GNGGA). Returns false if it isn't a valid GGA line.
// On a line without a fix, out.valid is false and lat/lon are left untouched.
inline bool parseGGA(const char *line, GpsFix &out) {
  if (strlen(line) < 7 || strncmp(line, "$G", 2) != 0 || strncmp(line + 3, "GGA,", 4) != 0) return false;
  if (!nmeaChecksumOk(line)) return false;
  char buf[100];
  strncpy(buf, line, sizeof buf - 1);
  buf[sizeof buf - 1] = 0;
  const char *f[10] = {};
  int n = 0;
  for (char *p = buf; n < 10;) {
    f[n++] = p;
    char *c = strchr(p, ',');
    if (!c) break;
    *c = 0;
    p = c + 1;
  }
  if (n < 8) return false;
  out.sats = atoi(f[7]);
  out.valid = atoi(f[6]) > 0 && *f[2] && *f[4];
  if (out.valid) {
    out.lat = nmeaToDeg(f[2], *f[3]);
    out.lon = nmeaToDeg(f[4], *f[5]);
  }
  return true;
}
