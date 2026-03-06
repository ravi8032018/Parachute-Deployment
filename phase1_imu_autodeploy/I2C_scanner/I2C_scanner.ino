#include <Wire.h>

const int SDA_PIN = D2;  // adjust if you used other pins
const int SCL_PIN = D1;

void setup() {
  Serial.begin(115200);
  pinMode(D0,OUTPUT);
  digitalWrite(D0,HIGH);
  Wire.begin(SDA_PIN, SCL_PIN);
  Serial.println("I2C scanner");
}

void loop() {
  Serial.println("Scanning...");
  byte count = 0;
  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("Found device at 0x");
      Serial.println(address, HEX);
      count++;
    }
    delay(5);
  }
  if (count == 0) Serial.println("No I2C devices found");
  Serial.println();
  delay(1000);
}
