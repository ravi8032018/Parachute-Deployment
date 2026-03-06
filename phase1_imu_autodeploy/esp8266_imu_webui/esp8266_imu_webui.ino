#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

ESP8266WebServer server(80);

// choose your own AP name and password
const char* apSsid = "MPU";
const char* apPass = "12341235";

const int SDA_PIN = D2;
const int SCL_PIN = D1;
const uint8_t MPU_ADDR = 0x68;   // change to 0x69 if needed

#define PWR_MGMT_1   0x6B
#define ACCEL_XOUT_H 0x3B
#define GYRO_XOUT_H  0x43

float roll_f = 0.0f;
float pitch_f = 0.0f;
unsigned long lastMicros = 0;

int16_t ax, ay, az;
int16_t gx, gy, gz;
float ax_offset = 0, ay_offset = 0, az_offset = 0;
float gx_offset = 0, gy_offset = 0, gz_offset = 0;

// parachute deploy state
bool  parachuteDeployed = false;
float lastFreeFallA     = 0.0f;

// ---- IMU helpers ----
void writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

int16_t readWord(uint8_t reg) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, (uint8_t)2);
  int16_t v = 0;
  if (Wire.available() == 2) {
    uint8_t hi = Wire.read();
    uint8_t lo = Wire.read();
    v = (int16_t)((hi << 8) | lo);
  }
  return v;
}

void calibrate() {
  const int N = 200;   // number of samples
  long ax_sum = 0, ay_sum = 0, az_sum = 0;
  long gx_sum = 0, gy_sum = 0, gz_sum = 0;

  Serial.println("Calibrating... keep the board still");

  for (int i = 0; i < N; i++) {
    ax = readWord(ACCEL_XOUT_H);
    ay = readWord(ACCEL_XOUT_H + 2);
    az = readWord(ACCEL_XOUT_H + 4);

    gx = readWord(GYRO_XOUT_H);
    gy = readWord(GYRO_XOUT_H + 2);
    gz = readWord(GYRO_XOUT_H + 4);

    ax_sum += ax;
    ay_sum += ay;
    az_sum += az;
    gx_sum += gx;
    gy_sum += gy;
    gz_sum += gz;

    delay(5);
  }

  ax_offset = ax_sum / (float)N;
  ay_offset = ay_sum / (float)N;
  // subtract 1g (16384) on the axis that points “up” when flat; here I assume Z
  az_offset = (az_sum / (float)N) - 16384.0f;

  gx_offset = gx_sum / (float)N;
  gy_offset = gy_sum / (float)N;
  gz_offset = gz_sum / (float)N;

  Serial.println("Calibration done.");
}

