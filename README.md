# 🛰️ Tactical Multi-GNSS Terminal & Embedded Kalman Engine
### Extreme-Condition Navigation, Tactical Data Link & Handheld Terminal

> ⚠️ **Project Status:** *Active Research & Development (WIP / Work in Progress)*  
> Open-source tactical navigation and situational awareness research combining an ANSI C99 bare-metal zero-heap firmware core, multi-band GNSS engine, bidirectional tactical data link (1W LoRa + Iridium SBD failover), two-stage RF power electronics, and a ruggedized handheld tactical terminal inspired by Garmin GPSMAP 67i architecture.

---

## 📌 Project Overview

This system is engineered to provide continuous, high-precision positioning and resilient telemetry under extreme environmental conditions ($-30^\circ\text{C}$ alpine sub-zero temperatures, deep gorge/canyon multipath obscuration, and complete cellular denial).

The system is **not** a passive consumer GPS receiver (RX-only); it is an **active bidirectional tactical transceiver node (TX/RX Active Node)** capable of decentralized peer-to-peer (P2P) and mesh tactical communication with adjacent squad terminals without cellular infrastructure.

### Core Architectural Layers:
1. **Embedded C99 Firmware & Silicon Core (`src/`, `include/`):**
   * **Target Silicon:** Primary backpack node powered by **STM32U585 / STM32L4R5 (ARM Cortex-M33 / M4 @ 160 MHz)**; chest HMI pod driven by **STM32G0B1 / ESP32-S3**.
   * **Zero-Dynamic Memory Allocation:** Deterministic execution and static memory safety with zero runtime heap allocation (`CONFIG_HEAP_MEM_POOL_SIZE = 0`).
   * **Topographic NLOS Horizon Filter:** 64-sector polar DEM horizon ray-tracer rejecting non-line-of-sight (NLOS) canyon reflections before extended Kalman filtering (EKF).
   * **Lossless CGPX Compression & Crypto:** DPCM + LEB128 varint coordinate delta compression (4–6 bytes/point) with hardware-accelerated AES-128-CTR encryption.
   * **Cryo-Sentinel Power Supervision:** Sub-zero electrochemical lithium plating lockout below $0^\circ\text{C}$, hysteretic PTC pre-heater control, and supercapacitor pulse buffering.

2. **Tactical Data Link & Blue Force Tracking (BFT):**
   * **Tier-1 Local Tactical RF (1W LoRa):** EBYTE E22-900T30D (Semtech SX1262, 868/915 MHz) delivering +30 dBm (1000 mW) for 12–15 km line-of-sight (LOS) and **40–60 km** tactical ridge-relay coverage.
   * **Tier-2 BLOS Satellite Failover:** RockBLOCK 9603 (Iridium 9603 SBD, 1621 MHz) providing global pole-to-pole reach with zero-leakage (0.0 µA) P-MOSFET power-gating.

3. **Power Electronics & RF Noise Suppression:**
   * **Two-Stage Hybrid Power Architecture:** Synchronous Buck-Boost (TPS63020, 96% efficiency @ 2.4 MHz) + Ferrite Bead $\pi$-Filter + Ultra-High PSRR RF LDO (TI TPS7A20).
   * **71 dB Power Isolation:** Reduces switcher ripple from $24.5\text{ mV}_{p-p}$ down to $6.91\ \mu\text{V}_{p-p}$, completely eliminating GNSS LNA receiver desensitization ($\Delta C/N_0 = 0.0003\text{ dB}$).

4. **Hardware Bridge & Tactical Terminal Interface (`web/`, `scripts/`):**
   * **10th Gen GNSS Circular DMA Bridge:** High-throughput parser for u-blox M10Q and Quectel LC29H streaming NMEA 0183 (`$GNGGA`, `$GNRMC`, `$GNGSV`).
   * **Garmin GPSMAP 67i Handheld Terminal:** Industrial chording keypad, dual MGRS/WGS84 coordinate engine, and 4-layer offline raster/vector basemap.
   * **17 Offline Tactical Engines:** Sight 'N Go, Route Profiler, Shoelace Polygon Area Surveyor, Geofence Proximity Sentry, Sun/Moon Ephemeris, and Morse SOS Flasher.

