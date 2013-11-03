const int kLeftServo = 3;
const int kRightServo = 2;

// To calibrate, send 1500us high pulses to servo, then adjust pot
// until servo doesn't rotate.

void setup() {
  pinMode(kLeftServo, OUTPUT);
  
    pinMode(13, OUTPUT);
    digitalWrite(13, HIGH);

}

void loop() {
  digitalWrite(kLeftServo, HIGH);

  delayMicroseconds(1500);
  digitalWrite(kLeftServo, LOW);
  delay(10);  // ms
  
}