// ---- Web UI ----
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>IMU Plane CSS 3D</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    :root{
      --bg-1: #2b0b0b;
      --bg-2: #3a1412;
      --accent-1: #FFB37E;
      --accent-2: #FF6B6B;
      --accent-3: #FFC86B;
      --muted: rgba(255,255,255,0.06);
      --glass: rgba(255,255,255,0.05);
      --text: #FFF4EA;
    }

    * { box-sizing: border-box; }
    body {
      margin: 0;
      background: linear-gradient(180deg, var(--bg-1) 0%, #220a06 70%);
      color: var(--text);
      font-family: Inter, system-ui, -apple-system, 'Segoe UI', Roboto, Arial, sans-serif;
      overflow: hidden;
      -webkit-font-smoothing: antialiased;
      -moz-osx-font-smoothing: grayscale;
    }

    .site-title{
      position: absolute;
      top: 12px;
      left: 50%;
      transform: translateX(-50%);
      color: var(--text);
      font-size: 20px;
      font-weight: 600;
      letter-spacing: 0.3px;
      z-index: 30;
    }
    .display{
      position: absolute;
      top: 10px;
      color: var(--text);
      z-index: 30;
      text-align: center;
      font-weight: 700;
      font-family: inherit;
      line-height: 1.5;
    }
    .display.left{ left: 18px; }
    .display.right{ right: 18px; }
    .display .label{ font-size: 14px; opacity: 0.9; margin-bottom: 1px; }
    .display .value{ font-size: 25px; letter-spacing: 0.3px; }

    #resetBtn{
      margin-top: 6px;
      padding: 4px 10px;
      font-size: 12px;
      border-radius: 6px;
      border: none;
      background: #444;
      color: #fff;
      cursor: pointer;
    }
    #resetBtn:hover{
      background: #666;
    }

    #scene {
      position: absolute;
      top: 0; left: 0;
      width: 100vw;
      height: 115vh;
      display: flex;
      align-items: center;
      justify-content: center;
      perspective: 900px;
      background: radial-gradient(circle at 30% 35%, rgba(255,140,60,0.06) 0%, transparent 20%),
                  radial-gradient(circle at 70% 65%, rgba(255,200,110,0.04) 0%, transparent 18%),
                  linear-gradient(180deg, rgba(31,12,10,0.6), rgba(12,6,6,0.85));
    }

    .plane-wrapper {
      width: 195px;
      height: 400px;
      transform-style: preserve-3d;
      transition: transform 280ms cubic-bezier(.22,.9,.38,1);
      will-change: transform;
      z-index: 10;
      display: grid;
      place-items: center;
    }

    .center-layer{ position: relative; width: 100%; height: 100%; display: grid; place-items: center; }

    .plane{
      --pw: 195px;
      --ph: 300px;
      --pd: 50px;
      width: var(--pw);
      height: var(--ph);
      transform-style: preserve-3d;
      transform-origin: center center;
      position: relative;
      perspective: 800px;
    }

    .plane .box{
      position: absolute;
      left: 50%; top: 50%;
      width: var(--pw); height: var(--ph);
      transform: translate(-50%,-50%);
      transform-style: preserve-3d;
      will-change: transform;
    }

    .face{
      position: absolute;
      left: 0; top: 0;
      backface-visibility: visible;
      -webkit-backface-visibility: hidden;
      overflow: hidden;
    }

    .face.front{
      width: 100%; height: 100%;
      transform: translateZ(calc(var(--pd) / 2));
      background: linear-gradient(90deg, #000 50%, #fff 50%);
      border: 2px solid #000;
    }
    .face.back{
      width: 100%; height: 100%;
      transform: rotateY(180deg) translateZ(calc(var(--pd) / 2));
      background: linear-gradient(90deg, #0a0a0a 0%, #121212 100%);
      border: 2px solid rgba(0,0,0,0.8);
    }

    .face.left, .face.right{
      width: calc(var(--pd));
      height: 100%;
      top: 0;
    }
    .face.left{
      transform: rotateY(90deg) translateZ(calc(var(--pw) / 2 - var(--pd)/2));
      left: calc( (var(--pw) / 2) - (var(--pd) / 2) );
      background: linear-gradient(180deg, #111, #222);
      border-left: 2px solid rgba(0,0,0,0.6);
    }
    .face.right{
      transform: rotateY(-90deg) translateZ(calc(var(--pw) / 2 - var(--pd)/2));
      left: calc( (var(--pw) / 2) + (var(--pd) / 2) - var(--pd) );
      background: linear-gradient(180deg, #ddd, #aaa);
      border-right: 2px solid rgba(0,0,0,0.6);
    }

    .face.top, .face.bottom{
      width: 100%;
      height: calc(var(--pd));
    }
    .face.top{
      transform: rotateX(90deg) translateZ(calc(var(--ph) / 2 - var(--pd)/2));
      top: calc( (var(--ph) / 2) - (var(--pd) / 2) );
      background: linear-gradient(180deg, rgba(194, 97, 97, 0.04), rgba(217, 65, 65, 0.06));
    }
    .face.bottom{
      transform: rotateX(-90deg) translateZ(calc(var(--ph) / 2 - var(--pd)/2));
      top: calc( (var(--ph) / 2) + (var(--pd) / 2) - var(--pd) );
      background: linear-gradient(180deg, rgba(0,0,0,0.06), rgba(255,255,255,0.02));
    }

    .face.front::after{
      content: '';
      position: absolute;
      left: -20%; top: -30%;
      width: 40%; height: 80%;
      background: linear-gradient(120deg, rgba(255,255,255,0.06), rgba(255,255,255,0));
      transform: rotate(-12deg);
      pointer-events: none;
      mix-blend-mode: overlay;
    }

    .wing {
      position: absolute;
      left: -120px;
      right: -120px;
      height: 40px;
      background: gray;
      top: calc(50% - 60px);
      border-radius: 8px;
      box-shadow: rgb(215, 181, 30);
      z-index: 8;
    }
    .tail {
      position: absolute;
      width:40px;
      height: 75px;
      background: gray;
      left: 50%;
      transform: translateX(-50%) rotate(0deg);
      bottom: 80px;
      border-radius: 6px;
      box-shadow: none;
      top: 280px;
      z-index: 8;
    }
    .nose {
      position: absolute;
      width: 76px;
      height:106px;
      background: gray;
      left: 50%;
      transform: translateX(-50%);
      top: -38px;
      border-radius: 50%;
      box-shadow: none;
      border: 2px solid #000;
      z-index: 8;
    }

    .dir-arrow{
      position: absolute;
      left: 50%;
      transform: translateX(-50%);
      top: -105px;
      display: block;
      width: 14px;
      height: 96px;
      pointer-events: none;
    }
    .dir-arrow .shaft{
      position: absolute;
      left: 50%;
      transform: translateX(-50%);
      width: 6px;
      height: 40px;
      background: #000;
      border-radius: 4px;
      top: 60px;
    }
    .dir-arrow .head{
      position: absolute;
      left: 50%;
      transform: translateX(-50%);
      width: 0;
      height: 0;
      border-left: 14px solid transparent;
      border-right: 14px solid transparent;
      border-bottom: 24px solid #000;
      top: 40px;
    }

    .axes{
      position: absolute;
      left: 50%;
      top: 50%;
      transform: translate(-50%,-50%);
      width: 88vmin;
      height: 78vmin;
      pointer-events: none;
      z-index: 5;
      opacity: 0.98;
    }
    .axis{
      position: absolute;
      left: 50%;
      top: 50%;
      transform-origin: center;
      pointer-events: none;
      transition: opacity 200ms;
    }
    .axis::after{ content: ''; position: absolute; }
    .axis-label{
      position: absolute;
      font-size: 16px;
      font-weight: 800;
      color: #fff;
      background: rgba(0,0,0,0.9);
      padding: 6px 8px;
      border-radius: 6px;
      pointer-events: none;
      white-space: nowrap;
    }
    .axis-x{
      width: 100%;
      height: 4px;
      background: #ff2d2d;
      transform: translate(-50%,-50%);
    }
    .axis-x::after{
      width: 0;
      height: 0;
      border-top: 10px solid transparent;
      border-bottom: 10px solid transparent;
      border-left: 18px solid #ff2d2d;
      right: -18px;
      top: 50%;
      transform: translateY(-50%);
    }
    .axis-x .axis-label{ left: 100%; top: 50%; transform: translate(14px,-50%); }
    .axis-y{
      width: 4px;
      height: 100%;
      background: #0066ff;
      transform: translate(-50%,-50%);
    }
    .axis-y::after{
      width: 0;
      height: 0;
      border-left: 10px solid transparent;
      border-right: 10px solid transparent;
      border-top: 18px solid #0066ff;
      left: 50%;
      top: -18px;
      transform: translateX(-50%);
    }
    .axis-y .axis-label{ left: 50%; top: 0%; transform: translate(-50%,-140%); }
    .origin-box{
      position: absolute;
      left: 50%;
      top: 50%;
      transform: translate(-50%,-50%);
      width: 30px;
      height: 30px;
      border-radius: 6px;
      overflow: hidden;
      border: 2px solid rgba(0,0,0,0.6);
      background: transparent;
      transition: transform 180ms ease, filter 180ms ease;
      z-index: 12;
    }
    .origin-box::before, .origin-box::after{
      content: '';
      position: absolute;
      top: 0; bottom: 0; width: 50%;
    }
    .origin-box::before{ left: 0; background: #ff2d2d; }
    .origin-box::after{ right: 0; background: #0066ff; }
    .tilt-readout{
      position: absolute;
      left: 50%;
      top: calc(50% + 36px);
      transform: translate(-50%,0);
      background: rgba(0,0,0,0.18);
      color: var(--text);
      padding: 8px 10px;
      border-radius:6px;
      font-weight:700;
      font-size:14px;
    }

    #info{ z-index: 20; }

    .plane-wrapper.parachute-mode .plane .face.front {
      background: radial-gradient(circle at 50% 20%, #ffffff 0%, #ff6b6b 40%, #c0392b 80%);
      border-radius: 50%;
    }
    .plane-wrapper.parachute-mode .wing,
    .plane-wrapper.parachute-mode .tail,
    .plane-wrapper.parachute-mode .nose,
    .plane-wrapper.parachute-mode .dir-arrow {
      opacity: 0;
    }

    @media (max-width:420px){
      .plane-wrapper{ width: 90px; height: 180px; }
      .plane{ width: 90px; height: 180px; }
    }
  </style>
</head>
<body>
<div id="siteTitle" class="site-title">ESP8266 + MPU6500 Plane</div>

<div id="rollDisplay" class="display left" aria-live="polite">
  <div class="label">Roll</div>
  <div id="rollText" class="value">0.00°</div>
</div>

<div id="pitchDisplay" class="display right" aria-live="polite">
  <div class="label">Pitch</div>
  <div id="pitchText" class="value">0.00°</div>
</div>

<div id="deployDisplay" class="display" style="top: 60px; left: 50%; transform: translateX(-50%);">
  <div class="label">Status</div>
  <div id="deployText" class="value">Waiting...</div>
  <button id="resetBtn" type="button">Reset</button>
</div>

<div id="scene">
  <div class="center-layer">
    <div class="axes" aria-hidden="true">
      <div class="axis axis-x" data-axis="Y">
        <span class="axis-label">Y Axis</span>
      </div>
      <div class="axis axis-y" data-axis="X">
        <span class="axis-label">X Axis</span>
      </div>
      <div class="origin-box" aria-hidden="true"></div>
      <div class="tilt-readout" id="tiltReadout" aria-hidden="true">Tilt: 0.00°</div>
    </div>

    <div class="plane-wrapper" id="planeWrapper" aria-label="plane wrapper">
      <div class="plane" role="img" aria-label="airplane">
        <div class="box" aria-hidden="true">
          <div class="face front"></div>
          <div class="face back"></div>
          <div class="face left"></div>
          <div class="face right"></div>
          <div class="face top"></div>
          <div class="face bottom"></div>
        </div>

        <div class="wing"></div>
        <div class="nose"></div>
        <div class="tail"></div>

        <div class="dir-arrow" aria-hidden="true">
          <div class="shaft"></div>
          <div class="head"></div>
        </div>
      </div>
    </div>
  </div>
</div>

<script>
  let roll = 0, pitch = 0;

  async function updateAngles() {
    try {
      const res = await fetch('/data');
      const obj = await res.json();
      roll  = -(obj.roll) || 0;
      pitch =  (obj.pitch) || 0;

      const deployed = !!obj.deployed;
      const aMag = (obj.a_mag !== undefined) ? obj.a_mag : 0;

      document.getElementById('rollText').textContent  = roll.toFixed(2) + '°';
      document.getElementById('pitchText').textContent = pitch.toFixed(2) + '°';

      const deployTextEl = document.getElementById('deployText');
      const planeWrapper = document.getElementById('planeWrapper');

      if (deployed) {
        deployTextEl.textContent = 'Parachute DEPLOYED |A| = ' + aMag.toFixed(2) + ' g';
        deployTextEl.style.color = '#FF6B6B';
        planeWrapper.classList.add('parachute-mode');
      } else {
        deployTextEl.textContent = 'Waiting for drop...';
        deployTextEl.style.color = '#FFFFFF';
        planeWrapper.classList.remove('parachute-mode');
      }

      updatePlaneTransform();
    } catch (e) {
      console.log('fetch error', e);
    }
  }

  function updatePlaneTransform() {
    const planeWrapper = document.getElementById('planeWrapper');
    const originBox = document.querySelector('.origin-box');
    const tiltReadout = document.getElementById('tiltReadout');

    const rollDeg  = roll;
    const pitchDeg = pitch;

    const transformStr =
      'rotateX(' + (-pitchDeg) + 'deg) ' +
      'rotateY(' + (rollDeg) + 'deg)';

    planeWrapper.style.transform = transformStr;

    const tiltMag = Math.sqrt(rollDeg * rollDeg + pitchDeg * pitchDeg);
    if (tiltReadout) tiltReadout.textContent = 'Tilt: ' + tiltMag.toFixed(2) + '°';

    if (originBox) {
      const y = Math.min(tiltMag * 0.35, 8);
      const blur = Math.min(tiltMag * 0.7 + 4, 18);
      const opacity = Math.min(0.12 + tiltMag / 360, 0.45);
      originBox.style.filter = 'drop-shadow(0 ' + y + 'px ' + blur + 'px rgba(0,0,0,' + opacity + '))';
      const scale = 1 + Math.min(tiltMag / 180, 0.06);
      originBox.style.transform = 'translate(-50%,-50%) scale(' + scale + ')';
    }
  }

  // Reset button: tell ESP to clear deployment state
  document.addEventListener('DOMContentLoaded', () => {
    const btn = document.getElementById('resetBtn');
    if (btn) {
      btn.addEventListener('click', async () => {
        try {
          await fetch('/reset');
        } catch(e) {
          console.log('reset error', e);
        }
      });
    }
  });

  setInterval(updateAngles, 100);
  updateAngles();
</script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleData() {
  String json = "{";
  json += "\"roll\":"   + String(roll_f, 2) + ",";
  json += "\"pitch\":"  + String(pitch_f, 2) + ",";
  json += "\"deployed\":" + String(parachuteDeployed ? "true" : "false") + ",";
  json += "\"a_mag\":" + String(lastFreeFallA, 2);
  json += "}";
  server.send(200, "application/json", json);
}

// reset endpoint: clear deploy state for next test
void handleReset() {
  parachuteDeployed = false;
  lastFreeFallA = 0.0f;
  // also clear free-fall counter implicitly (it’s static in loop and will restart counting)
  server.send(200, "text/plain", "OK");
}

void setup() {
  pinMode(D0, OUTPUT);
  digitalWrite(D0, HIGH);

  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  // wake up MPU6500
  writeReg(PWR_MGMT_1, 0x00);
  delay(100);
  calibrate();

  // Start Wi-Fi Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, apPass);

  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());  // usually 192.168.4.1

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/reset", handleReset);   // NEW

  server.begin();
  Serial.println("HTTP server started");

  lastMicros = micros();

  Serial.println("MPU6500 raw test");
}

void loop() {
  // 1) Time step
  unsigned long now = micros();
  float dt = (now - lastMicros) / 1000000.0f;  // seconds
  lastMicros = now;

  // 2) Read raw
  ax = readWord(ACCEL_XOUT_H);
  ay = readWord(ACCEL_XOUT_H + 2);
  az = readWord(ACCEL_XOUT_H + 4);

  gx = readWord(GYRO_XOUT_H);
  gy = readWord(GYRO_XOUT_H + 2);
  gz = readWord(GYRO_XOUT_H + 4);

  // 3) Apply calibration offsets
  float ax_corr = ax - ax_offset;
  float ay_corr = ay - ay_offset;
  float az_corr = az - az_offset;

  float gx_corr = gx - gx_offset;
  float gy_corr = gy - gy_offset;
  float gz_corr = gz - gz_offset;

  // 4) Convert to units
  const float ACCEL_SCALE = 16384.0f;  // g
  const float GYRO_SCALE  = 131.0f;    // deg/s
  
  float ax_g = ax_corr / ACCEL_SCALE;
  float ay_g = ay_corr / ACCEL_SCALE;
  float az_g = az_corr / ACCEL_SCALE;
  
  float gx_dps = gx_corr / GYRO_SCALE;
  float gy_dps = gy_corr / GYRO_SCALE;
  float gz_dps = gz_corr / GYRO_SCALE;

  // Free-fall detection
  static uint16_t freeFallCount = 0;
  const float FREE_FALL_THRESH = 0.2f;   // g
  const uint16_t FREE_FALL_SAMPLES = 5;  // e.g. 5 samples in a row

  float free_fall = sqrt(ax_g*ax_g + ay_g*ay_g + az_g*az_g);

  if (!parachuteDeployed) {  // only detect once until reset
    if (free_fall <= FREE_FALL_THRESH) {
      freeFallCount++;
      if (freeFallCount >= FREE_FALL_SAMPLES) {
        parachuteDeployed = true;
        lastFreeFallA = free_fall;

        Serial.print("|A| = ");
        Serial.print(free_fall, 2);
        Serial.println("  DEPLOY PARACHUTE!!!");
        // deployParachute();
      }
    } else {
      freeFallCount = 0;
      //    Serial.print("|A| = ");
      //    Serial.print(free_fall, 2);
      //    Serial.println("  Hold, it's not free fall yet.");
    }
  } else {
    // if you want to, you could still log free_fall here after deployment
  }

  // 5) Angles from accelerometer
  float roll_acc  = atan2(ay_g, az_g) * 180.0f / PI;
  float pitch_acc = atan2(-ax_g, sqrt(ay_g*ay_g + az_g*az_g)) * 180.0f / PI;

  // 6) Integrate gyro to update filtered angles
  // (assuming gx_dps is rotation around X, gy_dps around Y)
  roll_f  += gx_dps * dt;
  pitch_f += gy_dps * dt;

  // 7) Complementary filter
  const float alpha = 0.98f;  // 98% gyro, 2% accel
  roll_f  = alpha * roll_f  + (1.0f - alpha) * roll_acc;
  pitch_f = alpha * pitch_f + (1.0f - alpha) * pitch_acc;

  // 8) Print for debugging
  //  Serial.print("Roll_acc: ");
  //  Serial.print(roll_acc);
  //  Serial.print("  Pitch_acc: ");
  //  Serial.print(pitch_acc);
  //
  //  Serial.print("  | Roll_f: ");
  //  Serial.print(roll_f);
  //  Serial.print("  Pitch_f: ");
  //  Serial.println(pitch_f);

  server.handleClient();

  delay(10);  // small delay so loop is ~100 Hz
}
