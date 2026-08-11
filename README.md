# IoT-Enabled Energy Monitoring and Load Protection System

An ESP32-based IoT system for real-time electrical energy monitoring, remote visualization, automated load protection, and tamper detection. The system integrates the PZEM-004T energy measurement module, Blynk IoT, SIM900A GSM communication, relay-based load control, SW-420 vibration sensing, and EEPROM-backed data persistence.

## Overview

The system is designed as a connected embedded platform for monitoring electrical parameters and protecting an electrical load when the measured power exceeds a configured operating limit.

An ESP32 acts as the central controller and integrates:

- PZEM-004T for electrical measurement
- 16×2 I2C LCD for local monitoring
- Blynk IoT for remote visualization
- SIM900A for GSM/SMS communication
- Relay module for automated load control
- SW-420 vibration sensor for tamper detection
- EEPROM emulation for persistent energy data

The project combines sensing, embedded control, communication, local indication, remote monitoring, and hardware protection within a single prototype.

---

## Key Features

- Real-time measurement of voltage, current, power, and energy
- Measurement of frequency and power factor
- ESP32-based embedded control
- Blynk IoT dashboard for remote monitoring
- GSM/SMS alerts through SIM900A
- Configurable power-limit protection
- Automatic relay-based load disconnection during overload
- Vibration-based tamper detection
- Local parameter display using a 16×2 I2C LCD
- SMS-based system interaction and status reporting
- EEPROM-backed persistence of cumulative energy data
- Wi-Fi connectivity with background reconnection handling

---

## System Architecture

The system is organized into four major functional groups:

1. **Input and Measurement**
   - PZEM-004T electrical measurement
   - SW-420 vibration/tamper detection

2. **Embedded Controller**
   - ESP32 DevKit V1
   - Measurement processing
   - Protection logic
   - Communication handling
   - Data persistence

3. **Output and Load Control**
   - Relay module
   - 16×2 I2C LCD
   - Connected electrical load

4. **Communication and IoT**
   - SIM900A GSM/SMS communication
   - ESP32 Wi-Fi
   - Blynk IoT dashboard
   - EEPROM-backed data storage

### Architecture Diagram

![System Architecture](./hardware/system_architecture.jpg)  
---

## System Workflow

The ESP32 initializes the measurement, display, communication, and storage interfaces before entering the monitoring loop.

During normal operation:

1. Electrical parameters are read from the PZEM-004T.
2. The ESP32 evaluates the measured power against the configured limit.
3. The relay state is updated according to the protection condition.
4. Electrical parameters are displayed on the LCD.
5. Measurements are transmitted to the Blynk IoT dashboard.
6. The SW-420 sensor is monitored for vibration or tamper events.
7. GSM alerts are generated when configured system events occur.
8. Cumulative energy data is periodically stored in EEPROM.
9. Wi-Fi connectivity is monitored and restored when necessary.

### Flowchart

![System Flowchart](./hardware/system_flowchart.jpg)

---

## Hardware Components

| Component | Function |
|---|---|
| ESP32 DevKit V1 | Main controller |
| PZEM-004T v3.0 | Voltage, current, power, energy, frequency and power-factor measurement |
| SIM900A | GSM/SMS communication |
| SW-420 | Vibration and tamper detection |
| Relay Module | Electrical load switching and protection |
| 16×2 I2C LCD | Local display |
| LM2596 Buck Converter | 12 V to 5 V power conversion |
| 3.3 V Regulator | Low-voltage peripheral supply |
| 12 V DC Supply | System power source |

---

## Circuit and Pin Configuration

The major peripheral connections are:

| Device | Signal | ESP32 GPIO | Interface |
|---|---|---:|---|
| PZEM-004T | TX | GPIO 16 | UART2 RX |
| PZEM-004T | RX | GPIO 17 | UART2 TX |
| SIM900A | TX | GPIO 27 | UART1 RX |
| SIM900A | RX | GPIO 26 | UART1 TX |
| Relay | Control | GPIO 25 | Digital Output |
| SW-420 | DO | GPIO 33 | Digital Input |
| LCD | SDA | GPIO 21 | I2C |
| LCD | SCL | GPIO 22 | I2C |

![Circuit Diagram](./hardware/circuit_diagram.png)

---

## Electrical Measurement

The PZEM-004T provides the following electrical parameters to the ESP32:

- Voltage
- Current
- Real power
- Cumulative energy
- Frequency
- Power factor

The firmware requests measurements periodically and retains the previous valid values when required measurement data cannot be obtained.

---

## Load Protection

The system uses a configurable power threshold to protect the connected load.

When:

```text
Measured Power > Configured Power Limit
