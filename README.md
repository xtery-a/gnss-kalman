# 🛰️ Tactical Multi-GNSS Terminal & Embedded Kalman Engine
### Extreme-Condition Navigation, Hardware Bridge & Handheld Terminal

> ⚠️ **Project Status:** *Active Research & Development (WIP / Work in Progress)*  
> Open-source tactical navigation research combining an ANSI C99 bare-metal firmware core, hardware sensor bridge (u-blox M10Q), and a web-based handheld tactical terminal inspired by Garmin GPSMAP 67i architecture.

---

## 📌 Project Overview (Proje Genel Bakışı)

Bu sistem, ekstrem çevre koşullarında ($-30^\circ\text{C}$ dondurucu soğuk, derin vadi/kanyon gölgelenmesi ve sıfır şebeke ortamı) kesintisiz, yüksek hassasiyetli seyrüsefer ve telemetri sağlamak üzere iki ana katmanda kurgulanmıştır:

1. **Embedded C99 Firmware & Algorithmic Core (`src/`, `include/`):**
   * **Zero-Dynamic Memory Allocation:** Deterministik yürütme ve mutlak bellek güvenliği için sıfır-heap mimarisi (`CONFIG_HEAP_MEM_POOL_SIZE = 0`).
   * **NLOS (Non-Line-of-Sight) Filter:** 3D DEM sayısal yükseklik modelinden türetilen 64-sektör polar ufuk maskesi ile kanyon yansımalarını (multipath) ayıklama.
   * **Lossless CGPX Compression & Crypto:** DPCM + LEB128 varint kodlama ile nokta başına 4–6 bayt sıkıştırma ve AES-128-CTR kriptografik koruma.
   * **Cryo-Sentinel Power Supervision:** Dondurucu soğukta lityum kaplanma koruması, histerezisli PTC ön-ısıtıcı denetimi ve süperkapasitör yük tamponlama.

2. **Hardware Bridge & Tactical Terminal Interface (`web/`, `scripts/`):**
   * **u-blox 10th Gen GNSS Bridge:** TBS M10Q (GPS, GLONASS, Galileo, BeiDou eşzamanlı takibi) alıcısını otomatik tanıyan, NMEA 0183 (`$GNGGA`, `$GNRMC`, `$GNGSV`) akışını gerçek zamanlı ayrıştıran Python seri köprüsü.
   * **Garmin GPSMAP 67i Handheld Terminal:** Endüstriyel tuş takımı (D-Pad rocker), MGRS/WGS84 çift koordinat desteği ve 4 katmanlı harita motoru (OSM TopoActive, BirdsEye Uydu, Askeri NVG Dark, Sıfır-Şebeke Vektör Basemap).
   * **17 Offline Tactical Engines:** Sight 'N Go (Kerterizle ve Git), Rota Planlayıcı, Poligon Alan Hesaplayıcı (Shoelace formülü), Geofence Çevre Güvenlik Alarmı, Güneş/Ay Efemeris ve SOS Morse flaşörü.

---

## 📂 Repository Architecture (Dizin Mimarisi)

```
gnss-kalman/
├── src/                  # ANSI C99 Gömülü Firmware Modülleri
│   ├── cgpx_engine.c     # DPCM + LEB128 Varint Kayıpsız Rota Sıkıştırma
│   ├── mip_display.c     # Sharp MIP Ekran Sürücüsü (400x240 1-bit Monokrom)
│   ├── chord_fsm.c       # 4-Buton Akordeon (Chording) Giriş Durum Makinesi
│   ├── split_node_bus.c  # Ayrık Gövde CAN-FD / Seri İletişim Protokolü
│   ├── gnss_nmea.c       # Halka Bellek DMA NMEA Tokenizer & Ayrıştırıcı
│   ├── crypto_hal.c      # Donanımsal/Yazılımsal AES-128-CTR Kripto Katmanı
│   ├── nlos_filter.c     # 64-Sektör Polar Topografik Skymask Engelleyici
│   └── power_supervisor.c# Sub-Zero Lityum Koruma & Termal Histerezis Denetçisi
│
├── include/              # C99 Başlık Dosyaları & ROM Tabloları (*.h)
│
├── web/                  # Taktik Terminaller & Donanım Köprü Servisleri
│   ├── tactical_terminal.html # Garmin GPSMAP 67i Taktik El Terminali Arayüzü
│   ├── gnss_tracker.html      # Canlı Çoklu-GNSS Harita & Telemetri Takipçisi
│   ├── open_weather_lab.py    # Python Donanım Seri Köprüsü & HTTP Daemon
│   ├── tactical_survival_meteorology.html # MET-SURV OPS: Taktik Meteoroloji & Bivak Analizörü
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
│   ├── test_phases_all.py     # Master Sistem Doğrulama Süiti (Faz 1 - 6)
│   ├── test_cgpx.py           # Sıkıştırma, Şifreleme & C99 Motor Testleri
│   ├── test_topo.py           # Topografik Arazi & Sayısal Yükseklik Testleri
│   ├── test_map_stability.py  # Playwright Harita Kararlılık & Döngü Testi
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
* Opsiyonel: MinGW GCC (C99 test derlemeleri için), `playwright` (UI otomasyon testleri için)

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
* *(Not: Sensör bağlı olmadığında arayüz çevrimdışı bekleme modunda açılır, tüm harita ve matematiksel seyrüsefer araçları kullanılabilir durumdadır).*

### 3. Doğrulama Testlerini Çalıştırma (Verification & Validation)
Tüm firmware modüllerini ve matematiksel algoritmaları doğrulamak için:

```bash
python tests/test_phases_all.py
```

Tekil modül testleri:
```bash
python tests/test_cgpx.py    # CGPX Kayıpsız Sıkıştırma & AES-128-CTR Testi
python tests/test_topo.py    # 3D DEM, Hillshade & Marching Squares Testi
```

---

## 🗺️ Taktik Harita Mimarisi
* **OSM Topo Active:** Ayrıntılı arazi, patika ve yükseklik eğrileri (100% ücretsiz, açık CDN).
* **BirdsEye Satellite:** Yüksek çözünürlüklü hibrit uydu görüntüleri (API Key gerektirmez).
* **NVG Dark:** Gece operasyonları için yüksek kontrastlı askeri karanlık tema.
* **Vector Basemap:** Şebeke ve internet olmayan arazilerde sıfır veriyle çalışan MGRS referans ızgarası.

---

## 📄 Lisans
Bu proje [MIT Lisansı](LICENSE) kapsamında açık kaynak olarak yayımlanmaktadır.
