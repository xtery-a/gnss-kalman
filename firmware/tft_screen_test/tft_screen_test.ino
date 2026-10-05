#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// ESP32 DevKit V1 Pin Tanimlamalari (VSPI)
#define TFT_CS   5   // Chip Select
#define TFT_DC   2   // Data / Command
#define TFT_RST  4   // Reset
// SDI (MOSI) -> GPIO 23 (Donanimsal SPI)
// SCK        -> GPIO 18 (Donanimsal SPI)
// LED        -> 3.3V (Arka aydinlatma)

// Guvenli ve temiz 20MHz SPI hizi (Jumper parazitini sifirlar)
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

int heading = 0;
int frameCount = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[TAC-TERM] 2.8\" TFT SPI Ekran Baslatiliyor...");

  // 20 MHz SPI hizi ile baslat (Jumper kablo parazitini onler)
  tft.begin(20000000);

  // Yatay mod (320x240 piksel)
  tft.setRotation(1);

  // RENK INVERSION (Beyaz sis/filtreyi siyah arka plana cevirir!)
  tft.invertDisplay(true);

  // Ekrani tamamen siyaha boya
  tft.fillScreen(ILI9341_BLACK);
  delay(200);

  // Statik Taktik HUD Arayuzunu Ciz
  drawStaticHUD();
}

void loop() {
  // Canli Pusula Ibresini Guncelle
  updateCompass(heading);
  heading = (heading + 5) % 360;

  // Canli Telemetriyi Guncelle
  updateTelemetry();

  // Uydu Barlarini Simule Et
  updateSatelliteBars();

  frameCount++;
  delay(100);
}

void drawStaticHUD() {
  // Ust Baslik Bari
  tft.fillRect(0, 0, 320, 26, ILI9341_NAVY);
  tft.drawFastHLine(0, 26, 320, ILI9341_CYAN);
  
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setCursor(8, 9);
  tft.print("TAC-TERM v1.0 [EXTREME-GNSS]");

  tft.setTextColor(ILI9341_GREEN);
  tft.setCursor(240, 9);
  tft.print("STATUS: OK");

  // Dikey Ayrac Cizgisi
  tft.drawFastVLine(150, 27, 185, ILI9341_DARKGREY);

  // Pusula Cercevesi (Sol Alan)
  tft.drawCircle(75, 115, 45, ILI9341_WHITE);
  tft.drawCircle(75, 115, 46, ILI9341_DARKCYAN);
  tft.setTextColor(ILI9341_RED);
  tft.setCursor(72, 62);  tft.print("N");
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(72, 163); tft.print("S");
  tft.setCursor(24, 112); tft.print("W");
  tft.setCursor(123, 112); tft.print("E");

  // Alt Renk Test Seridi (RGB Panel Testi)
  int barW = 320 / 7;
  uint16_t colors[] = {ILI9341_RED, ILI9341_GREEN, ILI9341_BLUE, ILI9341_YELLOW, ILI9341_CYAN, ILI9341_MAGENTA, ILI9341_WHITE};
  for (int i = 0; i < 7; i++) {
    tft.fillRect(i * barW, 222, barW, 18, colors[i]);
  }
}

void updateCompass(int deg) {
  // Eski pusula icini temizle
  tft.fillCircle(75, 115, 40, ILI9341_BLACK);

  // Yeni aci cizgisi (Radyan)
  float rad = deg * 0.0174532925;
  int xEnd = 75 + (int)(35 * sin(rad));
  int yEnd = 115 - (int)(35 * cos(rad));

  // Ibreyi ciz
  tft.drawLine(75, 115, xEnd, yEnd, ILI9341_RED);
  tft.drawCircle(75, 115, 4, ILI9341_RED);

  // Aciyi sayi olarak yaz
  tft.fillRect(50, 175, 50, 16, ILI9341_BLACK);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(52, 175);
  if (deg < 10) tft.print("00");
  else if (deg < 100) tft.print("0");
  tft.print(deg);
  tft.print((char)247); // Derece sembolu
}

void updateTelemetry() {
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_GREENYELLOW, ILI9341_BLACK);

  // Koordinat alani (Sag ust)
  tft.setCursor(160, 36);
  tft.print("LAT: 41 27.184 N");
  tft.setCursor(160, 48);
  tft.print("LON: 31 47.925 E");
  tft.setCursor(160, 60);
  tft.print("ALT: 142.6 m MSL");
  tft.setCursor(160, 72);
  tft.print("SPD: 0.0 km/h (FIX)");

  tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  tft.setCursor(160, 88);
  tft.print("HDOP: 0.8  (M10Q 3D)");
  tft.setCursor(160, 100);
  tft.print("TOTAL SATS: 28");
}

void updateSatelliteBars() {
  // 4 Uydu Agi Sinyal Barlari (GPS, GLO, GAL, BDS)
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(160, 120);
  tft.print("CONSTELLATIONS:");

  const char* names[] = {"GPS", "GLO", "GAL", "BDS"};
  int vals[] = {8, 7, 6, 7};
  uint16_t barColors[] = {ILI9341_GREEN, ILI9341_BLUE, ILI9341_YELLOW, ILI9341_CYAN};

  for (int i = 0; i < 4; i++) {
    int y = 135 + (i * 18);
    tft.setCursor(160, y);
    tft.print(names[i]);
    
    // Bar cercevesi ve dolgusu
    tft.drawRect(190, y - 2, 100, 10, ILI9341_DARKGREY);
    int barWidth = (vals[i] * 12);
    tft.fillRect(191, y - 1, barWidth, 8, barColors[i]);
    tft.fillRect(191 + barWidth, y - 1, 98 - barWidth, 8, ILI9341_BLACK);
  }
}
