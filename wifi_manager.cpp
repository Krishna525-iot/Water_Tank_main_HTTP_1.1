#include <ESP8266WiFi.h>
#include "wifi_manager.h"

const char* AP_PASS = "123456789";
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);

// Generated at runtime from MAC — e.g. "helonix_A1B2C3"
char AP_SSID[32];

void wifi_init() {
  // Build SSID from last 3 bytes of MAC → "helonix_AABBCC"
  uint8_t mac[6];
  WiFi.softAPmacAddress(mac);
  snprintf(AP_SSID, sizeof(AP_SSID), "helonix_%02X%02X%02X",
           mac[3], mac[4], mac[5]);

  Serial.println("\n[WiFi] Setting up Access Point...");
  Serial.printf("[WiFi] SSID: %s\n", AP_SSID);

  WiFi.disconnect(true);
  delay(500);
  WiFi.mode(WIFI_AP);
  delay(200);

  WiFi.softAPConfig(apIP, apIP, netMsk);
  bool result = WiFi.softAP(AP_SSID, AP_PASS, 1, false, 4);

  if (result) {
    Serial.printf("[WiFi] AP started: %s\n", AP_SSID);
  } else {
    Serial.println("[WiFi] Failed to start AP!");
  }

  Serial.print("[WiFi] AP IP address: ");
  Serial.println(WiFi.softAPIP());

  wifi_set_sleep_type(LIGHT_SLEEP_T);
}

void wifi_loop() {
  static unsigned long lastCheck = 0;
  unsigned long now = millis();

  if (now - lastCheck > 10000) {
    lastCheck = now;
    if (WiFi.softAPgetStationNum() == 0 && WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] No clients connected. Reinitializing AP...");
      WiFi.softAPdisconnect(true);
      delay(500);
      WiFi.softAP(AP_SSID, AP_PASS); // reuses the same MAC-based SSID
      Serial.println("[WiFi] AP restarted.");
    }
  }

  yield();
}