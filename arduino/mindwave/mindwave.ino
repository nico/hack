// Based on NeuroSky's Mindwave Arduino sample code, and code from
// "Make a Mind-Controlled Arduino Robot".

// The pdf describing the protocol is better than the sample code:
// http://wearcam.org/ece516/mindset_communications_protocol.pdf


const int buttonPin = 7;
const int piezoPin = 8;
const int redPin = 11;
const int greenPin = 10;
const int bluePin = 9;
const int builtinLedPin = 13;

#define BAUDRATE 115200


void setup() {
  pinMode(buttonPin, INPUT);

  pinMode(piezoPin, OUTPUT);  
  pinMode(redPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(builtinLedPin, OUTPUT);

  Serial.begin(BAUDRATE);
  connectHeadset();
}

void connectHeadset() {
  setGreen();
  delay(3000) ;
  Serial.write(0xc2);  // request auto-connect
  setWhite();
  delay(100);
}

void setGreen() { setColor(0, 255, 0); }
void setWhite() { setColor(255, 255, 255); }
void setYellow() { setColor(255, 255, 0); }
void setBlueToRed(float redPercent) { setColor(255 * redPercent, 0, 255 * (1 - redPercent)); }

void setColor(int red, int green, int blue) {
  analogWrite(redPin, red);
  analogWrite(greenPin, green);
  analogWrite(bluePin, blue);
}

byte ReadOneByte() {
  while(!Serial.available())
    ;
  return Serial.read();
}

byte payloadData[169] = {0};
int readMindwave(byte* attention, byte* meditation) {
  // Look for sync bytes
  if (ReadOneByte() != 170) return -1;
  if (ReadOneByte() != 170) return -1;
  
  int payloadLength = ReadOneByte();
  while (payloadLength == 170)
    payloadLength = ReadOneByte();

  if (payloadLength > 169) return -2;

  byte generatedChecksum = 0;        
  for(int i = 0; i < payloadLength; i++) {  
    payloadData[i] = ReadOneByte();
    generatedChecksum += payloadData[i];
  }   
  generatedChecksum = ~generatedChecksum;

  byte checksum = ReadOneByte();
  if (checksum != generatedChecksum) return -3;

  byte poorQuality = 200;
  *attention = 0xff;
  *meditation = 0xff;

  unsigned bytesParsed = 0;
  const int EXCODE = 0x55;
  while (bytesParsed < payloadLength) {
    while (payloadData[bytesParsed] == EXCODE)
      ++bytesParsed;
    byte code = payloadData[bytesParsed++];
    
    byte length = (code & 0x80) ? payloadData[bytesParsed++] : 1;

    switch (code) {
      case 2:
        poorQuality = payloadData[bytesParsed];
        if (poorQuality == 200) {
          setYellow();
          return -4;
        }
        break;
      case 4:
        *attention = payloadData[bytesParsed];
        break;
      case 5:
        *meditation = payloadData[bytesParsed];
        break;
      case 16:  // blink detected: blink strength
        toggleTinyLed();
        break;
      case 0xd0:
        sayHeadsetConnected();
        break;
      // 0x80: raw value, single big-endian 16bit two-complements value
      // 0x83: ASIG EEG POWER: 8 3-byte values
      // 0xd0: headset connected, 2 bytes headset id
      // 0xd1: headset not found, 2 bytes headset id
      // 0xd2: headset disconnected, 2 bytes headset id
      // 0xd3: request denied
      // 0xd4: dongle in standby mode / trying to find headset (1 data byte)
      case 0xd1:
      case 0xd2:
      case 0xd3:
        wave(piezoPin, 900, 500);
        setWhite();
        return -5;
    }

    bytesParsed += length;
  }
  return 0;
}

void sayHeadsetConnected() {
  wave(piezoPin, 440, 40);
  delay(25);
  wave(piezoPin, 300, 20);
  wave(piezoPin, 540, 40);
  delay(25);
  wave(piezoPin, 440, 20);
  wave(piezoPin, 640, 40);
  delay(25);
  wave(piezoPin, 540, 40);
  delay(25);
}

void toggleTinyLed() {
  static bool state = false;
  digitalWrite(builtinLedPin, state ? HIGH : LOW);
  state = !state;
}

void loop() {

  byte attention, meditation;
  if (readMindwave(&attention, &meditation) == 0) {
    int state = digitalRead(buttonPin);
    byte value = (state == HIGH) ? attention : meditation;

    if (value != 0xff) {
      float percent = value / 100.f;
      //tone(piezoPin, 220 + value + 220, 10 /* ms */);
      setBlueToRed(percent);
    }
  } else {
    // Happens often.
  }
  //  delay(2);
}

void wave(int pin, float f, int ms) {
  // like tone() but synchronous and without interfering with pwm on 3 and 11
  float period = 1 / f * 1000 * 1000;  // microseconds
  long start = millis();
  while (millis() - start < ms) {
    digitalWrite(pin, HIGH);
    delayMicroseconds(period / 2);
    digitalWrite(pin, LOW);
    delayMicroseconds(period / 2);
  }
}