---

## 🏛️ System & Data Link Architecture

```
+====================================================================================================+
|                               TACTICAL SPLIT-NODE SYSTEM ARCHITECTURE                              |
+====================================================================================================+

 [ CHEST / WRIST HMI POD ]                                      [ BACKPACK MASTER COMPUTE & RF POD ]
 +---------------------------------------+                     +---------------------------------------+
 | MCU: STM32G0B1RE / Seeed XIAO ESP32-S3|                     | MCU: STM32U585CI / STM32L4R5ZI        |
 |  - ARM Cortex-M0+ / Dual Xtensa LX7   |                     |  - ARM Cortex-M33 / M4 @ 160 MHz      |
 |  - Ultra-Low Power (14 uA Standby)    |                     |  - FPU + DSP + Hardware SAES Engine   |
 |                                       |                     |  - Zero-Heap Deterministic C99 Core   |
 | DISPLAY: Sharp 2.7" MIP (LS027B7DH01A)|   4-Core PUR        |                                       |
 |  - 400x240 Monochrome Transflective   |   Spiral Cable      | GNSS: Quectel LC29H / u-blox M10Q     |
 |  - 50 uW Power, Zero Liquid Freezing  |                     |  - Dual-Band L1/L5 Multi-Constellation|
 |  - Dirty-Line SPI DMA Driver          |   ISO 11898-2       |  - 64-Sector DEM Polar Skymask        |
 |                                       |     CAN-FD          |                                       |
 | INPUT: 4x Omron B3F-4055 (650 gf)     |<===================>| TIER-1 RF: EBYTE E22-900T30D (LoRa)   |
 |  - Glove-Compatible Chording FSM      |   [500k / 2 Mbps]   |  - SX1262 1W (+30 dBm) @ 868 MHz      |
 |  - Pan, Zoom, Mode & Distress SOS     |   TI TCAN337G       |  - AES-128-CTR Encrypted BFT (TX/RX)  |
 |                                       |   +-70V Fault Tol.  |                                       |
 | SENSOR: Bosch Sensortec BMP581        |                     | TIER-2 SAT: RockBLOCK 9603 (Iridium)  |
 |  - +-0.06 hPa Barometric Altimeter    |                     |  - 1621 MHz SBD Global BLOS Satellite |
 |  - Vertical Terrain TRN Filter        |                     |  - Si2301 P-MOSFET Gate (0.0 uA Standby)
 +---------------------------------------+                     |                                       |
                                                               | POWER & THERMAL:                      |
                                                               |  - Molicel 21700 + PTC Heater Jacket  |
                                                               |  - 5.0F Low-ESR Supercapacitor Bank   |
                                                               |  - TI TPL5010 Nano-Power Watchdog     |
                                                               +---------------------------------------+
                                                                                  | |
                                                   +------------------------------+ +--------------------+
                                                   |                                                     |
                                                   v [1W LoRa TX/RX]                                     v [Iridium SBD]
                                      +-------------------------+                           +-------------------------+
                                      | BLUE FORCE TRACKING     |                           | GLOBAL SATELLITE NETWORK|
                                      | Squad Nodes (P2P/Mesh)  |                           | BLOS Distress SOS & Tele|
                                      +-------------------------+                           +-------------------------+
```

---

## 📡 Tactical Data Link (TX/RX) & Blue Force Tracking (BFT)

The terminal acts as an active communication node within tactical networks via a **dual-tier hybrid data link**:

