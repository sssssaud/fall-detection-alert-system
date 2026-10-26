// Laptop tests for logic.h:
//   g++ -std=c++17 -o /tmp/test_logic tests/test_logic.cpp && /tmp/test_logic
#include <cassert>
#include <cstdio>
#include <string>
#include "../firmware/fall_detector/logic.h"

// Feeds a constant reading at 100 Hz and counts reported falls.
// On the belt: upright = gravity along x, lying down = gravity along z.
struct Sim {
  FallDetector d;
  uint32_t t = 0;
  int falls = 0;
  void hold(float ax, float ay, float az, float dps, uint32_t ms) {
    for (uint32_t end = t + ms; t < end; t += 10) falls += d.update(ax, ay, az, dps, t);
  }
  void fallDown() {  // free-fall, impact, bounce
    hold(0.2f, 0, 0, 300, 300);
    hold(3.5f, 0.5f, 0.5f, 300, 30);
    hold(0.6f, 0.3f, 0.9f, 200, 300);
  }
};

std::string withSum(const std::string &body) {
  unsigned sum = 0;
  for (char c : body) sum ^= (unsigned char)c;
  char h[4];
  snprintf(h, sizeof h, "*%02X", sum);
  return "$" + body + h;
}

int main() {
  {  // real fall: upright, falls, lies still on its side
    Sim s;
    s.hold(1, 0, 0, 5, 2000);
    s.fallDown();
    s.hold(0, 0, 1, 5, 3000);
    assert(s.falls == 1);
    assert(s.d.peakG > 3.4f);
  }
  {  // pillow: bounces for ~0.9 s and rolls a little before lying still (real drop data)
    Sim s;
    s.hold(1, 0, 0, 5, 2000);
    s.hold(0.2f, 0, 0, 300, 300);
    s.hold(5.0f, 0.5f, 0.5f, 300, 30);
    for (int i = 0; i < 3; i++) s.hold(0.69f, 0.2f, 0.3f, 120, 150), s.hold(1.35f, 0.2f, 0.6f, 120, 150);
    s.hold(0, 0.1f, 1, 65, 200);
    s.hold(0, 0, 1, 5, 3000);
    assert(s.falls == 1);
  }
  {  // short fall: landed, lay still ~1.2 s, then picked up (real drop data) -> counts
    Sim s;
    s.hold(1, 0, 0, 5, 2000);
    s.fallDown();
    s.hold(0, 0, 1, 5, 1200);
    s.hold(1.4f, 0.3f, 0.3f, 150, 2000);
    assert(s.falls == 1);
  }
  {  // landed, then picked up 1 s after impact
    Sim s;
    s.hold(1, 0, 0, 5, 2000);
    s.fallDown();
    s.hold(0, 0, 1, 5, 700);
    s.hold(1.4f, 0.3f, 0.3f, 150, 2000);
    assert(s.falls == 0);
  }
  {  // dropped flat and landed flat: no tip-over, not a fall
    Sim s;
    s.hold(0, 0, 1, 5, 2000);
    s.fallDown();
    s.hold(0, 0, 1, 5, 3000);
    assert(s.falls == 0);
  }
  {  // falls but gets up straight away
    Sim s;
    s.hold(1, 0, 0, 5, 2000);
    s.fallDown();
    s.hold(0, 0, 1, 5, 500);
    s.hold(1.8f, 0, 0.5f, 200, 500);
    s.hold(1, 0, 0, 5, 3000);
    assert(s.falls == 0);
  }
  {  // sitting down hard: dip and bump, no free-fall
    Sim s;
    s.hold(1, 0, 0, 5, 2000);
    s.hold(0.7f, 0, 0, 50, 200);
    s.hold(1.4f, 0, 0, 80, 150);
    s.hold(1, 0, 0, 5, 3000);
    assert(s.falls == 0);
  }
  {  // free-fall with no impact (caught softly)
    Sim s;
    s.hold(1, 0, 0, 5, 2000);
    s.hold(0.3f, 0, 0, 100, 200);
    s.hold(1, 0, 0, 5, 3000);
    assert(s.falls == 0);
  }
  {  // walking, then a jump and walking on
    Sim s;
    s.hold(1, 0, 0, 5, 1000);
    for (int i = 0; i < 20; i++) s.hold(0.6f, 0, 0, 80, 250), s.hold(1.6f, 0, 0, 80, 250);
    s.hold(0.1f, 0, 0, 100, 200);
    s.hold(3.0f, 0, 0, 150, 30);
    for (int i = 0; i < 10; i++) s.hold(1.5f, 0.2f, 0, 150, 250), s.hold(0.6f, 0, 0.2f, 150, 250);
    assert(s.falls == 0);
  }
  {  // falls, gets up later, falls again: detector re-arms
    Sim s;
    s.hold(1, 0, 0, 5, 2000);
    s.fallDown();
    s.hold(0, 0, 1, 5, 3000);
    s.hold(1.5f, 0, 0.5f, 200, 500);
    s.hold(1, 0, 0, 5, 3000);
    s.fallDown();
    s.hold(0, 1, 0, 5, 3000);
    assert(s.falls == 2);
  }

  GpsFix f;
  assert(parseGGA("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47", f));
  assert(f.valid && f.sats == 8);
  assert(fabs(f.lat - 48.1173) < 1e-4 && fabs(f.lon - 11.516667) < 1e-4);

  GpsFix none;
  assert(parseGGA("$GPGGA,,,,,,0,00,99.99,,,,,,*48", none));
  assert(!none.valid && none.sats == 0);

  assert(!parseGGA("$GPGGA,123519,4807.038,N,01131.000,E,1,09,0.9,545.4,M,46.9,M,,*47", f));  // garbled
  assert(!parseGGA("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A", f));
  assert(!parseGGA("$GP", f));

  assert(parseGGA(withSum("GNGGA,101010,1952.572,N,07520.598,E,1,07,1.1,560.0,M,-70.0,M,,").c_str(), f));
  assert(f.valid && fabs(f.lat - 19.8762) < 1e-4 && fabs(f.lon - 75.3433) < 1e-4);
  assert(parseGGA(withSum("GPGGA,1,3352.000,S,15112.000,W,1,05,1,1,M,1,M,,").c_str(), f));
  assert(f.lat < 0 && f.lon < 0);

  puts("all logic tests passed");
}
