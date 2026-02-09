# 🏠 Smart AI-Based Home Energy Monitoring & Automation System

### ESP32 + ESP32-S3-BOX3 Hackathon Project

## 📌 Overview

This project demonstrates a Smart Home Simulation System built using ESP32 Dev Module and ESP32-S3-BOX3. It integrates intelligent automation, adaptive lighting, real-time energy monitoring, abnormal current fault detection, and lightweight predictive energy analytics.

The system simulates a real home where lighting and appliances are automatically controlled based on environmental conditions and usage patterns. It also monitors electricity consumption in real time and predicts the estimated energy bill using a lightweight analytical model.

This project focuses on energy efficiency, safety, and intelligent automation using IoT-based embedded systems.

---

## 🎯 Objectives

* Simulate a smart home environment
* Monitor real-time energy usage
* Automatically control lighting and appliances
* Detect electrical faults using current analysis
* Predict electricity bill using lightweight analytics
* Improve power efficiency and safety

---

## ⚡ Key Features

* Adaptive lighting using LDR sensor
* Motion-based automation using PIR sensor
* Relay-controlled smart appliances
* Real-time voltage, current, power monitoring
* Abnormal current fault detection
* Energy consumption tracking
* Predictive electricity bill estimation
* Lightweight AI-based analytical logic
* Smart display interface using ESP32-S3-BOX3

---

## 🧰 Hardware Components Used

| Component              | Purpose                                      |
| ---------------------- | -------------------------------------------- |
| ESP32 Dev Module       | Main microcontroller                         |
| ESP32-S3-BOX3          | Display & UI interface                       |
| LDR Sensor             | Detect ambient light                         |
| PIR Sensor             | Motion detection                             |
| 4-Channel Relay Module | Control bulbs/appliances                     |
| ACS712                 | Appliance current monitoring                 |
| PZEM-004T              | Voltage, current, power & energy measurement |
| Bulbs/Loads            | Simulated home devices                       |

---

## 🏗️ System Architecture

### Input Layer

* LDR → Detects light intensity
* PIR → Detects human motion
* ACS712 → Measures device current
* PZEM-004T → Measures voltage, current, power, energy

### Processing Layer

* ESP32 collects sensor and energy data
* Compares readings with threshold values
* Detects abnormal current faults
* Runs predictive energy analytics
* Sends output to ESP32-S3 display

### Output Layer

* Relay-controlled bulbs/appliances
* Fault alerts and safety shutdown
* Energy usage display
* Predicted electricity bill

---

## 💡 Adaptive Lighting (LDR-Based Bulb Control)

The LDR sensor automatically adjusts the number of bulbs based on ambient light intensity.

| Condition       | Action        |
| --------------- | ------------- |
| Bright daylight | All bulbs OFF |
| Medium light    | Few bulbs ON  |
| Low light/night | All bulbs ON  |

This ensures efficient lighting and reduced power consumption.

---

## 🚶 Motion-Based Automation

PIR sensor detects movement:

* Motion detected → Appliances ON
* No motion → Energy saving mode
* Night + motion → Lights ON automatically

---

## ⚡ Energy Monitoring

PZEM-004T measures:

* Voltage
* Current
* Power
* Energy consumption (kWh)

ACS712 monitors appliance-level current for safety and analytics.

All data is processed in real time by ESP32.

---

## ⚠️ Fault Detection System

Fault is detected when:

* Current exceeds safe threshold
* Sudden spike/drop in current
* Abnormal power usage
* Device draws current when OFF

### On Fault Detection:

* Relay turns OFF appliance
* Alert displayed on screen
* Prevents electrical damage
* Improves safety

---

## 📊 Predictive Energy Analytics

A lightweight rule-based analytical model predicts electricity consumption and monthly bill.

### Parameters Used:

* Time-of-day usage
* Current consumption
* Appliance usage duration
* Historical usage comparison

### Output:

* Daily energy estimate
* Monthly bill prediction
* Energy optimization insight

This model runs directly on ESP32 without heavy machine learning.

---

## 🔌 Example Pin Connections

| Module  | ESP32 Pin |
| ------- | --------- |
| LDR     | GPIO 34   |
| PIR     | GPIO 27   |
| Relay 1 | GPIO 18   |
| Relay 2 | GPIO 19   |
| Relay 3 | GPIO 21   |
| Relay 4 | GPIO 22   |
| ACS712  | GPIO 35   |
| PZEM TX | GPIO 17   |
| PZEM RX | GPIO 16   |

*(Modify as per wiring setup)*

---

## 💻 Software & Tools

* Arduino IDE / ESP-IDF
* Embedded C / Arduino C++
* ESP32 libraries
* Serial monitor
* IoT analytics logic

---

## 🚀 How to Run

1. Connect sensors and relays to ESP32
2. Connect PZEM-004T to AC load
3. Upload code to ESP32
4. Power ESP32-S3-BOX3
5. Monitor output on display/serial monitor
6. Observe automation and predictions

---

## 🏠 Applications

* Smart homes
* Smart hostels & offices
* Energy monitoring systems
* Electrical safety systems
* IoT-based automation projects

---

## 🔮 Future Enhancements

* Mobile app integration
* Cloud dashboard (AWS/Firebase)
* ML-based prediction
* Voice assistant control
* Solar energy monitoring
* Remote IoT control

---

## 🏆 Hackathon Project

Developed as part of a Smart Home Hackathon
Focus: Energy Intelligence + Automation + Safety

---

## 📜 License

Open-source for educational and research use.
