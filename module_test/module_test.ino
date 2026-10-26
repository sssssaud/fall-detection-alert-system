// Module self-test for the fall-detection board. Serial monitor at 115200.
// At boot: green (Uno LED) -> blue -> red light in turn, then 3 beeps — check by eye/ear.
// Then every round prints PASS/FAIL per module. Pressing SOS = instant beep + red LED.
#include <Wire.h>

const int LED_GREEN = 18, LED_BLUE = 19, LED_RED = 23, BUZZER = 25;
const int SOS_PIN = 33, BAT_PIN = 35, MPU_INT = 34;
const uint8_t MPU = 0x68;
HardwareSerial &gps = Serial2, &sim = Serial1;
int presses = 0;

// tone() so both active and passive buzzers make a sound.
void buzz(bool on) {
  if (on) tone(BUZZER, 2000);
  else noTone(BUZZER);
}

// Called from every wait loop so an SOS press beeps right away.
void sosCheck() {
  static bool was = HIGH;
  static unsigned long lastPress = 0, beepEnd = 0;
  bool now = digitalRead(SOS_PIN);
  if (was == HIGH && now == LOW && millis() - lastPress > 200) {  // 200 ms debounce
    presses++;
    lastPress = millis();
    beepEnd = lastPress + 200;
    digitalWrite(LED_RED, HIGH);
    buzz(true);
  }
  was = now;
  if (beepEnd && millis() > beepEnd) {
    beepEnd = 0;
    digitalWrite(LED_RED, LOW);
    buzz(false);
  }
}

void waitMs(unsigned long ms) {
  for (unsigned long t = millis(); millis() - t < ms;) sosCheck();
}

// Sends an AT command and returns the reply, stopping early on OK/ERROR.
String at(const char *cmd, unsigned long ms = 1500) {
  while (sim.available()) sim.read();
  sim.println(cmd);
  String r;
  for (unsigned long t = millis(); millis() - t < ms;) {
    while (sim.available()) r += (char)sim.read();
    if (r.indexOf("OK\r\n") >= 0 || r.indexOf("ERROR") >= 0) break;
    sosCheck();
  }
  r.trim();
  r.replace("\r\n", " ");
  return r;
}

