# 🛰️ Tactical Multi-GNSS Terminal & Embedded Kalman Engine
### Extreme-Condition Navigation, Tactical Data Link & Handheld Terminal

> ⚠️ **Project Status:** *Active Research & Development (WIP / Work in Progress)*  
> Open-source tactical navigation and situational awareness research combining an ANSI C99 bare-metal zero-heap firmware core, multi-band GNSS engine, bidirectional tactical data link (1W LoRa + Iridium SBD failover), and a ruggedized handheld tactical terminal inspired by Garmin GPSMAP 67i architecture.

---

## 📌 Project Overview (Proje Genel Bakışı)

Bu sistem, ekstrem çevre koşullarında ($-30^\circ\text{C}$ dondurucu soğuk, derin vadi/kanyon gölgelenmesi ve sıfır hücresel şebeke ortamı) kesintisiz, yüksek hassasiyetli seyrüsefer ve taktik haberleşme sağlamak üzere kurgulanmıştır.

Sistem klasik pasif bir GPS alıcısı (RX-only) değil; **sahadaki diğer tim terminalleriyle hücresel şebekeden bağımsız konuşan aktif bir taktik alıcı-verici düğümüdür (TX/RX Active Node).**

### 1. Ana Mimari Katmanları:
1. **Merkezi İşlem Birimi & C99 Firmware Çekirdeği (`src/`, `include/`):**
   * **Donanım Kalbi (Silicon):** Ana düğümde **STM32U585 / STM32L4R5 (ARM Cortex-M33 / M4 @ 120–160 MHz)** ile yardımcı HMI podunda **STM32G0B1 / ESP32-S3** ayrık işlemci mimarisi.
   * **Zero-Dynamic Memory Allocation:** Deterministik yürütme ve mutlak bellek güvenliği için sıfır-heap mimarisi (`CONFIG_HEAP_MEM_POOL_SIZE = 0`).
   * **Topographic NLOS Filter:** 3D DEM sayısal yükseklik modelinden türetilen 64-sektör polar ufuk maskesi ile kanyon yansımalarını (multipath) ayıklayan ışın izleme (ray-tracing) algoritması.
   * **Lossless CGPX Compression & Crypto:** DPCM + LEB128 varint kodlama ile nokta başına 4–6 bayt sıkıştırma ve donanımsal AES-128-CTR kriptografik koruma.
   * **Cryo-Sentinel Power Supervision:** Dondurucu soğukta lityum kaplanma koruması, histerezisli PTC ön-ısıtıcı denetimi ve süperkapasitör yük tamponlama.

2. **Taktik Veri Bağı & Dost Birlik Takip (BFT Data Link):**
   * **Tier-1 Yerel RF (1W / +30 dBm LoRa):** EBYTE E22-900T30D (Semtech SX1262 tabanlı, 868/915 MHz) ile görüş hattında 12–15 km, açık arazide ve rölelerle 40–60 km menzilli P2P/mesh Dost Birlik Takip (Blue Force Tracking) ağı.
   * **Tier-2 BLOS (Beyond-Line-of-Sight) Küresel Uydu:** RockBLOCK 9603 (Iridium 9603 SBD, 1621 MHz) ile kutuptan kutuba kesintisiz uydu yedekliliği ve Si2301 P-MOSFET ile 0.0 µA sızıntısız güç kapılama.

3. **Hardware Bridge & Tactical Terminal Interface (`web/`, `scripts/`):**
   * **u-blox 10th Gen GNSS Bridge:** TBS M10Q (GPS, GLONASS, Galileo, BeiDou eşzamanlı takibi) alıcısını otomatik tanıyan, NMEA 0183 (`$GNGGA`, `$GNRMC`, `$GNGSV`) akışını dairesel DMA ile gerçek zamanlı ayrıştıran donanım köprüsü.
   * **Garmin GPSMAP 67i Handheld Terminal:** Endüstriyel tuş takımı (D-Pad rocker), MGRS/WGS84 çift koordinat desteği ve 4 katmanlı harita motoru (OSM TopoActive, BirdsEye Uydu, Askeri NVG Dark, Sıfır-Şebeke Vektör Basemap).
   * **17 Çevrimdışı Taktik Motor:** Sight 'N Go (Kerterizle ve Git), Rota Planlayıcı, Poligon Alan Hesaplayıcı (Shoelace formülü), Geofence Çevre Güvenlik Alarmı, Güneş/Ay Efemeris ve SOS Morse flaşörü.

