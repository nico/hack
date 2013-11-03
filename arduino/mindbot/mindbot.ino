const int kLeftServo = 3;
const int kRightServo = 2;
const int kSensorPin = 4;
const int kLedPin = 13;

// To calibrate, send 1500us high pulses to servo, then adjust pot
// until servo doesn't rotate.

void setup() {
  pinMode(kLeftServo, OUTPUT);
  pinMode(kRightServo, OUTPUT);
  pinMode(kLedPin, OUTPUT);
  pinMode(kSensorPin, INPUT);
}

void loop() {
  //forward();
  if (digitalRead(kSensorPin) == LOW) {
    digitalWrite(kLedPin, HIGH);
  } else {
    digitalWrite(kLedPin, LOW);
  }
  delay(20);  // ms
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
