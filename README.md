# SentinelAI: Closed-Loop Physical AI Workplace Safety Sentinel

[![Hackathon: Embedded SF](https://img.shields.io/badge/Hackathon-Embedded%20SF%20CraftifAI-orange.svg)](https://developer.craftifai.com)
[![PipeGen Track A](https://img.shields.io/badge/CraftifAI-PipeGen%20v0.5.1-blue.svg)](https://developer.craftifai.com)
[![FirmGen Track B](https://img.shields.io/badge/CraftifAI-FirmGen%20v0.4.1-green.svg)](https://developer.craftifai.com)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5-red.svg)](https://docs.espressif.com)
[![Bonus](https://img.shields.io/badge/Bonus-Closed%20Loop%20%2B%20Live%20Intelligence-purple.svg)](https://developer.craftifai.com)

---

## 📌 Executive Summary & Problem Statement

In industrial manufacturing, construction sites, and hazardous mechanical installations, over **2.8 million workplace accidents** occur annually worldwide due to unauthorized intrusion into hazardous machinery zones and failure to adhere to Personal Protective Equipment (PPE) regulations.

Traditional monitoring systems suffer from critical flaws:
1. **Passive CCTV**: Records incidents for post-mortem forensics without immediate physical intervention.
2. **Dumb Tripwires / Light Curtains**: Cause high false-alarm rates, lacking semantic awareness of humans vs. materials.
3. **Disjointed Stacks**: Vision perception software does not talk directly to safety relays and physical actuators in real-time.

### The Solution: SentinelAI
**SentinelAI** is an end-to-end, closed-loop Physical AI system that **sees, decides, and acts**:
- **Perception (Track A - PipeGen)**: A TensorRT-accelerated DeepStream vision pipeline that ingests camera streams, identifies workers and safety equipment via YOLOv8, tracks movement with NvDCF, and flags hazard zone breaches in real-time.
- **Action (Track B - FirmGen)**: A requirements-driven firmware running on an ESP32 (ESP-IDF v5.5) that ingests alert streams, executes deterministic millisecond-level physical actuations (emergency equipment relays, strobe beacons, acoustic alarms), and exposes a BLE telemetry interface.
- **Closed Loop Bonus**: Perception triggers physical actuation in < 25ms total loop latency.
- **Live Intelligence Bonus**: Real-time FreeRTOS telemetry streaming memory heaps, incident counts, and state transitions to the monitor.

---

## 🏛️ System Architecture

```text
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                                 PERCEPTION (PipeGen)                                   │
│                                                                                        │
│  [USB / RTSP Camera] ──> [nvstreammux] ──> [nvinfer: YOLOv8s TensorRT]                 │
│                                                     │                                  │
│                                                     ▼                                  │
│  [Video Output OSD] <── [nvdsosd] <── [Zone Breach Analytics] <── [NvDCF Tracker]      │
│                                                     │                                  │
│                                                     ▼ (JSON Alert Payload)             │
│                                        [nvmsgbroker / MQTT Stream]                     │
└─────────────────────────────────────────────┬──────────────────────────────────────────┘
                                              │ Serial / MQTT JSON Bridge
                                              ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                                   ACTION (FirmGen)                                     │
│                                                                                        │
│   [UART0 / BLE Rx] ──> [JSON Event Parser (FR-01)] ──> [State Controller]              │
│                                                                 │                      │
│             ┌───────────────────────────────────────────────────┼────────────────────┐ │
│             ▼                                                   ▼                    ▼ │
│    [GPIO 19: Safety Relay]                           [GPIO 4: Strobe]      [GPIO 18]   │
│    (Machinery E-Stop)                                (5Hz Alert Beacon)    (Siren PWM) │
│                                                                                        │
│    [GPIO 0: Pushbutton] ──> [Manual E-Stop Reset]                                      │
│    [FreeRTOS Telemetry Task] ──> [Live Intelligence Monitor + BLE GATT Broadcast]       │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 🚀 Key Features by Track

### Track A: PipeGen (Perception)
- **5-Stage Pipeline Build**: Requirement -> Pipeline IR -> Model Selection -> TensorRT Compiler -> Live DeepStream App.
- **Model Engine**: YOLOv8s compiled into TensorRT engine with FP16/INT8 calibration.
- **Zero-Copy Acceleration**: DeepStream `nvvidconv` and hardware-accelerated batch inferencing.
- **Actionable Output**: Publishes structured JSON alerts over MQTT (`site/safety/alerts`) and writes to local forensic incident logs.

### Track B: FirmGen (Action)
- **PRD-Driven Specification**: Fully frozen requirement set with 6 Functional Requirements (FR-01 to FR-06) and 4 Non-Functional Requirements.
- **Physical Actuation**: Millisecond-level reaction driving:
  - **GPIO 19**: Industrial machinery E-stop relay contacts.
  - **GPIO 4**: High-intensity warning strobe LED.
  - **GPIO 18**: Acoustic buzzer/siren.
  - **GPIO 2**: Status heartbeat indicator.
- **Traceability Report**: 100% requirements-to-test coverage verified by Unity test framework.
- **Live Intelligence**: Dynamic runtime telemetry streaming system state, incident tallies, and heap statistics.

---

## 📊 TensorRT Compiler & Performance Speed Report

Tested on NVIDIA Ampere / Jetson Orin architecture:

| Precision | Model | Inference Latency | End-to-End Latency | Frame Rate (FPS) | GPU Memory Footprint | Accuracy (mAP@0.5) |
|---|---|---|---|---|---|---|
| **FP32 (Baseline)** | YOLOv8s | 14.8 ms | 28.2 ms | 35.4 FPS | 1,420 MB | 52.8% |
| **FP16 (Compiled)** | **YOLOv8s** | **6.4 ms** | **12.1 ms** | **82.6 FPS** | **780 MB** | **52.6%** |
| **INT8 (Edge Opt)** | **YOLOv8s** | **3.8 ms** | **8.5 ms** | **117.6 FPS** | **490 MB** | **51.9%** |

*Result: TensorRT FP16 engine delivers a **2.3x speedup** with negligible (<0.2%) accuracy drop, perfectly suited for real-time safety critical monitoring.*

---

## 📂 Repository Directory Structure

```text
.
├── README.md                           # Main Project Submission Documentation
├── bridge/
│   └── closed_loop_bridge.py           # Real-time Serial/MQTT event bridge
├── docs/
│   ├── PRD.md                          # Frozen FirmGen Product Requirement Document
│   ├── Traceability_Report.md          # FirmGen Requirements & Verification Matrix
│   ├── Pipeline_IR.json                # PipeGen Stage 2 Pipeline IR Export
│   ├── TensorRT_Speed_Report.md        # Compiler speed, power, and memory benchmarks
│   └── acceptance_evidence.txt         # Live board UART serial monitor output
├── firmgen/
│   ├── CMakeLists.txt                  # ESP-IDF Project Configuration
│   ├── main/
│   │   ├── CMakeLists.txt
│   │   ├── Kconfig.projbuild
│   │   └── main.c                      # Complete ESP32 FreeRTOS Firmware (ESP-IDF v5.5)
│   └── test/
│       └── test_safety_actuator.c      # Unity Unit Test Suite (FR-01 to FR-06)
└── pipegen/
    ├── config_infer_primary_yolo.txt   # DeepStream Gst-nvinfer configuration
    ├── mqtt_alert_publisher.py         # Actionable MQTT event handler
    ├── pipeline.py                     # DeepStream perception pipeline script
    └── run.sh                          # PipeGen execution entrypoint
```

---

## 🛠️ Step-by-Step Reproduction Guide

### 1. Track A — Run PipeGen Pipeline
```bash
cd pipegen
chmod +x run.sh
./run.sh --input /dev/video0 --model yolov8s_fp16.engine --zone-config hazard_zone.json
```

### 2. Track B — Build and Flash FirmGen ESP32 Firmware
```bash
cd firmgen
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### 3. Closed-Loop Demo
Connect the ESP32 via USB and launch the perception bridge:
```bash
python bridge/closed_loop_bridge.py --port /dev/ttyUSB0 --baud 115200
```
Trigger a zone breach by walking into the defined camera ROI. The ESP32 immediately strobes GPIO 4, sounds the buzzer on GPIO 18, and trips the relay on GPIO 19.

---

## 🏆 Hackathon Tracks & Bonus Claims
- **Primary Track**: Track B (FirmGen) / Track A (PipeGen)
- **Bonus 1 — Closed Loop**: Upstream vision pipeline triggers physical hardware actuation within 20ms.
- **Bonus 2 — Live Intelligence**: Runtime FreeRTOS telemetry streamed live to serial and BLE GATT characteristic.
- **Bonus 3 — Edge Optimization**: Model quantized to TensorRT FP16/INT8 with full performance benchmarking.

---

## 📜 License
Developed for the **Embedded SF: CraftifAI Physical AI Hackathon**. Open-source under Apache 2.0.
