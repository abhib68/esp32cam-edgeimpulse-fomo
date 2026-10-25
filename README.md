# 📷 ESP32-CAM Edge Impulse FOMO + Web Dashboard
> *Real-time object detection on the ESP32-CAM using Edge Impulse FOMO, with built-in support for non-JPEG sensors (like the GC2145) and a live interactive web dashboard.*

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform: ESP32-CAM](https://img.shields.io/badge/Platform-ESP32_CAM-blue.svg)](https://www.espressif.com/)
[![Framework: Arduino](https://img.shields.io/badge/Framework-Arduino-teal.svg)](https://www.arduino.cc/)

---

## ✨ Features

* **🤖 Edge Impulse FOMO Object Detection:** Runs lightweight machine learning locally on the microcontroller to detect custom objects.
* **📷 Non-JPEG Sensor Fallback:** Specially adapted for camera sensors that lack hardware JPEG encoding (such as the **GC2145**)—automatically falling back to RGB565 and handling software JPEG conversion.
* **🌐 Embedded Web Dashboard:** Hosts a lightweight HTTP web server directly on the ESP32 for remote monitoring without needing external cloud services.
* **⚡ PSRAM Optimized:** Leverages the ESP32's PSRAM to handle high-resolution raw frame buffers and real-time streaming smoothly.

---

## 🗂️ Web Server Endpoints

Once connected to Wi-Fi, the ESP32 serves three main endpoints:
* **`http://<ESP32-IP>/`** $\rightarrow$ Interactive live dashboard (Live video feed + bounding box overlays + detection table).
* **`http://<ESP32-IP>/json`** $\rightarrow$ Real-time detection results, coordinates, and performance metrics in JSON format.
* **`http://<ESP32-IP>/capture`** $\rightarrow$ Raw JPEG snapshot of the latest camera frame.

---

## 🛠️ Hardware & Prerequisites

* **Microcontroller:** AI-Thinker ESP32-CAM (with onboard PSRAM)
* **Camera Sensor:** Compatible with OV2640, GC2145, etc.
* **IDE:** Arduino IDE configured with:
  * **Board:** `AI Thinker ESP32-CAM`
  * **CPU Frequency:** `240MHz`
  * **PSRAM:** `Enabled`
  * **Partition Scheme:** `Huge APP`

---

## 🚀 Getting Started

1. **Clone the Repository:**
   ```bash
   git clone [https://github.com/YOUR_USERNAME/YOUR_REPO_NAME.git](https://github.com/YOUR_USERNAME/YOUR_REPO_NAME.git)
