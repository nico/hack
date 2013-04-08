#include <Servo.h>


const int N = 5;  // Number of sensors.
int sensors[N];
int adjusted[N];

struct { int s_min, s_max; } bounds[N] = {
  {200, 950},
  {200, 950},
  {200, 950},
  {200, 950},
  {200, 950}
};

const int kBlackThreshold = 20;
const int kThreshold = 128;
const int kWhiteThreshold = 230;

Servo left, right;

void setup() {
  Serial.begin(9600);
}

void update_sensors() {
  for (int i = 0; i < N; ++i) {
    sensors[i] = analogRead(i);
    adjusted[i] = map(sensors[i], bounds[i].s_min, bounds[i].s_max, 0, 255);
    adjusted[i] = constrain(adjusted[i], 0, 255);
  }
}

// Depends on the resistor values in the servo and whatnot.
const int kRightBias = - 2;
const int kLeftBias = 8;

const int kSpeed = 60; // 0 .. 90-max(kXBias)

const int kStopRight = 90 + kRightBias;
const int kStopLeft = 90 + kLeftBias;

const int kDriveRight = kStopRight + kSpeed;
const int kDriveLeft = kStopLeft - kSpeed;

void servo_stop(Servo &s, int val) {
#if 0
  s.write(val);
#else
  // The left servo has problems starting again when it's stopped with
  // s.write(kStopLeft). Detaching the servo works a lot more reliably.
  if (s.attached())
    s.detach();
#endif
}

void servo_drive(Servo &s, int val, int pin) {
  if (!s.attached())
    s.attach(pin);
  s.write(val);
}

void drive_forward() {
  servo_drive(left, kDriveLeft, 9);
  servo_drive(right, kDriveRight, 10);
}

void drive_stop() {
  servo_stop(left, kStopLeft);
  servo_stop(right, kStopRight);
}

void drive_right() {
  servo_stop(left, kStopLeft);
  servo_drive(right, kDriveRight, 10);
}

void drive_left() {
  servo_drive(left, kDriveLeft, 9);
  servo_stop(right, kStopRight);
}

void adjust_motors() {
  if (adjusted[2] < kBlackThreshold) {
    // Center sensor is above the black line. Move straight, or stop.
    if (adjusted[1] > kThreshold && adjusted[3] > kThreshold) {
      drive_forward();
    } else if (adjusted[0] + adjusted[1] +
               adjusted[2] + adjusted[3] +
               adjusted[4] < 1) {
      drive_stop();
    }
  } else {
    if (adjusted[0] < kWhiteThreshold && adjusted[4] > kWhiteThreshold)
      drive_right();
    else if (adjusted[0] > kWhiteThreshold && adjusted[4] < kWhiteThreshold)
      drive_left();
    else if (adjusted[1] < kWhiteThreshold && adjusted[3] > kWhiteThreshold)
      drive_right();
    else if (adjusted[1] > kWhiteThreshold && adjusted[3] < kWhiteThreshold)
      drive_left();
  }
}

void print_sensors() {
  for (int i = 0; i < N; ++i) {
    Serial.print("sensor ");
    Serial.print(i);
    Serial.print(":  ");
    Serial.print(sensors[i]);
    Serial.print("  -  ");

    Serial.print("adj: ");
    Serial.print(adjusted[i]);
    if (i != N - 1)
      Serial.print("  -  ");
  }
  Serial.println();
}

void loop() {
  update_sensors();
  adjust_motors();
  print_sensors();
  delay(20);
}
