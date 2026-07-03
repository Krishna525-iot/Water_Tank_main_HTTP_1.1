#include "wifi_manager.h"
#include "web_server.h"
#include "esp_uart_comm.h"

void setup() {
  Serial.begin(115200);
  wifi_init();
  esp_uart_init();
  start_webserver();
}

void loop() {
  handleClient();  
}
