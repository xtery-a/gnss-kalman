# 🧭 GNSS & BAROMETRİK DİKEY FÜZYON MİMARİSİ VE YOL HARİTASI
**Proje:** MET-SURV Taktik GNSS & Hayatta Kalma Telemetrisi  
**Modül:** 3. Faz Entegrasyonu - 1D Dikey Kalman Filtresi & Barometrik Altimetre Füzyonu  
**Hedef Donanım:** TBS M10Q (u-blox MAX-M10S) + I2C Barometre (BMP280 / BMP390 / DPS310) + ESP32 / Host PC  
**Tarih:** 27 Eylül 2026  
**Durum:** [FAZ 3 - KAYITLI YOL HARİTASI / TASARIM TAMAMLANDI]

---

## 1. Fiziksel ve Matematiksel Problem Tanımı

### 1.1 GNSS Dikey Eksen Kısıtı (VDOP Problemi)
GNSS uyduları yalnızca ufuk çizgisinin üzerinde yer alabilir (dünyanın altından sinyal alınamaz). Geometrik seyreltme (DOP - Dilution of Precision) gereği:
$$\text{VDOP} \approx 1.5 \sim 2.5 \times \text{HDOP}$$
Pencere kenarı ve kentsel kanyon testlerinde:
- Yatay saçılım (CEP %50): ~1.8 - 4.0 metre
- Dikey saçılım ($\sigma_{\text{Alt}}$): **±5.74 - 15+ metre**
- GNSS irtifası tek başına bir dağ tırmanışında, taktik intikalde veya İHA/drone irtifa korumasında güvenilir değildir; saniyeler içinde 10 metrelik sahte irtifa sıçramaları (multipath kaynaklı) yaşanır.

### 1.2 Barometrik Altimetrenin Üstünlüğü ve Zaafı
- **Üstünlük:** Modern MEMS barometreler (BMP390 / DPS310) ~0.02 - 0.05 hPa hassasiyetle çalışır. Bu da **10 - 25 cm bağıl irtifa çözünürlüğü** ve milisaniyelik dinamik tepki demektir.
- **Zaaf:** Atmosferik basınç (QNH), hava durumu cephe geçişleri ve sıcaklıkla saatlik olarak yavaşça kayar (Barometrik Drift). Sadece barometre kullanılırsa 4 saat içinde irtifa göstergesi 30-50 metre sapabilir.

### 1.3 Füzyon Çözümü (Doğal Ortaklık)
| Sensör | Kısa Vadeli Dinamikler (< 10 sn) | Uzun Vadeli Mutlak Doğruluk (> 5 dk) | Gürültü Seviyesi |
| :--- | :--- | :--- | :--- |
| **GNSS irtifası** | Zayıf (Gürültülü, 5-15m sıçrama) | **Mükemmel (Drift yapmaz, mutlak WGS84)** | Yüksek |
| **Barometre** | **Mükemmel (< 20 cm, anlık VSI tepkisi)** | Zayıf (Hava değiştikçe kayar) | Çok Düşük |

Bu iki sensörün birbirini tamamlayan gürültü karakteristikleri (Complementary Noise Characteristics), **1D Genişletilmiş/Lineer Kalman Filtresi** ile birleştirildiğinde sub-metre (< 0.4 m) dikey doğruluk elde edilir.

---

## 2. 1D Durum Uzayı (State-Space) & Kalman Formülasyonu

### 2.1 Durum Vektörü (State Vector)
Sistem 3 değişkenli durum vektörü ile tanımlanır:
$$\mathbf{x}_k = \begin{bmatrix} h_k \\ v_{z, k} \\ b_{\text{baro}, k} \end{bmatrix}$$
- $h_k$: Gerçek mutlak irtifa (metre, WGS84 / MSL).
- $v_{z, k}$: Dikey tırmanma hızı (Vertical Speed Indicator / VSI, $\text{m/s}$).
- $b_{\text{baro}, k}$: Atmosferik hava durumundan kaynaklanan barometre irtifa ofseti/kayması (metre).

