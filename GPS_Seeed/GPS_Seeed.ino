#include <TinyGPSPlus.h>

TinyGPSPlus gps;

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("Starting GPS Parsing...");

  // GPS UART: RX=GPIO9 (D10), TX=GPIO8 (D9)
  Serial1.begin(9600, SERIAL_8N1, 9, 8);
}

void loop() {

  // Read all available GPS data
  while (Serial1.available()) {
    gps.encode(Serial1.read());
  }

  // Only print when new location is received
  if (gps.location.isUpdated()) {
    Serial.print("Latitude:  ");
    Serial.println(gps.location.lat(), 6);

    Serial.print("Longitude: ");
    Serial.println(gps.location.lng(), 6);
  }

  // Print altitude if updated
  if (gps.altitude.isUpdated()) {
    Serial.print("Altitude:  ");
    Serial.print(gps.altitude.meters());
    Serial.println(" m");
  }

  // Satellite count
  if (gps.satellites.isUpdated()) {
    Serial.print("Satellites: ");
    Serial.println(gps.satellites.value());
  }

  // Fix quality
  if (gps.hdop.isUpdated()) {
    Serial.print("HDOP: ");
    Serial.println(gps.hdop.hdop());
  }
}
