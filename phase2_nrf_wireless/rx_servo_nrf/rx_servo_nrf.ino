#include <Arduino.h>
#include <SPI.h>
#include <RH_NRF24.h>
#include <Servo.h>

// CE = 4, CSN = 5  (must match wiring)
RH_NRF24 nrf24(4, 5);

Servo myservo;
const int SERVO_PIN = D4;     // keep as-is (GPIO2 on NodeMCU)
int currentAngle = -1;        // track last angle to avoid unnecessary writes

// helper: move servo only if angle changed
void setServoAngle(int angle) {
  if (angle == currentAngle) return;
  myservo.write(angle);
  currentAngle = angle;
  delay(300);                 // allow servo to reach new position
}

// blink built-in LED once
void blinkLedOnce(unsigned long onMs = 150, unsigned long offMs = 150) {
  digitalWrite(LED_BUILTIN, LOW);   // on for ESP8266 (active low)
  delay(onMs);
  digitalWrite(LED_BUILTIN, HIGH);  // off
  delay(offMs);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);  // off (ESP8266 active low)

  myservo.attach(SERVO_PIN, 500, 2400);

  // startup sweep: 0 -> 180 -> 0
  myservo.write(0);
  currentAngle = 0;
  delay(500);

  myservo.write(180);
  currentAngle = 180;
  delay(800);

  myservo.write(0);
  currentAngle = 0;
  delay(800);

  if (!nrf24.init()) {
    Serial.println("NRF init failed");
  } else {
    Serial.println("NRF init OK");
  }

  nrf24.setChannel(3);        // must match TX
  nrf24.setRF(RH_NRF24::DataRate2Mbps, RH_NRF24::TransmitPower0dBm);
}

void loop() {
  uint8_t buf[RH_NRF24_MAX_MESSAGE_LEN];
  uint8_t len = sizeof(buf);

  if (len < 1) {
    Serial.println("RX: empty packet");
    return;
  }

  uint8_t v = buf[0];
  Serial.print("RX recv: ");
  Serial.println(v);

  if (v == 1) {
    // reset: servo to 95°, LED off
    setServoAngle(95);
  } else {
      // deploy: servo to 90°, LED blinks once then stays off
      setServoAngle(0);
      blinkLedOnce();
  }
}
