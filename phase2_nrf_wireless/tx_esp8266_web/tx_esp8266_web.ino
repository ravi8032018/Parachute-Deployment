#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Servo.h>

#include <SPI.h>
#include <RH_NRF24.h> 

// NRF – CE = 4, CSN = 5 (must match wiring on both TX and RX)
RH_NRF24 nrf24(4, 5);

ESP8266WebServer server(80);

// choose your own AP name and password
const char* apSsid = "MPU";
const char* apPass = "12341235";

// parachute deploy state
bool  parachuteDeployed = false;
float lastFreeFallA     = 0.0f;   // not used, kept for status JSON if needed
Servo parachuteServo;

const int SERVO_PIN = D6;           // local servo on TX (optional)
const int SERVO_ARM_ANGLE = 0;
const int SERVO_DEPLOY_ANGLE = 90;

bool servoMoved = false;           // prevents repeated movement

// dummy attitude values for JSON compatibility (always 0)
float roll_f  = 0.0f;
float pitch_f = 0.0f;

// ---- NRF send helper ----
void sendNrfCommand(uint8_t value) {
  uint8_t data[1];
  data[0] = value;
  Serial.print("NRF TX sending: ");
  Serial.println(data[0]);
  nrf24.send(data, sizeof(data));
  nrf24.waitPacketSent();
}

// ---- Web UI ----
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Parachute Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    :root {
      --bg: #0b1020;
      --bg-card: #151b30;
      --accent-deploy: #ff4d4d;
      --accent-reset: #4da3ff;
      --accent-deploy-hover: #ff6d6d;
      --accent-reset-hover: #6bb4ff;
      --text: #f5f7ff;
      --muted: #9aa3c0;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
      background: radial-gradient(circle at top, #1b2340, #050814);
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      color: var(--text);
    }
    .card {
      width: min(420px, 94vw);
      padding: 24px 20px;
      border-radius: 16px;
      background: linear-gradient(145deg, #171f38, #0f1424);
      box-shadow:
        0 20px 40px rgba(0,0,0,0.6),
        0 0 0 1px rgba(255,255,255,0.03);
      text-align: center;
    }
    .title {
      margin: 0 0 6px;
      font-size: 22px;
      font-weight: 600;
    }
    .subtitle {
      margin: 0 0 18px;
      font-size: 13px;
      color: var(--muted);
    }
    .buttons {
      display: flex;
      gap: 14px;
      justify-content: center;
      margin-top: 8px;
    }
    button {
      flex: 1;
      border-radius: 999px;
      padding: 12px 14px;
      border: none;
      cursor: pointer;
      font-size: 15px;
      font-weight: 600;
      letter-spacing: 0.03em;
      color: #ffffff;
      transition: transform 0.15s ease, box-shadow 0.15s ease, background 0.15s ease;
    }
    #deployBtn {
      background: linear-gradient(135deg, var(--accent-deploy), #ff8c66);
      box-shadow: 0 10px 20px rgba(255,77,77,0.35);
    }
    #deployBtn:hover {
      background: linear-gradient(135deg, var(--accent-deploy-hover), #ff9f7d);
      transform: translateY(-1px);
      box-shadow: 0 14px 26px rgba(255,77,77,0.45);
    }
    #resetBtn {
      background: linear-gradient(135deg, var(--accent-reset), #6bd0ff);
      box-shadow: 0 10px 20px rgba(75,150,255,0.32);
    }
    #resetBtn:hover {
      background: linear-gradient(135deg, var(--accent-reset-hover), #83d7ff);
      transform: translateY(-1px);
      box-shadow: 0 14px 26px rgba(75,150,255,0.42);
    }
    .status {
      margin-top: 14px;
      font-size: 13px;
      color: var(--muted);
    }
    .status span {
      font-weight: 600;
      color: #ffd36b;
    }
  </style>
</head>
<body>
  <div class="card">
    <h1 class="title">Parachute Control</h1>
    <p class="subtitle">Use the buttons below to deploy or reset the parachute system.</p>

    <div class="buttons">
      <button id="deployBtn" type="button">DEPLOY</button>
      <button id="resetBtn" type="button">RESET</button>
    </div>

    <p class="status">Status: <span id="statusText">Waiting…</span></p>
  </div>

  <script>
    async function callEndpoint(path, label) {
      const statusEl = document.getElementById('statusText');
      statusEl.textContent = label + '...';
      try {
        const res = await fetch(path, { method: 'GET', cache: 'no-store' });
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const text = await res.text();
        statusEl.textContent = label + ' OK (' + text + ')';
      } catch (e) {
        console.log(e);
        statusEl.textContent = label + ' failed';
      }
    }

    document.addEventListener('DOMContentLoaded', () => {
      document.getElementById('deployBtn').addEventListener('click', () => {
        callEndpoint('/deploy', 'Deploy');
      });
      document.getElementById('resetBtn').addEventListener('click', () => {
        callEndpoint('/reset', 'Reset');
      });
    });
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

// optional: simple JSON status
void handleData() {
  String json = "{";
  json += "\"roll\":"   + String(roll_f, 2) + ",";
  json += "\"pitch\":"  + String(pitch_f, 2) + ",";
  json += "\"deployed\":" + String(parachuteDeployed ? "true" : "false") + ",";
  json += "\"a_mag\":" + String(lastFreeFallA, 2);
  json += "}";
  server.send(200, "application/json", json);
}

// reset endpoint: clear deploy state and send 0
void handleReset() {
  parachuteDeployed = false;
  lastFreeFallA = 0.0f;
  servoMoved = false;
  parachuteServo.write(SERVO_ARM_ANGLE);  // re-arm local servo
  sendNrfCommand(0);                      // tell receiver "reset"
  server.send(200, "text/plain", "reset");
}

// deploy endpoint: set deploy state and send 1
void handleDeploy() {
  if (!parachuteDeployed) {
    parachuteDeployed = true;
  }

  if (!servoMoved) {
    parachuteServo.write(SERVO_DEPLOY_ANGLE);
    servoMoved = true;
    Serial.println("Servo rotated (web button): Parachute mechanism deployed");
  }

  sendNrfCommand(1);                      // tell receiver "deploy"
  server.send(200, "text/plain", "deployed");
}

void setup() {
  pinMode(D0, OUTPUT);
  digitalWrite(D0, HIGH);

  Serial.begin(115200);
  delay(1000);

  // Start Wi-Fi Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, apPass);

  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());  // usually 192.168.4.1

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/reset", handleReset);
  server.on("/deploy", handleDeploy);

  server.begin();
  Serial.println("HTTP server started");

  // local servo (optional)
  parachuteServo.attach(SERVO_PIN, 500, 2400);
  parachuteServo.write(SERVO_ARM_ANGLE);  // start locked

  // NRF init
  if (!nrf24.init()) {
    Serial.println("NRF init failed");
  } else {
    Serial.println("NRF init OK");
  }
  nrf24.setChannel(3);
  nrf24.setRF(RH_NRF24::DataRate2Mbps, RH_NRF24::TransmitPower0dBm);
}

void loop() {
  server.handleClient();
  delay(10);
}
