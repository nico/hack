const int kLeftServo = 3;
const int kRightServo = 2;
const int kSensorPin = 4;
const int kLedPin = 13;

const int kSpeed = 25;

// Servo rotating backwards are a bit slower for some reason.
const float kBackwardServoSlop = 0.6;

// To calibrate, send 1500us high pulses to servo, then adjust pot
// until servo doesn't rotate.

void setup() {
  pinMode(kLeftServo, OUTPUT);
  pinMode(kRightServo, OUTPUT);
  pinMode(kLedPin, OUTPUT);
  pinMode(kSensorPin, INPUT);

  randomSeed(analogRead(0));
}

void loop() {
  digitalWrite(kLedPin, obstacleDetected() ? HIGH : LOW);
  delay(10);  // ms
  if (obstacleDetected()) {
    evade();
  } else {
    forward();
  }
  //delay(500);
  //backward();
}

bool obstacleDetected() {
  return digitalRead(kSensorPin) == LOW;
}

void evade() {
  for (int i = 0; i < 12; ++i)
    backward();
  for (int i = 0, e = 19 * random(1, 4); i < e; ++i)
    turn();
}

void turn() {
  // 18 turns are about 90 deg
  for (int i = 0; i < 10; ++i) {
    pulseServo(kLeftServo, 1500 + kSpeed);  // us
    pulseServo(kRightServo, 1500 + kSpeed);  // us
  }
}

void forward() {
  for (int i = 0; i < 10; ++i) {
    pulseServo(kLeftServo, 1500 - kSpeed / kBackwardServoSlop);  // us
    pulseServo(kRightServo, 1500 + kSpeed);  // us
  }
}

void backward() {
  for (int i = 0; i < 10; ++i) {
    pulseServo(kLeftServo, 1500 + kSpeed);  // us
    pulseServo(kRightServo, 1500 - kSpeed / kBackwardServoSlop);  // us
  }
}

void pulseServo(int pin, int us) {
  digitalWrite(pin, HIGH);

  delayMicroseconds(us);
  digitalWrite(pin, LOW);
  delay(5);  // ms
}
