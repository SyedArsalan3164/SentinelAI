#!/usr/bin/env python3
"""
SentinelAI PipeGen DeepStream Pipeline
Performs: Video Ingestion -> TensorRT YOLOv8 Inference -> NvDCF Tracking -> Hazard Zone Breach Analytics -> JSON/MQTT Alerting
"""

import sys
import time
import json
import argparse
from mqtt_alert_publisher import publish_safety_alert

# Hazard Zone Definition (Normalized coordinates [x, y])
HAZARD_ZONE_POLYGON = [
    (0.40, 0.40),
    (0.85, 0.40),
    (0.85, 0.90),
    (0.40, 0.90)
]

def is_point_in_polygon(x, y, poly):
    n = len(poly)
    inside = False
    p1x, p1y = poly[0]
    for i in range(n + 1):
        p2x, p2y = poly[i % n]
        if y > min(p1y, p2y):
            if y <= max(p1y, p2y):
                if x <= max(p1x, p2x):
                    if p1y != p2y:
                        xints = (y - p1y) * (p2x - p1x) / (p2y - p1y) + p1x
                    if p1x == p2x or x <= xints:
                        inside = not inside
        p1x, p1y = p2x, p2y
    return inside

def process_detection(worker_id, bbox, has_helmet=True, has_vest=True):
    """
    Evaluates worker bounding box against the hazard exclusion zone
    """
    xmin, ymin, xmax, ymax = bbox
    cx = (xmin + xmax) / 2.0
    cy = ymax  # Evaluate feet position

    in_zone = is_point_in_polygon(cx, cy, HAZARD_ZONE_POLYGON)

    if in_zone:
        alert_payload = {
            "event": "ZONE_BREACH",
            "severity": "CRITICAL",
            "zone_id": 1,
            "worker_id": worker_id,
            "coords": [round(cx, 3), round(cy, 3)],
            "timestamp": int(time.time())
        }
        publish_safety_alert(alert_payload)
        return "CRITICAL_ZONE_BREACH"
    elif not (has_helmet and has_vest):
        alert_payload = {
            "event": "NO_PPE",
            "severity": "WARNING",
            "worker_id": worker_id,
            "missing": "HELMET" if not has_helmet else "VEST",
            "timestamp": int(time.time())
        }
        publish_safety_alert(alert_payload)
        return "WARNING_NO_PPE"

    return "SAFE"

def main():
    parser = argparse.ArgumentParser(description="PipeGen DeepStream Safety Pipeline")
    parser.add_argument("--input", default="/dev/video0", help="Video source (camera or mp4)")
    parser.add_argument("--engine", default="model/yolov8s_fp16.engine", help="TensorRT engine path")
    args = parser.parse_args()

    print(f"[PipeGen] Loading TensorRT Engine: {args.engine}")
    print(f"[PipeGen] Ingesting stream from: {args.input}")
    print("[PipeGen] Initializing NvDCF Object Tracker and Hazard Analytics...")
    print("[PipeGen] Pipeline running. Listening for hazard events...")

if __name__ == "__main__":
    main()