// nth comma-separated number after key: num("+CREG: 0,1", "+CREG:", 1) == 1. -1 if missing.
long num(const String &s, const char *key, int n) {
  int i = s.indexOf(key);
  if (i < 0) return -1;
  i += strlen(key);
  for (int k = 0; k < n; k++) {
    i = s.indexOf(',', i) + 1;
    if (i == 0) return -1;
  }
  return s.substring(i).toInt();
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

int16_t be16(const uint8_t *p) { return (int16_t)(p[0] << 8 | p[1]); }

void testMpu() {
  uint8_t d[14];
  mpuWrite(0x6B, 0);  // wake up: the chip powers on asleep
  if (!mpuRead(0x3B, d, 14)) {
    Serial.println("MPU6050 : FAIL  no reply at 0x68 - check SDA->21, SCL->22, VCC, GND");
    return;
  }
  float ax = be16(d) / 16384.0, ay = be16(d + 2) / 16384.0, az = be16(d + 4) / 16384.0;  // ±2g range
  float g = sqrt(ax * ax + ay * ay + az * az);
  Serial.printf("MPU6050 : %s  |a|=%.2fg (~1.00 when still)  a=%.2f,%.2f,%.2f  gyro=%.0f,%.0f,%.0f dps\n",
                g > 0.8 && g < 1.2 ? "PASS" : "FAIL", g, ax, ay, az,
                be16(d + 8) / 131.0, be16(d + 10) / 131.0, be16(d + 12) / 131.0);
}

void testGps() {
  while (gps.available()) gps.read();
  String buf;
  for (unsigned long t = millis(); millis() - t < 2000;) {
    while (gps.available()) buf += (char)gps.read();
    sosCheck();
  }
  int g = buf.indexOf("GGA,"), end = g < 0 ? -1 : buf.indexOf('\n', g);
  if (buf.length() == 0) {
    Serial.println("GPS     : FAIL  no data - check GPS TX->16, VCC, GND");
  } else if (end < 0) {
    Serial.printf("GPS     : FAIL  %u bytes but no NMEA - wrong baud or loose wire\n", buf.length());
  } else {
    String gga = buf.substring(g, end);
    gga.trim();
    long fix = num(gga, "GGA,", 5), sats = num(gga, "GGA,", 6);
    Serial.printf("GPS     : PASS  NMEA ok, fix=%ld sats=%ld  %s\n", fix, sats,
                  fix > 0 ? gga.c_str() : "(no fix - needs open sky, normal indoors)");
  }
}

void testSim() {
  String r;
  for (int i = 0; i < 3 && r.indexOf("OK") < 0; i++) r = at("AT", 500);  // first ATs lock auto-baud
  if (r.indexOf("OK") < 0) {
    Serial.printf("SIM800L : FAIL  no reply to AT (got \"%s\") - check TXD->26, RXD->27, VCC from battery, GND\n", r.c_str());
    return;
  }
  at("ATE0");
  String cpin = at("AT+CPIN?"), creg = at("AT+CREG?"), cops = at("AT+COPS?", 3000);
  long sig = num(at("AT+CSQ"), "+CSQ:", 0), reg = num(creg, "+CREG:", 1), mv = num(at("AT+CBC"), "+CBC:", 2);
  int q = cops.indexOf('"');
  String op = q < 0 ? "none" : cops.substring(q + 1, cops.indexOf('"', q + 1));
  bool simOk = cpin.indexOf("READY") >= 0, regOk = reg == 1 || reg == 5;
  const char *regTxt = reg == 1 ? "registered" : reg == 5 ? "registered (roaming)" : reg == 2 ? "searching..."
                     : reg == 3 ? "DENIED (Jio? SIM800L needs a 2G SIM)" : "not registered";
  Serial.printf("SIM800L : %s  modem ok | SIM %s | signal %ld/31 | %s | operator %s | supply %.2fV\n",
                simOk && regOk ? "PASS" : "WARN", simOk ? "ready" : cpin.c_str(), sig, regTxt, op.c_str(), mv / 1000.0);
}

void testBattery() {
  float v = analogReadMilliVolts(BAT_PIN) * 2 / 1000.0;  // 100k/100k divider halves it
  Serial.printf("Battery : %s  %.2fV%s\n", v >= 3.0 && v <= 4.3 ? "PASS" : "CHECK", v,
                v < 0.5 ? "  (nothing on GPIO35 - divider not wired?)" : "");
}

void setup() {
  Serial.begin(115200);
  delay(3000);  // ponytail: time to open the serial monitor after reset
  Serial.println("\n=== module self-test ===");
  if (esp_reset_reason() == ESP_RST_BROWNOUT) Serial.println("!! last reset was a BROWNOUT - supply too weak (SIM800L on ESP32 power?)");

  for (int p : {LED_GREEN, LED_BLUE, LED_RED}) pinMode(p, OUTPUT);
  pinMode(SOS_PIN, INPUT_PULLUP);
  pinMode(MPU_INT, INPUT);
  gps.begin(9600, SERIAL_8N1, 16, 17);
  sim.begin(9600, SERIAL_8N1, 26, 27);
  Wire.begin(21, 22);

  Serial.println("LEDs: green (Uno LED) -> blue -> red, then 3 beeps. Watch and listen.");
  for (int p : {LED_GREEN, LED_BLUE, LED_RED}) {
    digitalWrite(p, HIGH);
    delay(600);
    digitalWrite(p, LOW);
  }
  for (int i = 0; i < 3; i++) {
    buzz(true);
    delay(150);
    buzz(false);
    delay(150);
  }

  Serial.print("I2C scan:");
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", a);
  }
  Serial.println("  (MPU6050 = 0x68)");

  // INT check: data-ready interrupt off -> pin LOW, on -> pin HIGH (latched until status read).
  uint8_t s;
  mpuWrite(0x6B, 0);
  mpuWrite(0x37, 0x20);
  mpuWrite(0x38, 0x00);
  mpuRead(0x3A, &s, 1);
  delay(5);
  bool lo = digitalRead(MPU_INT);
  mpuWrite(0x38, 0x01);
  delay(5);
  bool hi = digitalRead(MPU_INT);
  mpuWrite(0x38, 0x00);
  mpuRead(0x3A, &s, 1);
  Serial.printf("MPU INT : %s\n", !lo && hi ? "PASS  GPIO34 follows the interrupt" : "--    not seen on GPIO34 (optional, fine for now)");
  Serial.println("SOS     : press the button any time - it should beep + flash red");
}

void loop() {
  static int rnd = 0;
  Serial.printf("\n--- round %d ---\n", ++rnd);
  testMpu();
  testGps();
  testSim();
  testBattery();
  Serial.printf("SOS btn : %s  presses=%d, now %s\n", presses ? "PASS" : "WAIT", presses,
                digitalRead(SOS_PIN) ? "released" : "LOW (if always LOW: use diagonal legs of the button)");
  waitMs(1000);
}