### 2.2 Durum Geçiş Modeli (State Transition Matrix $\mathbf{F}$)
Örnekleme periyodu $\Delta t$ olmak üzere:
$$\mathbf{x}_{k|k-1} = \mathbf{F} \mathbf{x}_{k-1} + \mathbf{w}_k$$

$$\mathbf{F} = \begin{bmatrix} 1 & \Delta t & 0 \\ 0 & 1 & 0 \\ 0 & 0 & 1 \end{bmatrix}$$

Süreç gürültüsü kovaryans matrisi $\mathbf{Q}$:
$$\mathbf{Q} = \begin{bmatrix} \frac{1}{3}\Delta t^3 q_{\text{acc}} & \frac{1}{2}\Delta t^2 q_{\text{acc}} & 0 \\ \frac{1}{2}\Delta t^2 q_{\text{acc}} & \Delta t q_{\text{acc}} & 0 \\ 0 & 0 & \Delta t q_{\text{drift}} \end{bmatrix}$$
- $q_{\text{acc}}$: Beklenen dikey ivme dinamizmi ($\approx 0.1 - 0.5 \text{ m}^2/\text{s}^3$).
- $q_{\text{drift}}$: Barometre ofsetinin zamansal kayma hızı ($\approx 10^{-5} \text{ m}^2/\text{s}$, hava cephesi kayması çok yavaştır).

### 2.3 Asenkron Ölçüm Modeli (Measurement Updates)

#### A. Barometre Güncellemesi (Yüksek Frekans: 10 - 50 Hz)
Barometreden okunan basınç ($P$), Hipsometrik formülle anlık ham irtifaya çevrilir:
$$h_{\text{raw\_baro}} = 44330 \cdot \left(1 - \left(\frac{P}{1013.25}\right)^{0.190295}\right)$$
Barometre gözlem matrisi:
$$\mathbf{H}_{\text{baro}} = \begin{bmatrix} 1 & 0 & 1 \end{bmatrix}$$
Gözlem gürültüsü kovaryansı:
$$R_{\text{baro}} = \sigma_{\text{baro}}^2 \approx (0.15 \text{ m})^2 = 0.0225$$
Yenilik (Innovation):
$$y_{\text{baro}} = h_{\text{raw\_baro}} - (h_{k|k-1} + b_{\text{baro}, k|k-1})$$

#### B. GNSS İrtifa Güncellemesi (Düşük Frekans: 1 - 5 Hz)
TBS M10Q NMEA GGA veya NAV-PVT mesajından gelen elipsoidal/MSL irtifa:
$$\mathbf{H}_{\text{gnss}} = \begin{bmatrix} 1 & 0 & 0 \end{bmatrix}$$
Gözlem gürültüsü kovaryansı (Dinamik VDOP ağırlıklı):
$$R_{\text{gnss}} = (\sigma_{\text{gnss\_nominal}} \cdot \text{VDOP})^2$$
*Örnek:* $\text{VDOP} = 2.0$ ise, $R_{\text{gnss}} = (2.5 \cdot 2.0)^2 = 25.0 \text{ m}^2$.  
Yenilik (Innovation):
$$y_{\text{gnss}} = h_{\text{raw\_gnss}} - h_{k|k-1}$$

**Sonuç:** GNSS güncellemesi, barometrenin ofsetini ($b_{\text{baro}}$) yavaşça absorbe ederek gerçek irtifaya kilitler. Barometre güncellemesi ise saniyenin kesirlerindeki dikey sıçramaları filtreler ve pürüzsüz 0.1 m hassasiyetinde VSI üretir.

---

## 3. Donanım Bağlantı Şeması ve Pinout

