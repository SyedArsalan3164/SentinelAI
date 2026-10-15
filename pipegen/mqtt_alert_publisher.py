#!/usr/bin/env python3
"""
SentinelAI Alert Publisher
Dispatches structured JSON telemetry over MQTT and local IPC file
"""

import json
import os
import sys

INCIDENT_LOG_PATH = "incident_log.json"

def publish_safety_alert(payload):
    json_str = json.dumps(payload)
    print(f"\n[ALERT EMITTED] >>> {json_str}")

    # Append to local forensic ledger
    try:
        with open(INCIDENT_LOG_PATH, "a") as f:
            f.write(json_str + "\n")
    except Exception as e:
        print(f"Error logging incident: {e}")

    # If MQTT client is available, publish to broker
    try:
        import paho.mqtt.client as mqtt
        client = mqtt.Client()
        client.connect("localhost", 1883, 60)
        client.publish("site/safety/alerts", json_str)
        client.disconnect()
    except Exception:
        # Graceful fallback when running standalone without local broker
        pass
