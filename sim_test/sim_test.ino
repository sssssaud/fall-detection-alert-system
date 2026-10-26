// SIM800L-only test. Serial monitor at 115200. Repeats every few seconds so you can fix wires live.
// 1) Is SIM TXD driving GPIO26? (an idle UART line sits HIGH)  2) Find the baud  3) Basic status.

const int SIM_RX = 26, SIM_TX = 27;  // ESP32 side: RX <- SIM TXD, TX -> SIM RXD
const long BAUDS[] = {9600, 115200, 57600, 38400, 19200};
HardwareSerial &sim = Serial1;

// Shows a reply with \r \n and junk bytes visible, so garbage (wrong baud) is obvious.
String show(const String &s) {
  String o;
  for (char c : s) {
    if (c == '\r') o += "\\r";
    else if (c == '\n') o += "\\n";
    else if (c >= 32 && c < 127) o += c;
    else {
      char h[6];
      snprintf(h, sizeof h, "<%02X>", (uint8_t)c);
      o += h;
    }
  }
  return o;
}

// Sends an AT command and returns the raw reply, stopping early on OK/ERROR.
String at(const char *cmd, unsigned long ms = 800) {
  while (sim.available()) sim.read();
  sim.print(cmd);
  sim.print("\r");
  String r;
  for (unsigned long t = millis(); millis() - t < ms;) {
    while (sim.available()) r += (char)sim.read();
    if (r.indexOf("OK\r\n") >= 0 || r.indexOf("ERROR") >= 0) break;
  }
  return r;
}

void setup() {
  Serial.begin(115200);
  delay(3000);  // ponytail: time to open the serial monitor after reset
  Serial.println("\n=== SIM800L test ===");
}

void loop() {
  static int rnd = 0;
  Serial.printf("\n--- round %d ---\n", ++rnd);

  sim.end();
  pinMode(SIM_RX, INPUT_PULLDOWN);
  delay(5);
  bool rxHigh = digitalRead(SIM_RX);
  Serial.printf("Line    : GPIO26 %s\n", rxHigh
                ? "HIGH - SIM TXD is powered and connected"
                : "LOW - nothing drives it: SIM has no power, or TXD is not on D26");
  pinMode(SIM_TX, INPUT_PULLDOWN);
  delay(5);
  if (!rxHigh && digitalRead(SIM_TX)) Serial.println("Line    : GPIO27 HIGH - SIM is powered, but TXD/RXD look swapped (TXD must go to D26)");

  long found = 0;
  for (long b : BAUDS) {
    sim.begin(b, SERIAL_8N1, SIM_RX, SIM_TX);
    String r;
    for (int i = 0; i < 3 && r.indexOf("OK") < 0; i++) r = at("AT", 400);  // first ATs lock auto-baud
    Serial.printf("Baud %6ld: %s\n", b, r.length() ? show(r).c_str() : "(nothing)");
    if (r.indexOf("OK") >= 0) {
      found = b;
      break;
    }
    sim.end();
  }
  if (!found) {
    Serial.println("Result  : FAIL - no OK at any baud");
    delay(2000);
    return;
  }

  Serial.printf("Result  : PASS - modem answers at %ld baud\n", found);
  at("ATE0");
  for (const char *c : {"ATI", "AT+CPIN?", "AT+CSQ", "AT+CREG?", "AT+CBC"})
    Serial.printf("  %-9s -> %s\n", c, show(at(c, 1500)).c_str());
  sim.end();
  delay(2000);
}
