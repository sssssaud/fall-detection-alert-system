// Replays a recorded IMU log (serial 'l' mode) through the real FallDetector and explains each decision.
//   g++ -std=c++17 -o /tmp/replay tests/replay.cpp && /tmp/replay < capture.log
// Lines it reads: "[HH:MM:SS ]t_ms,ax,ay,az,dps"; everything else is ignored.
#include <cstdio>
#include <cstring>
#include "../firmware/fall_detector/logic.h"

int main() {
  FallDetector d;
  char line[200];
  uint32_t first = 0;
  while (fgets(line, sizeof line, stdin)) {
    const char *p = strchr(line, ' ');
    p = p ? p + 1 : line;
    unsigned long t;
    float ax, ay, az, dps;
    if (sscanf(p, "%lu,%f,%f,%f,%f", &t, &ax, &ay, &az, &dps) != 5) continue;
    if (!first) first = t;
    float s = (t - first) / 1000.0f, g = sqrtf(ax * ax + ay * ay + az * az);
    auto before = d.state;
    bool fell = d.update(ax, ay, az, dps, t);
    if (fell) {
      printf("%7.2fs  FALL (peak %.2f g)\n", s, d.peakG);
    } else if (before != d.state) {
      if (d.state == FallDetector::FREEFALL) printf("%7.2fs  free-fall\n", s);
      else if (d.state == FallDetector::IMPACT) printf("%7.2fs    impact %.2f g\n", s, g);
      else if (before == FallDetector::FREEFALL) printf("%7.2fs    no impact within %u ms\n", s, IMPACT_WINDOW_MS);
      else {
        char why[80];
        d.whyNot(why, sizeof why);
        printf("%7.2fs    %s\n", s, why);
      }
    }
  }
}
