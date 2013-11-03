const int kLeftServo = 3;
const int kRightServo = 2;

// To calibrate, send 1500us high pulses to servo, then adjust pot
// until servo doesn't rotate.

void setup() {
  pinMode(kLeftServo, OUTPUT);
  pinMode(kRightServo, OUTPUT);
}

void loop() {
  forward();  
}

void forward() {
  for (int i = 0; i < 10; ++i) {
    pulseServo(kLeftServo, 1500 - 500);  // us
    pulseServo(kRightServo, 1500 + 500);  // us
  }
}

void pulseServo(int pin, int us) {
  digitalWrite(pin, HIGH);

  delayMicroseconds(us);
  digitalWrite(pin, LOW);
  delay(5);  // ms

}
