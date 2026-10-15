# FirmGen Product Requirement Document (PRD)

## Project Name: SPAN-ESP32 (Safety Physical Actuator Node)
**Target Hardware**: ESP32-WROOM-32  
**Framework**: ESP-IDF v5.5  
**Execution Path**: FirmGen Category-wise Manual Review & Freeze  
**Status**: REVIEWED & FROZEN  

---

### 1. Vision & Purpose
SPAN-ESP32 is an industrial edge safety node responsible for deterministic, physical actuation when hazard warnings or intrusion alerts are signaled by upstream perception nodes (PipeGen). It eliminates latency and reliability risks inherent in purely cloud-driven safety systems by providing local, hardware-level machinery cut-offs and strobe alerts.

---

### 2. Physical Pin Allocation

| Function | Pin | Direction | Active Level | Default State |
|---|---|---|---|---|
| Status Heartbeat LED | GPIO 2 | Output | High | Low |
| Visual Alert Strobe | GPIO 4 | Output | High | Low (Off) |
| Acoustic Siren / Buzzer | GPIO 18 | Output | High | Low (Off) |
| Safety Relay / E-Stop | GPIO 19 | Output | Low (Active Low) | High (Normal Operation) |
| Manual Reset Button | GPIO 0 | Input | Low (Pulled High) | High |
| Host UART Interface | GPIO 1 (Tx) / GPIO 3 (Rx) | In/Out | 3.3V TTL | 115200 8-N-1 |

---

### 3. Requirements Specification

#### 3.1 Functional Requirements (FR)

##### [FR-01] Structured JSON Ingestion
- **Category**: Communication / Parser
- **Tag**: Explicit
- **Description**: The firmware MUST parse newline-terminated (`\n`) JSON strings from UART0 containing `event`, `severity`, and optional `zone_id` or `worker_id` fields.
- **Payload Schema**:
  ```json
  {"event": "ZONE_BREACH", "severity": "CRITICAL", "zone_id": 1, "timestamp": 1710000000}
  ```
- **Acceptance Criterion**: Invalid JSON or unrecognized events MUST be dropped with an error log without crashing or hanging the parser.

##### [FR-02] Critical Hazard Actuation
- **Category**: Actuation / Safety
- **Tag**: Explicit
- **Description**: Upon receiving `event == "ZONE_BREACH"`:
  1. GPIO 4 (Strobe) toggles at 5Hz (100ms ON / 100ms OFF).
  2. GPIO 18 (Buzzer) sounds with a pulsed tone.
  3. GPIO 19 (Safety Relay) drops to LOW (tripping machinery circuit).
- **Acceptance Criterion**: All three pins toggle to alarm state within 20ms of packet arrival.

##### [FR-03] Warning Status Indication
- **Category**: Actuation / Warning
- **Tag**: Explicit
- **Description**: Upon receiving `event == "NO_PPE"`:
  1. GPIO 2 (Status LED) blinks at 1Hz (500ms ON / 500ms OFF).
  2. GPIO 19 remains HIGH (machinery remains running).
- **Acceptance Criterion**: System transitions to `STATE_WARNING_PPE` and emits warning log.

##### [FR-04] Manual Operator Reset
- **Category**: Control / Interlock
- **Tag**: Explicit
- **Description**: Pressing the physical push button on GPIO 0 for at least 50ms (debounced) MUST reset the alarm state back to `STATE_IDLE`.
- **Acceptance Criterion**: GPIO 4 & 18 turn OFF; GPIO 19 returns to HIGH (Safe Operation).

##### [FR-05] BLE GATT Telemetry Advertising
- **Category**: Wireless / Connectivity
- **Tag**: Inferred
- **Description**: The ESP32 MUST host a BLE GATT Server advertising custom service UUID `4fafc201-1fb5-459e-8fcc-c5c9c331914b` exposing read/notify characteristics for current state and incident tally.
- **Acceptance Criterion**: External BLE central can subscribe and receive notification upon state transitions.

##### [FR-06] Heartbeat & System Health
- **Category**: Diagnostics / Telemetry
- **Tag**: Explicit
- **Description**: System MUST emit periodic telemetry over UART every 2000ms in JSON format:
  ```json
  {"telemetry": {"state": 0, "incidents": 2, "heap": 224180}}
  ```
- **Acceptance Criterion**: Host monitoring software receives regular heartbeat packets.

---

#### 3.2 Non-Functional Requirements (NFR)

##### [NFR-01] Latency
Actuation latency from receipt of terminating `\n` to GPIO assertion must be `< 20ms`.

##### [NFR-02] Stability & Watchdog
System must register FreeRTOS Task Watchdog Timer (TWDT) with 3000ms timeout on all worker tasks.

##### [NFR-03] Memory Footprint
Peak dynamic SRAM consumption must remain under 128KB.

##### [NFR-04] Fail-Safe Interlock
If UART link disconnects or drops heartbeat for > 15s in active monitoring mode, status LED indicates communication warning.