---

## 🏛️ Sistem ve Veri Bağı Mimarisi

```
+====================================================================================================+
|                               TAKTİK AYRIK GÖVDE SİSTEM MİMARİSİ                                   |
+====================================================================================================+

 [ GÖĞÜS / BİLEK HMI PODU ]                                     [ SIRT ÇANTASI ANA İŞLEMCİ & RF PODU ]
 +---------------------------------------+                     +---------------------------------------+
 | MCU: STM32G0B1RE / Seeed XIAO ESP32-S3|                     | MCU: STM32U585CI / STM32L4R5ZI        |
 |  - ARM Cortex-M0+ / Dual Xtensa LX7   |                     |  - ARM Cortex-M33 / M4 @ 160 MHz      |
 |  - Ultra Düşük Güç (14 uA Standby)    |                     |  - FPU + DSP + Donanımsal AES Hızl.   |
 |                                       |                     |  - Zero-Heap Bare-Metal C99 Motoru    |
 | EKRAN: Sharp 2.7" MIP (LS027B7DH01A)  |   4-Damarlı PUR     |                                       |
 |  - 400x240 Monokrom Transflektif      |   Spiral Kablo      | GNSS: Quectel LC29H / TBS M10Q        |
 |  - 50 uW Güç, Sub-Zero Donmaz Sıvısız |                     |  - Çift Bant L1/L5 Çoklu Takımyıldız  |
 |  - Dirty-Line SPI DMA Sürücüsü        |   ISO 11898-2       |  - 64-Sektör DEM Polar Skymask        |
 |                                       |     CAN-FD          |                                       |
 | GİRİŞ: 4x Omron B3F-4055 (650 gf)     |<===================>| TIER-1 RF: EBYTE E22-900T30D (LoRa)   |
 |  - Eldiven Uyumlu Chording FSM        |   [500k / 2 Mbps]   |  - SX1262 1W (+30 dBm) @ 868 MHz      |
 |  - Pan, Zoom, Mod ve Acil SOS Girişi  |   TI TCAN337G       |  - AES-128-CTR Şifreli BFT Mesh (TX/RX|
 |                                       |   +-70V Korumalı    |                                       |
 | SENSÖR: Bosch Sensortec BMP581        |                     | TIER-2 UYDU: RockBLOCK 9603 (Iridium) |
 |  - +-0.06 hPa Hassas Baro Altimetre   |                     |  - 1621 MHz SBD Çift Yönlü BLOS Uydu  |
 |  - Düşey Türevli TRN Filtresi         |                     |  - Si2301 P-MOSFET Güç Kapısı (0.0uA) |
 +---------------------------------------+                     |                                       |
                                                               | GÜÇ & TERMAL:                         |
                                                               |  - Molicel 21700 + PTC Isıtıcı Ped    |
                                                               |  - 5.0F Düşük ESR Süperkapasitör Bankı|
                                                               |  - TI TPL5010 Donanımsal Watchdog     |
                                                               +---------------------------------------+
                                                                                  | |
                                                   +------------------------------+ +--------------------+
                                                   |                                                     |
                                                   v [1W LoRa TX/RX]                                     v [Iridium SBD]
                                      +-------------------------+                           +-------------------------+
                                      | DOST BİRLİK TAKİP (BFT) |                           | KÜRESEL UYDU ŞEBEKESİ   |
                                      | Tim Düğümleri (P2P/Mesh)|                           | BLOS Acil SOS & Telemetri|
                                      +-------------------------+                           +-------------------------+
```

---

## 📡 Taktik Veri Bağı (Data Link / TX-RX) & Dost Birlik Takip (BFT)

Sistem pasif bir alıcı olmanın ötesinde, taktik sahada tim unsurlarının konum ve operasyonel durumlarını paylaşmasını sağlayan **çift katmanlı (Tier-1 LoRa + Tier-2 Iridium) hibrit taktik veri bağına** sahiptir:

