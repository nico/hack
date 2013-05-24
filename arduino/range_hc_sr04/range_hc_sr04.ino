const int kTrigger = 12;
const int kEcho = 13;

void setup() {
  Serial.begin(9600);
  pinMode(kTrigger, OUTPUT);
  pinMode(kEcho, INPUT);
}

void loop() {
  http://www.micropik.com/PDF/HCSR04.pdf
  // "...10us pulse to the trigger input to start the ranging..."
  digitalWrite(kTrigger, HIGH);
  delayMicroseconds(10);
  digitalWrite(kTrigger, LOW);
  
  long echo_us = pulseIn(kEcho, HIGH);
  float dist_cm = echo_us / 58.0;  // 340 km/h * echo_us * 100cm
  Serial.println(dist_cm);
  
  delay(100);  // "...we suggest over 60ms..."
}
