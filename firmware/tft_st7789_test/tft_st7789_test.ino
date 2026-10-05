#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[TEST] ST7789 Sürücüsü ile Başlatılıyor...");

  // ST7789 240x320 başlatma
  tft.init(240, 320, SPI_MODE0);
  tft.setRotation(1); // 320x240

  // IPS Ekranlarda Siyah-Beyaz Inversion (Beyaz filtreyi yok eder!)
  tft.invertDisplay(true);

  // Ekranı jilet gibi siyaha boya
  tft.fillScreen(ST77XX_BLACK);
  delay(100);

  // Büyük ve kalın test yazısı
  tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  tft.setTextSize(3);
  tft.setCursor(30, 40);
  tft.print("ST7789 TEST");

  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(30, 90);
  tft.print("NETLESTI MI?");

  tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
  tft.setCursor(30, 130);
  tft.print("TAC-TERM v1.0");

  // Renkli test kutuları
  tft.fillRect(30, 170, 50, 40, ST77XX_RED);
  tft.fillRect(90, 170, 50, 40, ST77XX_GREEN);
  tft.fillRect(150, 170, 50, 40, ST77XX_BLUE);
  tft.fillRect(210, 170, 50, 40, ST77XX_WHITE);
}

void loop() {
  delay(1000);
}
