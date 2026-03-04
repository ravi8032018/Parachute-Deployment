# Parachute-Deployment

An ESP8266-based parachute deployment system with a web-based control panel and NRF24L01 wireless link.

The project started as an **IMU-driven autonomous deployer** using an MPU6500 to detect free fall and trigger a servo. In the second phase, it evolved into a **two-node wireless system** where a transmitter board sends deployment commands to a receiver board over NRF24L01, allowing remote and manual control of the parachute mechanism.

---

## Features

- ESP8266 running as a Wi‑Fi access point with a responsive web UI
- Two-button control interface: **Deploy** and **Reset**
- NRF24L01 wireless link between transmitter and receiver
- Servo actuation on receiver side (0° = safe, 90° = deployed)
- Visual feedback using responsive web UI (deployed vs reset)
- Earlier phase: MPU6500-based free‑fall detection with complementary filtering for roll/pitch

---

## Project phases

### Phase 1 – IMU-based deployment

- Hardware:
  - ESP8266 (NodeMCU)
  - MPU6500 IMU
  - Servo for parachute release

- Logic:
  - ESP8266 reads raw accelerometer and gyro data from the MPU6500 over I²C.
  - Data is calibrated and converted to physical units (g, deg/s).
  - A complementary filter blends accelerometer and gyro data to compute roll and pitch.
  - Free‑fall is detected by monitoring the magnitude of acceleration \(|A|\) and comparing it to a threshold (e.g. `< 0.2 g` for a number of consecutive samples).
  - Once free‑fall is confirmed, the ESP8266:
    - Marks the system as “deployed”
    - Drives the servo from the locked position to the deploy angle
    - Updates a 3D plane visualization on a web page (roll/pitch driven by IMU data)

This phase validated the sensor fusion and free‑fall detection pipeline and proved the mechanical deployment concept.

### Phase 2 – NRF24L01 wireless deployment (current)

- Hardware:
  - ESP8266 (NodeMCU) as **transmitter**
  - ESP8266 / Arduino as **receiver**
  - NRF24L01 modules on both sides
  - Servo attached to receiver
  - On‑board LED for status indication

- Transmitter:
  - Runs a Wi‑Fi access point and minimal HTTP server.
  - Serves a clean web page with two buttons: **DEPLOY** and **RESET**.
  - On **DEPLOY**:
    - Sends a 1‑byte command `1` via NRF24L01.
    - Updates internal `parachuteDeployed` state.
    - Optionally toggles the on‑board LED.
  - On **RESET**:
    - Sends a 1‑byte command `0` via NRF24L01.
    - Clears `parachuteDeployed`.
    - Resets LED / any attached actuators.

- Receiver:
  - Listens continuously for incoming NRF24L01 packets on the same channel.
  - When a packet is received:
    - `0` → sets servo to 0°, turns LED off (safe / reset).
    - `1` → sets servo to 90°, turns LED on (deploy).
  - Only moves the servo when the target angle changes to avoid unnecessary writes.

This architecture decouples the user interface (web, ESP8266 AP) from the physical deployment mechanism, improving reliability and range.

---

## Technologies used

- **Microcontrollers**
  - ESP8266 (NodeMCU)

- **Wireless**
  - NRF24L01 RF transceivers
  - Custom 1‑byte command protocol for deploy/reset

- **Sensors (Phase 1)**
  - MPU6500 IMU (accelerometer + gyroscope)

- **Firmware / Libraries**
  - Arduino core for ESP8266
  - Wi‑Fi soft AP + HTTP server (ESP8266WebServer)
  - RadioHead `RH_NRF24` driver for NRF24L01
  - Servo library for deployment mechanism

- **Frontend**
  - Vanilla HTML, CSS, and JavaScript
  - Responsive layout and simple status feedback

---

## Repository structure (suggested)

```text
Parachute-Deployment/
  ├─ phase1_imu_autodeploy/
  │   ├─ mpu_scanner/              # optional: I2C / IMU scanner sketch
  │   │   └─ mpu_scanner.ino
  │   ├─ esp8266_imu_webui/        # main phase 1 firmware
  │   │   └─ esp8266_imu_webui.ino
  │   └─ README.md                 # short note for phase 1
  │
  ├─ phase2_nrf_wireless/
  │   ├─ tx_esp8266_web/           # transmitter (ESP8266 + Web UI + NRF24)
  │   │   └─ tx_esp8266_web.ino
  │   ├─ rx_servo_nrf/             # receiver (NRF24 + servo)
  │   │   └─ rx_servo_nrf.ino
  │   └─ README.md                 # short note for phase 2
  │
  ├─ README.md                     # main project README
  └─ docs/                         # diagrams, photos, notes (optional)