### 1. Tier-1 Yerel Taktik RF Veri Bağı (1W LoRa):
* **RF Alıcı-Verici (Transceiver):** EBYTE E22-900T30D (Semtech SX1262 tabanlı, 868 MHz ISM bandı).
* **Çıkış Gücü ve RF Bütçesi:** $+30\text{ dBm}$ (1000 mW) RF iletim gücü. Görüş hattında (LOS) 12–15 km; açık arazide ve taktik sırt röleleriyle **40–60 km** efektif menzil.
* **Dost Birlik Takip (Blue Force Tracking - BFT):** Tim üyelerinin konumları, irtifaları, hareket yönleri ve hayati durumları periyodik olarak şifrelenmiş paketler halinde yayınlanır.
* **Protokol Güvenliği:** Her BFT paketi donanımsal **AES-128-CTR** ile şifrelenir; paket başına dönen sıra numarası (rolling sequence ID) ile tekrar saldırılarına (replay attacks) karşı korunur ve CRC-16-CCITT ile doğrulanır.
* **Harita Üzerinde Görselleştirme:** Alınan BFT telemetrisi Garmin el terminali haritasında dost birlik sembolleri (mavi taktik ikonlar) ve irtifa etiketleriyle anlık olarak çizilir.

### 2. Tier-2 BLOS (Beyond-Line-of-Sight) Küresel Uydu Failover:
* **Uydu Modülü:** RockBLOCK 9603 (Iridium 9603 Short Burst Data - SBD, 1621 MHz).
* **Otomatik Geçiş Mantığı:** Tim derin bir kanyona veya dağın arkasına geçtiğinde ve LoRa üzerinden 3 ardışık denemede onay (ACK) alınamadığında sistem otomatik olarak Tier-2 Iridium uydusuna geçer.
* **Acil Durum (SOS) Önceliği:** Operatör acil durum buton kombinasyonuna bastığında LoRa kuyruğu baypas edilir; uydu modülü derhal uyandırılarak konum ve tehlike paketi doğrudan yörüngeye fırlatılır.
* **Sıfır Sızıntı Güç Kapılama (Power-Gating):** Iridium modülü uyku modunda bile sızıntı yapmaması için Si2301 P-MOSFET ile besleme hattından tamamen izole edilir (**0.0 µA kaçak akım**). Süperkapasitör bankı 2A'lik tepe iletim akımını pilde voltaj çökmesi yaratmadan karşılar.

---

## ⚡ Merkezi İşlem Birimi (MCU) & Donanım Kalbi (Silicon Specs)

| Pod / Düğüm | İşlemci Modeli | Çekirdek Mimarisi | Saat Hızı | Bellek & Donanım Hızlandırıcı | Temel Görevi |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Sırt Çantası Ana Düğüm (Core)** | **STM32U585CI** *(Ref: STM32L4R5ZI / ESP32-S3)* | ARM Cortex-M33 (TrustZone) | 160 MHz | 2MB Flash, 786KB SRAM, Donanımsal FPU, SAES (AES-128/256), Çift Dairesel DMA | Sıfır-heap C99 çekirdek, DEM 64-sektör polar skymask ray-tracing, EKF Kalman filtreleri, 1W LoRa yığını, süperkapasitör güç denetimi. |
| **Göğüs/Bilek HMI Düğümü** | **STM32G0B1RE** *(Ref: Seeed XIAO ESP32-S3)* | ARM Cortex-M0+ / Dual Xtensa | 64 MHz / 240 MHz | 512KB Flash, 144KB SRAM, 14 µA Standby | Sharp 2.7" MIP ekran Dirty-Line DMA paketleme, Omron 4-buton akordeon durum makinesi, BMP581 baro okuma. |
| **Diferansiyel İletişim Hattı** | **TI TCAN337G** | ISO 11898-2 CAN-FD Transceiver | 5 Mbps | $\pm 70\text{ V}$ Hata Koruması, 30 kV ESD TVS | 1m spiral PUR kablo ile iki düğüm arasında deterministik, gürültü bağışıklı veri iletimi. |

---

## ⚡ Güç Elektroniği: Hibrit İki Kademeli RF Güç Mimarisi (Buck + Ultra-High PSRR LDO)

Taktik seyrüsefer terminallerinde yüksek verimli anahtarlamalı regülatörler (Buck Converter) $1.8 - 2.5\text{ MHz}$ bandında anahtarlama dalgalanması ($20 - 30\text{ mV}_{p-p}$ ripple) üretir. Bu dalgalanma doğrudan $-167\text{ dBm}$ hassasiyetli GNSS LNA'sına ve LoRa PLL katına sızarsa, $C/N_0$ sinyal-gürültü oranını 5–8 dB düşürerek alıcı sağırlığına (Receiver Desensitization) neden olur.

