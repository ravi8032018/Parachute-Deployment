#include <Servo.h>

Servo myservo;

void setup() {
  myservo.attach(D4, 500, 2400);  // correct for full rotation

  myservo.write(0);    // start at 0°
  delay(800);          // give servo time to reach position

  myservo.write(180);   // rotate to 90° exactly once
  delay(3000);  
  // time to reach target
 
}


void loop() {
  myservo.write(0);    // start at 0°
  delay(1500);          // give servo time to reach position
myservo.write(90);   // rotate to 90° exactly once
  delay(1500);
  myservo.write(180);   // rotate to 90° exactly once
  delay(1500);  
  /// Do nothing (servo already moved once)
}
