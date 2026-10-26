// Blinks GPIO18 (the "green LED" pin) once a second.
// The Uno running arduino_led_test should blink its built-in LED in step.

const int GREEN_LED = 18;

void setup() {
  pinMode(GREEN_LED, OUTPUT);
  Serial.begin(115200);
}

void loop() {
  digitalWrite(GREEN_LED, HIGH);
  Serial.println("GPIO18 HIGH");
  delay(1000);
  digitalWrite(GREEN_LED, LOW);
  Serial.println("GPIO18 LOW");
  delay(1000);
}
