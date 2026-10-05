#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== TBS M10Q GPS Passthrough Monitor ===");
  Serial.println("Pinler: RX2 = GPIO 16 (GPS TX) | TX2 = GPIO 17 (GPS RX)");
  Serial.println("Baud: 115200");
  Serial.println("NMEA veri akisi bekleniyor...\n");

  Serial2.begin(115200, SERIAL_8N1, 16, 17);
}

void loop() {
  while (Serial2.available()) {
    Serial.write(Serial2.read());
  }
}
