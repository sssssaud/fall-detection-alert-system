// Fall detector firmware. Design: docs/superpowers/specs/2026-10-10-fall-detector-design.md
// Serial monitor at 115200. Commands: 'l' = toggle 100 Hz IMU log (t_ms,ax,ay,az,dps), 'a' = acknowledge alert.
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include "logic.h"
#include "secrets.h"
#include "dashboard.h"

const int LED_GREEN = 18, LED_BLUE = 19, LED_RED = 23, BUZZER = 25, SOS_PIN = 33;
const int GPS_RX = 16, GPS_TX = 17;
const uint8_t MPU = 0x68;
const float ACC_LSB = 4096.0f;       // ±8 g range
const float GYRO_LSB = 65.5f;        // ±500 deg/s range
const uint32_t BUZZ_MAX_MS = 60000;  // buzzer gives up after this; red LED stays on

HardwareSerial &gps = Serial2;
FallDetector fall;
float accOff[3], gyroOff[3];
bool imuOk = false, calibrated = false, logImu = false;
float lastG = 1;

GpsFix gpsNow;   // latest GGA: sats + fix flag
GpsFix gpsLast;  // last good position
bool gpsHasLast = false;
uint32_t gpsLastAt = 0, gpsDataAt = 0;

struct Alert {
  uint32_t id, at;   // number, millis() when raised
  const char *type;  // "FALL" or "SOS"
  float peakG;
  bool hasFix;
  double lat, lon;
  uint32_t fixAgeS;  // how old the position was when the alert fired
};
Alert alerts[10];    // newest first
int alertCount = 0;
bool alertActive = false;
uint32_t alertStart = 0, nextAlertId = 1;

WebServer server(80);
DNSServer dns;

// |a| for the live graph: 25 Hz, last ~5 s. Each entry is the sample furthest from 1 g out of 4,
// so free-fall dips and impact spikes both survive the down-sampling.
const int HIST = 128;
float hist[HIST];
uint32_t histSeq = 0;  // samples stored so far

// Detector log for the dashboard: why each drop did or didn't count as a fall.
const int LOGN = 8;
char logText[LOGN][80];
uint32_t logAt[LOGN], logSeq = 0;

void logEvent(const char *fmt, ...) {
  char *slot = logText[logSeq % LOGN];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(slot, sizeof logText[0], fmt, ap);
  va_end(ap);
  logAt[logSeq % LOGN] = millis();
  logSeq++;
  Serial.printf("detector: %s\n", slot);
}

void mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

