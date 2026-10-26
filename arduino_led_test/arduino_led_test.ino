// Mirrors the ESP32 "green LED" signal (GPIO18) onto the Uno's built-in LED.
// Wiring: ESP32 GPIO18 -> 1k resistor -> Uno D7, ESP32 GND -> Uno GND.
// ponytail: D7 not D13 — D13 *is* the built-in LED, so it can't also be the input.

const int ESP_PIN = 7;

void setup() {
  pinMode(ESP_PIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(9600);
}

void loop() {
  static int last = -1;
  int state = digitalRead(ESP_PIN);
  digitalWrite(LED_BUILTIN, state);
  if (state != last) {
    Serial.println(state ? "ESP32 pin HIGH -> LED ON" : "ESP32 pin LOW -> LED OFF");
    last = state;
  }
}
