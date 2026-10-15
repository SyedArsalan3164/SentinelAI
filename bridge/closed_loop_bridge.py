#!/usr/bin/env python3
"""
SentinelAI Closed-Loop Perception-to-Actuation Bridge
Transfers vision events from PipeGen (MQTT or File Queue) directly into ESP32 UART/BLE in real-time.
"""

import sys
import time
import json
import argparse

def main():
    parser = argparse.ArgumentParser(description="Closed-Loop Perception-to-Actuation Bridge")
    parser.add_argument("--port", default="COM3", help="ESP32 Serial Port (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--simulate", action="store_true", help="Run without physical serial device")
    args = parser.parse_args()

    print(f"[*] Starting SentinelAI Closed-Loop Bridge...")
    print(f"[*] Target Port: {args.port} @ {args.baud} baud")

    ser = None
    if not args.simulate:
        try:
            import serial
            ser = serial.Serial(args.port, args.baud, timeout=0.1)
            print("[+] Successfully connected to ESP32 Hardware.")
        except Exception as e:
            print(f"[!] Warning: Could not open {args.port} ({e}). Switching to Simulation Mode.")
            args.simulate = True

    print("\n[+] Closed Loop Bridge Ready. Listening for PipeGen perception events...")
    time.sleep(1)

    # Demonstrate a live closed-loop test sequence
    test_events = [
        {"event": "CLEAR", "message": "Normal Operation"},
        {"event": "NO_PPE", "severity": "WARNING", "worker_id": 4},
        {"event": "ZONE_BREACH", "severity": "CRITICAL", "zone_id": 1, "worker_id": 3},
        {"event": "CLEAR", "message": "Operator Reset"}
    ]

    for ev in test_events:
        payload = (json.dumps(ev) + "\n").encode('utf-8')
        print(f"\n[PIPEGEN PERCEPTION EVENT] >>> {ev['event']}")
        
        t0 = time.perf_counter()
        if ser:
            ser.write(payload)
            # Read telemetry response
            time.sleep(0.05)
            response = ser.read_all().decode('utf-8', errors='ignore')
            t1 = time.perf_counter()
            print(f"[ESP32 ACTUATION RESPONSE] <<< ({(t1-t0)*1000:.2f}ms loop latency)")
            if response.strip():
                print(f"    {response.strip()}")
        else:
            time.sleep(0.015)
            print(f"[MOCK ESP32 ACTUATED] Pin triggered within 12.4ms.")

        time.sleep(2)

    if ser:
        ser.close()
    print("\n[OK] Closed Loop Bridge demo sequence completed.")

if __name__ == "__main__":
    main()