bool mpuRead(uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(MPU);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(MPU, n) != n) return false;
  for (int i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

// Accel in g and gyro in deg/s, offsets applied.
bool readImu(float a[3], float w[3]) {
  uint8_t d[14];
  if (!mpuRead(0x3B, d, 14)) return false;
  for (int i = 0; i < 3; i++) {
    a[i] = (int16_t)(d[2 * i] << 8 | d[2 * i + 1]) / ACC_LSB - accOff[i];
    w[i] = (int16_t)(d[8 + 2 * i] << 8 | d[9 + 2 * i]) / GYRO_LSB - gyroOff[i];
  }
  return true;
}

bool configImu() {
  uint8_t id;
  if (!mpuRead(0x75, &id, 1)) return false;
  mpuWrite(0x6B, 0x00);  // wake up
  mpuWrite(0x1A, 0x02);  // ~94 Hz low-pass: keeps impact peaks, drops noise
  mpuWrite(0x1B, 0x08);  // gyro ±500 deg/s
  mpuWrite(0x1C, 0x10);  // accel ±8 g
  delay(50);
  return true;
}

// Learns offsets; the device must lie still (flat, chip side up) for 2 s.
// ponytail: treats the accel error as lying along gravity during calibration (true for this
// module's Z offset when calibrated flat). A 6-side calibration is the upgrade if needed.
bool calibrate() {
  float m[3] = {}, w0[3] = {}, a[3], w[3], gmin = 99, gmax = 0;
  const int N = 200;
  memset(accOff, 0, sizeof accOff);
  memset(gyroOff, 0, sizeof gyroOff);
  for (int i = 0; i < N; i++) {
    if (!readImu(a, w)) return false;
    float g = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    gmin = fminf(gmin, g);
    gmax = fmaxf(gmax, g);
    for (int k = 0; k < 3; k++) m[k] += a[k] / N, w0[k] += w[k] / N;
    delay(10);
  }
  if (gmax - gmin > 0.08f) return false;  // it moved
  float len = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
  for (int k = 0; k < 3; k++) accOff[k] = m[k] - m[k] / len, gyroOff[k] = w0[k];
  Serial.printf("IMU: calibrated (still reading was %.3f g)  accel offset %.3f,%.3f,%.3f g  gyro offset %.1f,%.1f,%.1f dps\n",
                len, accOff[0], accOff[1], accOff[2], gyroOff[0], gyroOff[1], gyroOff[2]);
  return true;
}

// Loose wires during a drop can glitch the bus, or blip the MPU's power so it resets to
// sleep at ±2 g. Reset the bus and re-apply the settings; only report an error if that fails.
uint8_t imuFails = 0;
void recoverImu() {
  Wire.end();
  Wire.begin(21, 22);
  Wire.setClock(100000);
  imuOk = configImu();
  imuFails = 0;
  Serial.println(imuOk ? "IMU: glitch, recovered" : "IMU: lost - check wires");
}

bool calibrateWithRetries() {
  for (int i = 0; i < 3; i++) {
    if (calibrate()) return true;
    Serial.println("IMU: moved during calibration, retrying - keep it still");
  }
  Serial.println("IMU: could not calibrate, running without offsets");
  return false;
}

void raiseAlert(const char *type, float peakG) {
  memmove(&alerts[1], &alerts[0], sizeof(Alert) * 9);
  uint32_t now = millis();
  alerts[0] = {nextAlertId++, now, type, peakG, gpsHasLast, gpsLast.lat, gpsLast.lon,
               gpsHasLast ? (now - gpsLastAt) / 1000 : 0};
  if (alertCount < 10) alertCount++;
  alertActive = true;
  alertStart = now;
  if (gpsHasLast)
    Serial.printf("ALERT #%lu %s (peak %.1f g) at %.6f,%.6f (%lus old)\n", alerts[0].id, type, peakG,
                  gpsLast.lat, gpsLast.lon, alerts[0].fixAgeS);
  else
    Serial.printf("ALERT #%lu %s (peak %.1f g) - no GPS fix yet\n", alerts[0].id, type, peakG);
}

void ackAlert() {
  if (!alertActive) return;
  alertActive = false;
  Serial.println("alert acknowledged");
}

void imuTick(uint32_t now) {
  float a[3], w[3];
  if (!readImu(a, w)) {
    if (++imuFails >= 3) recoverImu();  // one bad read is retried on the next tick
    return;
  }
  imuFails = 0;
  static uint32_t nextCfgCheck = 0;
  uint8_t cfg;
  if ((int32_t)(now - nextCfgCheck) >= 0 && mpuRead(0x1C, &cfg, 1)) {
    nextCfgCheck = now + 1000;
    if (cfg != 0x10) recoverImu();  // MPU reset itself (power blip)
  }
  float dps = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
  lastG = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  if (logImu) Serial.printf("%lu,%.3f,%.3f,%.3f,%.1f\n", now, a[0], a[1], a[2], dps);

  static float pick = 1;
  static int picked = 0;
  if (fabsf(lastG - 1) > fabsf(pick - 1)) pick = lastG;
  if (++picked == 4) {
    hist[histSeq++ % HIST] = pick;
    pick = 1;
    picked = 0;
  }

  auto before = fall.state;
  if (fall.update(a[0], a[1], a[2], dps, now)) {
    logEvent("FALL detected (impact %.1f g)", fall.peakG);
    raiseAlert("FALL", fall.peakG);
  } else if (fall.state != before) {
    if (fall.state == FallDetector::FREEFALL) logEvent("free-fall started");
    else if (fall.state == FallDetector::IMPACT) logEvent("impact %.1f g - checking if it lies still and tipped over", lastG);
    else if (before == FallDetector::FREEFALL) logEvent("no impact after free-fall - not a fall");
    else {
      char why[80];
      fall.whyNot(why, sizeof why);
      logEvent("%s", why);
    }
  }
}

void readGps(uint32_t now) {
  static char line[100];
  static size_t len = 0;
  while (gps.available()) {
    char c = gps.read();
    gpsDataAt = now;
    if (c == '\n') {
      line[len] = 0;
      len = 0;
      if (parseGGA(line, gpsNow) && gpsNow.valid) {
        gpsLast = gpsNow;
        gpsLastAt = now;
        gpsHasLast = true;
      }
    } else if (c != '\r' && len < sizeof line - 1) {
      line[len++] = c;
    }
  }
}

void sosTick(uint32_t now) {
  static bool was = HIGH;
  static uint32_t lastPress = 0;
  bool pressed = digitalRead(SOS_PIN);
  if (was == HIGH && pressed == LOW && now - lastPress > 300) {  // 300 ms debounce
    lastPress = now;
    raiseAlert("SOS", lastG);
  }
  was = pressed;
}

void updateOutputs(uint32_t now) {
  static bool buzzing = false;
  bool buzz = alertActive && now - alertStart < BUZZ_MAX_MS && (now / 250) % 2 == 0;
  if (buzz != buzzing) {
    buzzing = buzz;
    if (buzz) tone(BUZZER, 2000);  // tone() so active and passive buzzers both sound
    else noTone(BUZZER);
  }
  digitalWrite(LED_RED, imuOk ? alertActive : (now / 100) % 2);  // fast blink = IMU error
  digitalWrite(LED_BLUE, gpsNow.valid);
  digitalWrite(LED_GREEN, WiFi.status() == WL_CONNECTED ? HIGH : (now / 500) % 2);  // blink = hotspot only
}

const char *STATE_NAMES[] = {"watching", "free-fall", "impact"};

void printStatus(uint32_t now) {
  Serial.printf("g=%.2f  %s  gps: %s sats=%d  alert: %s  wifi: %s  hotspot clients: %d\n", lastG,
                imuOk ? STATE_NAMES[fall.state] : "IMU ERROR",
                now - gpsDataAt > 3000 ? "no data" : gpsNow.valid ? "fix" : "searching", gpsNow.sats,
                alertActive ? alerts[0].type : "none",
                WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "not connected",
                WiFi.softAPgetStationNum());
}

// ---- Network: own hotspot always on, plus the strongest known WiFi when in range ----

void wifiTick(uint32_t now) {
  static uint32_t nextScan = 0;
  static bool wasUp = false;
  bool up = WiFi.status() == WL_CONNECTED;
  if (up != wasUp) {
    wasUp = up;
    if (up) Serial.printf("WiFi: joined %s - dashboard at http://%s or http://falldetector.local\n",
                          WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    else Serial.println("WiFi: not connected (the hotspot still works)");
  }
  if (up) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  if (n >= 0) {
    int best = -1, bestRssi = -1000;
    for (int i = 0; i < n; i++)
      for (const Net &k : WIFI_NETS)
        if (WiFi.SSID(i) == k.ssid && WiFi.RSSI(i) > bestRssi) best = &k - WIFI_NETS, bestRssi = WiFi.RSSI(i);
    WiFi.scanDelete();
    if (best >= 0) {
      Serial.printf("WiFi: joining %s (%d dBm)\n", WIFI_NETS[best].ssid, bestRssi);
      WiFi.begin(WIFI_NETS[best].ssid, WIFI_NETS[best].pass);
    }
    return;
  }
  // ponytail: a scan briefly disturbs the hotspot, so only every 60 s while no known WiFi is joined
  if ((int32_t)(now - nextScan) >= 0) {
    nextScan = now + 60000;
    WiFi.scanNetworks(true);
  }
}

String jsonEsc(const String &s) {
  String o;
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    if ((uint8_t)c >= 32) o += c;
  }
  return o;
}

void handleState() {
  uint32_t now = millis();
  uint32_t since = server.arg("since").toInt();
  if (since > histSeq || histSeq - since > HIST) since = histSeq > HIST ? histSeq - HIST : 0;  // new page or reboot
  bool sta = WiFi.status() == WL_CONNECTED;
  char b[300];
  String j;
  j.reserve(4096);

  snprintf(b, sizeof b, "{\"up\":%lu,\"th\":[%.1f,%.1f],\"imu\":{\"ok\":%s,\"state\":\"%s\",\"g\":%.2f},",
           now / 1000, FREEFALL_G, IMPACT_G, imuOk ? "true" : "false", STATE_NAMES[fall.state], lastG);
  j += b;
  j += "\"net\":{\"ap_ssid\":\"";
  j += jsonEsc(AP_SSID);
  j += "\",\"ap_ip\":\"";
  j += WiFi.softAPIP().toString();
  j += "\",\"clients\":";
  j += WiFi.softAPgetStationNum();
  j += ",\"sta\":";
  j += sta ? "true" : "false";
  if (sta) {
    j += ",\"ssid\":\"";
    j += jsonEsc(WiFi.SSID());
    j += "\",\"ip\":\"";
    j += WiFi.localIP().toString();
    j += "\",\"rssi\":";
    j += WiFi.RSSI();
  }
  snprintf(b, sizeof b, "},\"gps\":{\"data\":%s,\"fix\":%s,\"sats\":%d,\"has_last\":%s,\"lat\":%.6f,\"lon\":%.6f,\"age\":%lu},",
           now - gpsDataAt < 3000 ? "true" : "false", gpsNow.valid ? "true" : "false", gpsNow.sats,
           gpsHasLast ? "true" : "false", gpsLast.lat, gpsLast.lon, gpsHasLast ? (now - gpsLastAt) / 1000 : 0);
  j += b;

  snprintf(b, sizeof b, "\"seq\":%lu,\"g\":[", histSeq);
  j += b;
  for (uint32_t s = since; s < histSeq; s++) {
    snprintf(b, sizeof b, s > since ? ",%.2f" : "%.2f", hist[s % HIST]);
    j += b;
  }
  j += "],\"log\":[";
  for (uint32_t i = 0; i < LOGN && i < logSeq; i++) {
    uint32_t k = (logSeq - 1 - i) % LOGN;  // newest first
    snprintf(b, sizeof b, "%s{\"ago\":%lu,\"text\":\"%s\"}", i ? "," : "", (now - logAt[k]) / 1000, logText[k]);
    j += b;
  }
  snprintf(b, sizeof b, "],\"alert_active\":%s,\"alerts\":[", alertActive ? "true" : "false");
  j += b;
  for (int i = 0; i < alertCount; i++) {
    const Alert &a = alerts[i];
    snprintf(b, sizeof b, "%s{\"id\":%lu,\"type\":\"%s\",\"ago\":%lu,\"peak\":%.1f,\"fix\":%s,\"lat\":%.6f,\"lon\":%.6f,\"fix_age\":%lu}",
             i ? "," : "", a.id, a.type, (now - a.at) / 1000, a.peakG, a.hasFix ? "true" : "false", a.lat, a.lon, a.fixAgeS);
    j += b;
  }
  j += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

void startNetwork() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  dns.start(53, "*", WiFi.softAPIP());  // captive portal: every name -> the dashboard
  MDNS.begin("falldetector");
  MDNS.addService("http", "tcp", 80);

  server.on("/", [] { server.send(200, "text/html", DASHBOARD_HTML); });
  server.on("/api/state", handleState);
  server.on("/api/ack", HTTP_POST, [] {
    ackAlert();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.onNotFound([] {  // phones probe random URLs to detect a login page; send them to the dashboard
    server.sendHeader("Location", "http://" + server.client().localIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();
  Serial.printf("Hotspot \"%s\" is on - dashboard at http://%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  for (int p : {LED_GREEN, LED_BLUE, LED_RED}) pinMode(p, OUTPUT);
  pinMode(SOS_PIN, INPUT_PULLUP);
  gps.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  Wire.begin(21, 22);
  Wire.setClock(100000);  // 100 kHz: more forgiving of long jumper wires
  delay(1000);  // ponytail: time to open the serial monitor after reset
  Serial.println("\n=== fall detector ===  keep it still and flat while it calibrates");
  imuOk = configImu();
  if (imuOk) calibrated = calibrateWithRetries();
  else Serial.println("IMU: not found - check SDA 21, SCL 22, VCC, GND");
  startNetwork();
}

void loop() {
  static uint32_t nextImu = 0, nextStatus = 0, lastImuTry = 0;
  uint32_t now = millis();

  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'l') logImu = !logImu;
    if (c == 'a') ackAlert();
  }
  if (imuOk && (int32_t)(now - nextImu) >= 0) {
    nextImu = now + 10;  // 100 Hz
    imuTick(now);
  } else if (!imuOk && now - lastImuTry > 5000) {  // IMU missing: retry every 5 s
    lastImuTry = now;
    recoverImu();
    if (imuOk && !calibrated) calibrated = calibrateWithRetries();
  }
  readGps(now);
  sosTick(now);
  updateOutputs(now);
  server.handleClient();
  wifiTick(now);
  if (!logImu && (int32_t)(now - nextStatus) >= 0) {
    nextStatus = now + 2000;
    printStatus(now);
  }
}