Bu fiziksel kısıtı aşmak için iki kademeli hibrit güç regülasyonu kurgulanmıştır:

```
 [ 21700 / 5.0F SÜPERKAPASİTÖR ] (3.0V - 4.2V)
             |
             v
 [ KADEME 1: SENKRON BUCK-BOOST ] (TI TPS63020 / %96 Verim @ 2.4 MHz)
             |
             +----------------------------> VDD_DIG (3.3V Dijital Ray: MCU, MIP, CAN-FD)
             |                              (Ripple: ~24.5 mVp-p)
             v
 [ Pi-FİLTRE ] (Murata BLM18HE152SN1D Ferrit Boncuk + 2x 10 uF MLCC)
             | (18.2 dB Yüksek Frekans Zayıflatması)
             v
 [ KADEME 2: ULTRA-HIGH PSRR LDO ] (TI TPS7A2030PDBVR: 95dB PSRR @ 1kHz, 52.8dB @ 2.4MHz)
             |
             v
       VDD_RF (3.0V Temiz Analog/RF Rayı)
       - Kalan Dalgalanma: < 7.0 uVp-p (71 dB toplam izolasyon)
       - GNSS LNA Desense Kaybı: Delta C/N0 < 0.001 dB (Desense Sıfırlandı)
       - Quectel LC29H / TBS M10Q LNA & LoRa TCXO/PLL Beslemesi
```

* **Sonuç:** Dijital alt sistemler $\%96$ verimle batarya ömrünü maksimize ederken, RF alıcı katı $71\text{ dB}$ güç izolasyonu sayesinde kanyon içi zayıf uydu sinyallerini kilitlenme kaybı olmadan izler.

---

## 📂 Repository Architecture (Dizin Mimarisi)

```
gnss-kalman/
├── src/                  # ANSI C99 Gömülü Firmware Modülleri (Sıfır-Heap)
│   ├── hybrid_comms.c    # 1W LoRa & Iridium SBD Hibrit Veri Bağı, BFT & Uydu Failover
│   ├── cgpx_engine.c     # DPCM + LEB128 Varint Kayıpsız Rota Sıkıştırma
│   ├── mip_display.c     # Sharp MIP Ekran Sürücüsü (400x240 1-bit Monokrom)
│   ├── chord_fsm.c       # 4-Buton Akordeon (Chording) Giriş Durum Makinesi
│   ├── split_node_bus.c  # Ayrık Gövde CAN-FD / Diferansiyel İletişim Protokolü
│   ├── gnss_nmea.c       # Halka Bellek DMA NMEA Tokenizer & Ayrıştırıcı
│   ├── crypto_hal.c      # STM32 SAES / Yazılımsal AES-128-CTR Kripto Katmanı
│   ├── nlos_filter.c     # 64-Sektör Polar Topografik Skymask Engelleyici
│   └── power_supervisor.c# Sub-Zero Lityum Koruma & Termal Histerezis Denetçisi
│
├── include/              # C99 Başlık Dosyaları & Protokol Tanımları (*.h)
│   ├── hybrid_comms.h    # Taktik Veri Bağı, BFT Beacon & Iridium API
│   ├── split_node_bus.h  # CAN-FD Çerçeve & BFT Mesaj Tanımlayıcıları
│   └── crypto_hal.h      # AES-128-CTR Donanım Hızlandırıcı Arayüzü
│
├── web/                  # Taktik El Terminalleri & Donanım Köprü Servisleri
│   ├── tactical_terminal.html # Garmin GPSMAP 67i Taktik El Terminali Arayüzü
│   ├── gnss_tracker.html      # Canlı Çoklu-GNSS Harita & Telemetri Takipçisi
│   ├── open_weather_lab.py    # Python Donanım Seri Köprüsü & HTTP Daemon
│   ├── tactical_survival_meteorology.html # MET-SURV OPS: Taktik Meteoroloji Analizörü
│   └── cgpx_studio.html       # Rota Kripto & 400x240 MIP Stüdyosu
│
├── firmware/             # Donanım & Mikrodenetleyici Test Kodları
│   ├── gps_passthrough/  # TBS M10Q UART Passthrough Monitör (.ino)
│   ├── tft_screen_test/  # SPI Ekran Donanım Test Kodu (.ino)
│   └── tft_st7789_test/  # ST7789 Ekran Doğrulama Kodu (.ino)
│
├── scripts/              # Taşınabilir Başlatıcı & Otomasyon Betikleri
│   ├── start_terminal.bat     # Donanım Köprüsü & Garmin Terminal Başlatıcı
│   └── start_weather_lab.bat  # Taktik Meteoroloji Laboratuvarı Başlatıcı
│
├── tests/                # Doğrulama Test Süitleri (C Bare-Metal & Python)
│   ├── test_phases_all.py     # Master Sistem Doğrulama Süiti (Faz 1 - 7)
│   ├── test_hybrid_comms.py   # LoRa BFT & Iridium Failover Doğrulama Testi
│   ├── test_rf_power_psrr.py  # Hibrit RF Güç Mimarisi & LDO PSRR Doğrulama Testi
│   ├── test_cgpx.py           # Sıkıştırma, Şifreleme & C99 Motor Testleri
│   ├── test_topo.py           # Topografik Arazi & Sayısal Yükseklik Testleri
│   ├── test_map_stability.py  # Harita Kararlılık & Döngü Testi
│   └── test_harness_*.c       # MinGW GCC Bare-Metal Test Koşucuları
│
├── tools/                # Python DEM, 3D Hillshade & Coğrafi CLI Araçları
├── data/                 # Örnek Alp Rotaları (GPX/CGPX) ve 30m DEM GeoTIFF
├── docs/                 # Mühendislik Şartnameleri, V&V Raporları ve BOM Dosyaları
├── assets/               # Çıktı Görselleri, Topografik Matrisler ve Şematikler
├── .gitignore            # Derleme ikilileri ve önbellek filtreleri
├── LICENSE               # MIT Lisansı
└── README.md             # Proje Ana Dokümantasyonu
```