### 1. Tier-1 Local Tactical RF Link (1W LoRa):
* **Transceiver:** EBYTE E22-900T30D (Semtech SX1262 silicon, 868 MHz NATO/EU ISM band).
* **RF Link Budget:** $+30\text{ dBm}$ (1000 mW) output power, $-148\text{ dBm}$ receiver sensitivity, achieving 12–15 km line-of-sight (LOS) and **40–60 km** range with tactical ridge-line relays.
* **Blue Force Tracking (BFT) Protocol:** Broadcasts compressed `.cgpx` coordinate deltas, tactical callsign (`node_id`), altitude, battery charge, and operational flags.
* **Cryptographic Security:** End-to-end encrypted with **AES-128-CTR**. Includes rolling 16-bit sequence counters (`seq_num`) and dynamic nonces for replay attack immunity. Frames are verified with CRC-16-CCITT.
* **Map Overlay:** Received peer telemetry is rendered directly on the terminal display with blue tactical markers and altitude tags.

### 2. Tier-2 Beyond-Line-of-Sight (BLOS) Satellite Failover:
* **Satellite Transceiver:** RockBLOCK 9603 (Iridium 9603 Short Burst Data - SBD, 1621 MHz).
* **Automated Failover Engine ([`src/hybrid_comms.c`](file:///c:/Users/ayibogan996/Desktop/projeler/gps/src/hybrid_comms.c)):** If a squad unit traverses behind a mountain ridge and fails to receive an acknowledgment (ACK) across 3 consecutive LoRa transmissions, the state machine automatically routes telemetry to the Iridium satellite link.
* **Emergency Distress (SOS) Bypass:** Triggering the emergency distress sequence bypasses the LoRa queue, instantly powering the satellite module to transmit coordinates to command headquarters.
* **Zero-Leakage Power-Gating:** An external Si2301 P-MOSFET cuts all power to the Iridium module during standby (**0.0 µA quiescent draw**). An integrated 5.0F supercapacitor supplies the 2A transmit burst without battery voltage collapse ($V_{sag}$).

---

## ⚡ Microcontroller Unit (MCU) Silicon Specifications

| Subsystem Node | Processor Silicon | Core Architecture | Clock & Memory | Primary Firmware Responsibilities |
| :--- | :--- | :--- | :--- | :--- |
| **Backpack Master Node (Core)** | **STM32U585CI** *(Alt: STM32L4R5ZI / ESP32-S3)* | **ARM Cortex-M33** (ARMv8-M, TrustZone) | 160 MHz, 2MB Flash, 786KB SRAM, FPU, DSP | Zero-heap C99 core (`CONFIG_HEAP_MEM_POOL_SIZE = 0`), 64-sector DEM polar skymask ray-tracer, EKF filters, DPCM compression, supercapacitor power supervisor, 1W LoRa / Iridium comms. |
| **Chest/Wrist HMI Pod** | **STM32G0B1RE** *(Alt: Seeed XIAO ESP32-S3)* | **ARM Cortex-M0+** | 64 MHz, 512KB Flash, 144KB SRAM, 14 µA Standby | Sharp 2.7" MIP display Dirty-Line DMA engine (99.55% SPI bandwidth reduction), Omron 4-button temporal chording FSM, BMP581 barometric altimeter sampling. |
| **Differential Interconnect** | **TI TCAN337G** | ISO 11898-2 CAN-FD Transceiver | 5 Mbps (500 kbps arbit. / 2 Mbps data) | Deterministic serial frame transport across 1m PUR cable with SOF `0xAA55`, EOF `0x55AA`, and CRC-16-CCITT protection ($\pm 70\text{ V}$ bus fault tolerance). |

---

## ⚡ Power Electronics: Hybrid Two-Stage RF Power Architecture

In mission-critical tactical hardware, switching DC-DC converters generate $20 - 30\text{ mV}_{p-p}$ high-frequency ripple ($f_{sw} \approx 2.4\text{ MHz}$). If routed directly to $-167\text{ dBm}$ GNSS low-noise amplifiers (LNA) and LoRa VCOs, this switching noise severely degrades satellite carrier-to-noise ratio ($\Delta C/N_0 \approx 5.5\text{ dB}$ desense drop).

To eliminate receiver desensitization, this platform implements a two-stage hybrid power isolation architecture:

```
 [ 21700 Li-Ion / 5.0F SUPERCAPACITOR ] (3.0V - 4.2V)
                     |
                     v
 [ STAGE 1: SYNCHRONOUS BUCK-BOOST ] (TI TPS63020 / 96% Efficiency @ 2.4 MHz)
                     |
                     +----------------------------> VDD_DIG (3.3V Digital Rail: MCU, MIP, CAN-FD)
                     |                              (Ripple: ~24.5 mVp-p)
                     v
 [ Pi-FILTER (LC / Ferrite) ] (Murata BLM18HE152SN1D: 1500R @ 100MHz + 2x 10 uF MLCC)
                     | (18.2 dB High-Frequency Attenuation)
                     v
 [ STAGE 2: ULTRA-HIGH PSRR LDO ] (TI TPS7A2030PDBVR: 95dB PSRR @ 1kHz, 52.8dB @ 2.4MHz)
                     |
                     v
               VDD_RF (3.0V Clean Analog / RF Rail)
               - Residual Ripple: < 7.0 uVp-p (71 dB Total Isolation)
               - GNSS LNA Desense Loss: Delta C/N0 = 0.0003 dB (Zero Desense)
               - Powers Quectel LC29H / u-blox M10Q LNA & LoRa TCXO/PLL
```

* **Mathematical Isolation Equation:**
  $$V_{ripple\_out} = V_{ripple\_in} \times 10^{-\frac{\text{PSRR}(f_{sw}) + \text{Atten}_{\pi}}{20}} = 24.5\text{ mV}_{p-p} \times 10^{-\frac{71.0\text{ dB}}{20}} = \mathbf{6.91\ \mu\text{V}_{p-p}}$$
* **Result:** Digital components maintain $96\%$ power conversion efficiency, while the RF front-end achieves $71\text{ dB}$ supply noise rejection, preserving weak GNSS carrier tracking in steep alpine terrain.

---

## 📂 Repository Architecture

```
gnss-kalman/
├── src/                  # ANSI C99 Bare-Metal Firmware Modules (Zero-Heap)
│   ├── hybrid_comms.c    # 1W LoRa & Iridium SBD Hybrid Telemetry, BFT & Failover
│   ├── cgpx_engine.c     # DPCM + LEB128 Varint Lossless Route Compression
│   ├── mip_display.c     # Sharp MIP Display Driver (400x240 1-bit Monochrome)
│   ├── chord_fsm.c       # 4-Button Temporal Chording Input State Machine
│   ├── split_node_bus.c  # Split-Node CAN-FD Differential Communication Protocol
│   ├── gnss_nmea.c       # Circular DMA Buffer NMEA 0183 Tokenizer & Parser
│   ├── crypto_hal.c      # STM32 Hardware SAES / Software AES-128-CTR Crypto HAL
│   ├── nlos_filter.c     # 64-Sector Polar Topographic DEM Skymask Horizon Filter
│   └── power_supervisor.c# Sub-Zero Lithium Guard, Hysteresis & Thermal Throttle
│
├── include/              # C99 Header Files & Protocol Definitions (*.h)
│   ├── hybrid_comms.h    # Tactical Data Link, BFT Beacon & Satellite API
│   ├── split_node_bus.h  # CAN-FD Frame Format & BFT Message Types
│   ├── crypto_hal.h      # AES-128-CTR Cipher Engine Interface
│   └── nlos_filter.h     # Skymask LUT & Knife-Edge Diffraction Declarations
│
├── web/                  # Handheld Tactical Terminals & Hardware Bridges
│   ├── tactical_terminal.html # Garmin GPSMAP 67i Tactical Handheld UI
│   ├── gnss_tracker.html      # Live Multi-Constellation GNSS Map & Telemetry
│   ├── open_weather_lab.py    # Python Hardware Serial Bridge & Daemon
│   ├── tactical_survival_meteorology.html # MET-SURV OPS: Tactical Meteorology Engine
│   └── cgpx_studio.html       # CGPX Crypto & 400x240 MIP Studio
│
├── firmware/             # Hardware Validation Sketches (.ino)
│   ├── gps_passthrough/  # u-blox M10Q High-Speed Serial Monitor
│   ├── tft_screen_test/  # SPI Screen Hardware Test Sketch
│   └── tft_st7789_test/  # ST7789 Display Validation Sketch
│
├── scripts/              # Portable Automation & Launch Scripts
│   ├── start_terminal.bat     # Launches Hardware Bridge & Tactical Terminal
│   └── start_weather_lab.bat  # Launches Tactical Meteorology Lab
│
├── tests/                # Verification & Validation (V&V) Test Suites
│   ├── test_phases_all.py     # Master System Verification Harness (Phases 1 - 7)
│   ├── test_harness_vnv.py    # Mission-Critical Environmental & RF Test Suite (10/10)
│   ├── test_hybrid_comms.py   # 1W LoRa BFT & Iridium Failover Unit Test
│   ├── test_rf_power_psrr.py  # Hybrid RF Power & LDO PSRR Isolation Test
│   ├── test_cgpx.py           # CGPX Lossless Compression & C99 Engine Tests
│   ├── test_topo.py           # Topographic DEM, Hillshade & Contour Tests
│   ├── test_map_stability.py  # Map Engine Stability & Loop Test
│   └── test_harness_*.c       # Native MinGW GCC Bare-Metal Test Runners
│
├── tools/                # Python DEM, 3D Hillshade & Geospatial CLI Tools
├── data/                 # Sample Alpine Routes (GPX/CGPX) & 30m DEM GeoTIFF
├── docs/                 # Engineering Specifications, V&V Reports & BOM Sheets
├── assets/               # Output Graphics, Topographic Matrices & Schematics
├── .gitignore            # Build binary and cache filters
├── LICENSE               # MIT License
└── README.md             # Project Master Documentation
```

---

## 🚀 Quick Start

### 1. Prerequisites
* Python 3.10+
* Required libraries: `pyserial`, `numpy`, `pillow`, `cryptography`
* Optional: MinGW GCC (for C99 bare-metal test suite execution)

```bash
pip install pyserial numpy pillow cryptography
```

### 2. Launching Live Tactical Terminal
Connect your u-blox M10Q or Quectel GNSS module via USB and start the bridge:

```bash
python web/open_weather_lab.py
# or on Windows: scripts\start_terminal.bat
```

* **Tactical Terminal:** `http://127.0.0.1:8080/tactical_terminal.html`
* **Live Telemetry Tracker:** `http://127.0.0.1:8080/gnss_tracker.html`

### 3. Running Verification & Validation (V&V) Harness
Run the full 7-phase automated firmware and algorithm test suite:

```bash
python tests/test_phases_all.py
```

* **Phase 1:** CGPX Lossless DPCM Compression & C99 Rasterizer (PASS)
* **Phase 2:** Sharp 2.7" MIP Display & Chording FSM Input Engine (PASS)
* **Phase 3:** Split-Node ISO 11898-2 CAN-FD Bus & 100k-Byte Fault Injection (PASS)
* **Phase 4:** Circular DMA Tokenizer, Polar Skymask & AES-128-CTR Crypto (PASS)
* **Phase 5:** Sub-Zero Power Supervisor, Thermal Hysteresis & TPL5010 Watchdog (PASS)
* **Phase 6:** Topographic Terrain Engine, 315° Hillshade & Marching Squares (PASS)
* **Phase 7:** Tactical Data Link, 1W LoRa BFT Mesh & Satellite Failover (PASS)

For RF power integrity and PSRR bench simulation:
```bash
python tests/test_rf_power_psrr.py
python tests/test_harness_vnv.py
```

---

## 🗺️ Offline Tactical Map Engines
* **OSM TopoActive:** Contours, trails, and elevation isolines (Open CDN).
* **BirdsEye Satellite:** High-resolution hybrid satellite imagery (zero API keys required).
* **NVG Dark Mode:** High-contrast tactical night theme designed for NVG optics.
* **Vector Basemap:** Zero-data offline baseline grid with instantaneous MGRS reprojection.

---

## 📄 License
This project is open-source research released under the [MIT License](LICENSE).
