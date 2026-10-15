# PipeGen TensorRT Engine Speed & Optimization Report

**Target Platform**: NVIDIA JetPack 6.0 / RTX 40-Series  
**Framework**: NVIDIA TensorRT 10.2 / DeepStream 7.0  
**Model Architecture**: YOLOv8s Detection Network (Input Resolution: 640x640x3)  
**Compiler**: PipeGen Stage 4 Auto-Quantizer & Compiler  

---

## 🏎️ Comparative Precision Benchmarks

| Metric | FP32 Engine | FP16 Engine (Selected) | INT8 Engine (Calibrated) |
|---|---|---|---|
| **Inference Latency (Mean)** | 14.82 ms | **6.38 ms** | **3.84 ms** |
| **P99 Inference Latency** | 17.10 ms | **7.42 ms** | **4.91 ms** |
| **End-to-End Pipeline Latency** | 28.25 ms | **12.10 ms** | **8.50 ms** |
| **Throughput (Frames Per Second)** | 35.4 FPS | **82.6 FPS** | **117.6 FPS** |
| **GPU Memory Consumption** | 1,420 MB | **780 MB** | **490 MB** |
| **Estimated Power Draw (Jetson)** | 19.8 W | **14.2 W** | **10.5 W** |
| **mAP @ IoU 0.5:0.95** | 37.4% | **37.3% (-0.1%)** | **36.8% (-0.6%)** |
| **mAP @ IoU 0.5** | 52.8% | **52.6% (-0.2%)** | **51.9% (-0.9%)** |

---

## 📈 Optimization Breakdown

```text
Latency Comparison (lower is better):
FP32  | ██████████████████████████████ 14.82 ms
FP16  | █████████████ 6.38 ms  (2.32x Speedup)
INT8  | ████████ 3.84 ms        (3.86x Speedup)

GPU VRAM Allocation (lower is better):
FP32  | ████████████████████████████ 1420 MB
FP16  | ███████████████ 780 MB (45% reduction)
INT8  | ██████████ 490 MB      (65% reduction)
```

---

## 🔬 Profiling Methodology & Insights
1. **Layer Fusion**: TensorRT fused Conv + BatchNorm + SiLU activation into single kernels, reducing memory bandwidth pressure by 41%.
2. **Precision Recommendation**: **FP16** is chosen as the sweet spot for primary deployment because it offers a **2.32x throughput improvement** and reduces GPU memory footprint by nearly half with virtually zero loss in detection precision (0.2% delta).
3. **Edge Optimization Bonus**: The INT8 engine demonstrates deployment feasibility on edge Jetson devices under a strict 15W power envelope.
