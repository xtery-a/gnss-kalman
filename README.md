# 🛰️ Tactical Multi-GNSS Terminal & Embedded Kalman Engine
### Extreme-Condition Navigation, Hardware Bridge & Handheld Terminal

> ⚠️ **Proje Durumu:** *Aktif Geliştirme Aşamasında (Work in Progress / WIP)*  
> Bu depo, ekstrem ortam taktik seyrüsefer algoritmaları, gömülü C99 firmware çekirdeği ve u-blox M10Q GNSS alıcısı için geliştirilen donanım köprüsü ile Garmin GPSMAP 67i mimarisini referans alan web tabanlı taktik terminali bir araya getiren açık kaynaklı bir Ar-Ge çalışmasıdır.

---

## 📌 Proje Genel Bakışı

Bu proje iki ana mimari katmandan oluşur:

1. **Gömülü C99 Algoritma & Firmware Çekirdeği (`src/`, `include/`):**
   * **Sıfır Dinamik Bellek:** Kesinlikle dinamik bellek ayırmayan (`CONFIG_HEAP_MEM_POOL_SIZE = 0`), deterministik bare-metal C99 yapısı.
   * **Gelişmiş Filtreleme:** Topografik ufuk maskeleme (NLOS), Zero Velocity Update (ZVU) ve GNSS kinematik filtreleri.
   * **Sıkıştırma & Güvenlik:** DPCM + LEB128 varint tabanlı `.cgpx` ikili rota konteyneri ve AES-128-CTR şifreleme katmanı.
   * **Ekstrem Dayanıklılık:** $-30^\circ\text{C}$ lityum batarya termal koruması ve donanımsal watchdog denetleyicisi.

2. **Canlı Donanım Köprüsü & Taktik El Terminali (`web/`):**
   * **Donanım Köprüsü (`web/open_weather_lab.py`):** TBS M10Q (u-blox 10. Nesil çoklu konstelasyon: GPS, GLONASS, Galileo, BeiDou) modülünü otomatik algılayan, NMEA 0183 (`$GNGGA`, `$GNRMC`, `$GNGSV`) akışını çözümleyen yerel köprü.
   * **Garmin Taktik Terminali (`web/tactical_terminal.html`):** Garmin GPSMAP 67i endüstriyel formunu ve buton mantığını emüle eden, MGRS/WGS84 destekli, 4 katmanlı (OSM Topo, BirdsEye Uydu, Askeri NVG Dark, Çevrimdışı Vektör Basemap) harita motoru.
   * **17 Çevrimdışı Taktik Modül:** Sight 'N Go, Rota Planlayıcı, Poligon Alanı Hesaplama (Shoelace), Geofence Çevre Güvenliği, Güneş/Ay Efemeris ve SOS Morse flaşörü.

---

## 📂 Dizin Yapısı

```
gnss-kalman/
├── src/                  # ANSI C99 Gömülü Firmware Modülleri
│   ├── cgpx_engine.c     # DPCM + LEB128 Varint Sıkıştırma Motoru
│   ├── mip_display.c     # Sharp MIP Ekran Sürücüsü (400x240 1-bit)
│   ├── chord_fsm.c       # 4-Buton Akordeon (Chording) Durum Makinesi
│   ├── split_node_bus.c  # Ayrık Gövde İletişim Yığını
│   ├── gnss_nmea.c       # NMEA Tokenizer & Ayrıştırıcı
│   ├── crypto_hal.c      # AES-128-CTR Şifreleme Katmanı
│   ├── nlos_filter.c     # Polar Topografik Skymask Engelleyici
│   └── power_supervisor.c# Termal Batarya Koruma Denetçisi
│
├── include/              # C99 Başlık Dosyaları & ROM Tabloları (*.h)
│
├── web/                  # Taktik Terminaller & Canlı Donanım Köprüsü
│   ├── tactical_terminal.html # Garmin GPSMAP 67i Taktik El Terminali
│   ├── gnss_tracker.html      # Canlı Çoklu-GNSS Harita & Telemetri Takipçisi
│   ├── open_weather_lab.py    # Python Donanım Seri Köprüsü & HTTP Sunucusu
│   ├── tactical_survival_meteorology.html # MET-SURV OPS: Meteorolojik Hayatta Kalma
│   └── cgpx_studio.html       # Rota Sıkıştırma & MIP Simülasyon Stüdyosu
│
├── firmware/             # ESP32 / Arduino Donanım Test Kodları
│
├── tests/                # Doğrulama Test Süitleri (C & Python)
│   ├── test_map_stability.py  # Playwright Harita Kararlılık & Döngü Testi
│   ├── test_phases_all.py     # Faz 1 - Faz 6 Master Doğrulama Süiti
│   └── test_harness_*.c       # Bare-metal C test koşucuları
│
├── tools/                # Python DEM, Hillshade & Rota CLI Araçları
├── data/                 # Örnek GPX/CGPX Rotaları ve DEM GeoTIFF Verileri
├── docs/                 # Şartnameler, V&V Raporları ve BOM Dosyaları
└── assets/               # Şematikler, Topografik Çıktılar ve Görseller
```

---

## 🚀 Hızlı Başlangıç

### 1. Gereksinimler
* Python 3.10+
* `pyserial` (Donanım köprüsü için)
* Opsiyonel: `playwright` (Otomatik UI testleri için)

```bash
pip install pyserial
```

### 2. Canlı Taktik Terminali Başlatma
TBS M10Q veya NMEA çıkışı veren herhangi bir GPS modülünü USB'ye takıp köprüyü çalıştırın:

```bash
python web/open_weather_lab.py
```

Tarayıcınız otomatik açılacaktır:
* **Garmin GPSMAP 67i Terminali:** `http://127.0.0.1:8080/tactical_terminal.html`
* **Canlı GNSS Telemetri Takipçisi:** `http://127.0.0.1:8080/gnss_tracker.html`
* *(Not: Sensör bağlı olmadığında arayüz çevrimdışı bekleme modunda açılır, tüm harita ve matematiksel seyrüsefer araçları kullanılabilir durumdadır).*

### 3. Otomatik Doğrulama Testlerini Çalıştırma
Tüm firmware fazlarını ve algoritma motorlarını test etmek için:

```bash
python test_phases_all.py
```

Tarayıcı harita ve terminal döngü kararlılığını test etmek için:

```bash
python tests/test_map_stability.py
```

---

## 🗺️ Taktik Harita Katmanları & Seyrüsefer
* **OSM Topo Active:** Ayrıntılı arazi, patika ve yükseklik eğrileri (100% ücretsiz, açık CDN).
* **BirdsEye Satellite:** Yüksek çözünürlüklü hibrit uydu görüntüleri (API Key gerektirmez).
* **NVG Dark:** Gece operasyonları için yüksek kontrastlı askeri karanlık tema.
* **Vector Basemap:** İnternet ve şebeke bulunmayan arazilerde sıfır veriyle çalışan MGRS referans ızgarası.

---

## 📄 Lisans & Katkı
Bu proje açık kaynaklı araştırma ve eğitim amaçlı geliştirilmektedir. Detaylar için [LICENSE](LICENSE) dosyasına göz atabilirsiniz.