---

## 🚀 Quick Start (Hızlı Başlangıç)

### 1. Gereksinimler
* Python 3.10+
* `pyserial`, `numpy`, `pillow`, `cryptography`
* Opsiyonel: MinGW GCC (C99 bare-metal test derlemeleri için)

```bash
pip install pyserial numpy pillow cryptography
```

### 2. Canlı Taktik Terminali Başlatma
TBS M10Q modülünü USB portuna bağlayıp köprü servisini başlatın:

```bash
python web/open_weather_lab.py
# veya Windows ortamında: scripts\start_terminal.bat
```

* **Garmin GPSMAP 67i Terminali:** `http://127.0.0.1:8080/tactical_terminal.html`
* **Canlı GNSS Telemetri Takipçisi:** `http://127.0.0.1:8080/gnss_tracker.html`

### 3. Doğrulama Testlerini Çalıştırma (Master V&V Harness)
Tüm firmware modüllerini, veri bağını ve matematiksel algoritmaları doğrulamak için:

```bash
python tests/test_phases_all.py
```

* **Faz 1:** CGPX Kayıpsız Sıkıştırma & C99 Zero-Heap Motoru
* **Faz 2:** Sharp 2.7" MIP Ekran & Chording FSM Giriş Durum Makinesi
* **Faz 3:** Ayrık Gövde ISO 11898-2 CAN-FD Diferansiyel Veriyolu & Hata İyileştirme
* **Faz 4:** GNSS Dairesel DMA Tokenizer, Skymask & Kripto HAL
* **Faz 5:** Sub-Zero Güç Denetçisi, Termal Histerezis & TI TPL5010 Donanımsal Watchdog
* **Faz 6:** Taktik Topografik Arazi Motoru, DEM Hillshade & Marching Squares
* **Faz 7:** Taktik Veri Bağı, 1W LoRa BFT Durum Makinesi & Iridium Uydu Failover

---

## 🗺️ Taktik Harita Motoru
* **OSM Topo Active:** Ayrıntılı arazi, patika ve yükseklik eğrileri (100% ücretsiz, açık CDN).
* **BirdsEye Satellite:** Yüksek çözünürlüklü hibrit uydu görüntüleri (API Key gerektirmez).
* **NVG Dark:** Gece operasyonları için yüksek kontrastlı askeri karanlık tema.
* **Vector Basemap:** Şebeke ve internet olmayan arazilerde sıfır veriyle çalışan MGRS referans ızgarası.

---

## 📄 Lisans
Bu proje [MIT Lisansı](LICENSE) kapsamında açık kaynak olarak yayımlanmaktadır.