```
+-------------------------------------------------------------+
|                        ESP32-S3 / MCU                       |
|                                                             |
|   [GPIO 21] (I2C SDA) <---------> [SDA] BMP390 / DPS310     |
|   [GPIO 22] (I2C SCL) <---------> [SCL] Barometrik Sensör   |
|   [3.3V]              <---------> [VIN]                     |
|   [GND]               <---------> [GND]                     |
|                                                             |
|   [GPIO 16] (RX2)     <---------- [TX]  TBS M10Q            |
|   [GPIO 17] (TX2)     ----------> [RX]  (u-blox MAX-M10S)   |
|   [5V / 3.3V]         <---------> [5V]                      |
|   [GND]               <---------> [GND]                     |
+-------------------------------------------------------------+
```

### Tavsiye Edilen Sensör Karşılaştırması
1. **Bosch BMP390:**
   - Göreceli doğruluk: $\pm 3 \text{ cm}$ (0.03 hPa)
   - Sıcaklık kararlılığı: Sektör standardı, entegre IIR filtre.
   - Önerilen çalışma modu: Ultra-high precision, 25 Hz.
2. **Infineon DPS310:**
   - Göreceli doğruluk: $\pm 5 \text{ cm}$
   - Düşük güç tüketimi, kentsel drone/taktik cihazlar için optimize.
3. **Bosch BMP280:**
   - Ekonomik alternatif, $\pm 12 \text{ cm}$ göreceli doğruluk.

---

## 4. Yazılım Uygulama Adımları (Faz 3 Kod Planı)

### Adım 1: Sensör Okuma & Veri Senkronizasyonu (ESP32 C++ / MicroPython / Host Python)
- I2C üzerinden 20 Hz örnekleme ile `pressure_pa` ve `temperature_c` okunması.
- UART üzerinden 1 Hz / 5 Hz TBS M10Q NMEA/UBX akışının yakalanması.
- Zaman damgalı dairesel kuyruk (circular buffer) ile asenkron verilerin füzyon modülüne beslenmesi.

### Adım 2: 1D Kalman Filtresi C++ / JS Uygulaması
```cpp
class BaroGnssFusionFilter {
private:
    float h = 0.0f;       // İrtifa (m)
    float vz = 0.0f;      // Dikey Hız (m/s)
    float b_baro = 0.0f;  // Barometrik kayma ofseti (m)
    float P[3][3];        // Hata kovaryans matrisi
    
public:
    void predict(float dt, float q_acc = 0.2f, float q_drift = 1e-5f);
    void updateBaro(float baroAltMeters, float r_baro = 0.04f);
    void updateGnss(float gnssAltMeters, float vdop, float baseSigma = 2.5f);
    
    float getAltitude() const { return h; }
    float getClimbRate() const { return vz; }
    float getBaroBias() const { return b_baro; }
};
```

### Adım 3: Web HUD & Telemetri Entegrasyonu
- `web/gnss_tracker.html` arayüzüne **Dikey Profil & VSI Variometre Göstergesi** eklenmesi.
- Akustik variometre (sesli bip tonu ile termal tırmanış/çöküş uyarısı - paraşüt/dağcılık modu).
- Gerçek zamanlı grafik: `Ham GNSS İrtifası` vs. `Ham Baro İrtifası` vs. `Füzyon İrtifası`.

---

## 5. Başarı Kriterleri ve Doğrulama
1. **Statik Test (Masaüstü/Pencere Kenarı):** 1 saatlik statik ölçümde dikey saçılım $\sigma_{\text{Alt}}$ değerinin $\mathbf{\pm 5.74 \text{ m}}$'den **$< \pm 0.35 \text{ m}$** seviyesine indirilmesi.
2. **Dinamik Test (Merdiven Çıkışı / Asansör):** Her kat çıkıldığında (~3.0 metre) filtrenin 1 saniye içinde sıfır gecikme ve aşma olmaksızın adımı yakalaması.
3. **Barometrik Fırtına/Cephe Dayanımı:** QNH değiştiğinde GNSS güncellemesinin 10 dakika içinde barometrik ofseti $b_{\text{baro}}$ değişkenine aktararak mutlak irtifayı hatasız koruması.

---
*Bu mimari belge, MET-SURV projesinin sonraki fazında I2C donanım modülü bağlandığı anda doğrudan uygulanmak üzere kaydedilmiş ve dondurulmuştur.*
