/*
 * esp_uart_comm.cpp — ESP ↔ STM32 UART Communication
 *
 * FIXES:
 *  1. Status packet parsed positionally (@STATUS:ON:75:AUTO#)
 *     instead of searching for labels ("MOTOR:" etc.) that don't exist
 *  2. g_waterLevel now correctly stores the raw % from STM32
 */

#include "esp_uart_comm.h"
#include "auto_mode.h"
#include <Arduino.h>
#include <string.h>

extern String g_motorStatus;   // defined in web_server.cpp
extern int    g_waterLevel;    // defined in web_server.cpp (alias of g_liveLevel)
extern String g_liveMode;      // defined in web_server.cpp

static char s_rxBuffer[ESP_UART_RX_BUFFER_SIZE];
static int  s_rxBufferIndex = 0;
static unsigned long s_lastStatusRequest = 0;

#if ESP_UART_ENABLE_DEBUG
  #define DBG_PRINT(...)     Serial.printf(__VA_ARGS__)
  #define DBG_PRINTLN(...)   Serial.println(__VA_ARGS__)
#else
  #define DBG_PRINT(...)
  #define DBG_PRINTLN(...)
#endif


/* =====================================================
   UART INIT
===================================================== */

void esp_uart_init() {
  Serial.begin(ESP_UART_BAUD_RATE);
  Serial.setTimeout(10);

  memset(s_rxBuffer, 0, sizeof(s_rxBuffer));
  s_rxBufferIndex = 0;
  s_lastStatusRequest = millis();

  DBG_PRINTLN("UART Init OK");
}


/* =====================================================
   UART SEND
===================================================== */

void esp_uart_send(const char *message) {
  if (!message || !*message) return;

  String pkt = message;
  pkt.trim();

  if (!pkt.startsWith("@")) pkt = "@" + pkt;
  if (!pkt.endsWith("#"))   pkt += "#";

  pkt += "\r\n";

  Serial.write(pkt.c_str(), pkt.length());
  Serial.flush();

#if ESP_UART_ENABLE_DEBUG
  Serial.print("→ SENT: ");
  Serial.println(pkt);
#endif
}


/* =====================================================
   SETTINGS SENDER
===================================================== */

void esp_uart_sendSettings(
  int dryRun,
  int testingGap,
  int maxRun,
  int retry,
  int lowVolt,
  int highVolt,
  int overLoad,
  int underLoad,
  int powerRestore,
  int dryRun_en,
  int testing_en,
  int maxRun_en,
  int retry_en,
  int lowVolt_en,
  int highVolt_en,
  int overLoad_en,
  int underLoad_en,
  int buzzerEnable,
  int buzzerFull,
  int buzzerEmpty)
{
  char pkt[220];
  snprintf(pkt, sizeof(pkt),
    "@SET:D=%d;T=%d;M=%d;RC=%d;LV=%d;HV=%d;OL=%d;UL=%d;PR=%d;"
    "DE=%d;TE=%d;ME=%d;RCE=%d;LVE=%d;HVE=%d;OLE=%d;ULE=%d;"
    "BZ=%d;BF=%d;BE=%d#",
    dryRun, testingGap, maxRun, retry,
    lowVolt, highVolt, overLoad, underLoad, powerRestore,
    dryRun_en, testing_en, maxRun_en, retry_en,
    lowVolt_en, highVolt_en, overLoad_en, underLoad_en,
    buzzerEnable, buzzerFull, buzzerEmpty);

  esp_uart_send(pkt);
}


/* =====================================================
   UART RECEIVE
===================================================== */

bool esp_uart_receive(char *buffer, size_t bufferSize, unsigned long timeoutMs) {
  unsigned long start = millis();
  bool packetReceived = false;

  auto drainSerialOnce = [&]() -> bool {
    while (Serial.available()) {
      char c = Serial.read();

      if (c == '\r' || c == '\n') continue;
      if ((uint8_t)c < 32 && c != '@' && c != '#') continue;

      if (c == '@') {
        s_rxBufferIndex = 0;
        memset(s_rxBuffer, 0, sizeof(s_rxBuffer));
      }

      if (s_rxBufferIndex < ESP_UART_RX_BUFFER_SIZE - 1)
        s_rxBuffer[s_rxBufferIndex++] = c;

      if (c == '#') {
        s_rxBuffer[s_rxBufferIndex] = '\0';
        strncpy(buffer, s_rxBuffer, bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
        packetReceived = true;
        s_rxBufferIndex = 0;
        memset(s_rxBuffer, 0, sizeof(s_rxBuffer));
        return true;
      }
    }
    return false;
  };

  if (timeoutMs == 0) {
    drainSerialOnce();
    return packetReceived;
  }

  while (millis() - start < timeoutMs) {
    if (drainSerialOnce()) return true;
    delay(1);
  }

  drainSerialOnce();
  return packetReceived;
}


/* =====================================================
   STATUS REQUEST
===================================================== */

void esp_uart_requestStatus() {
  esp_uart_send("STATUS");
  s_lastStatusRequest = millis();
  DBG_PRINTLN("Requested @STATUS#");
}

void esp_uart_autoStatusRequest() {
  unsigned long now = millis();
  unsigned long interval =
      (g_motorStatus == "ON") ? 10000UL : (10UL * 60UL * 1000UL);

  if (now - s_lastStatusRequest >= interval) {
    esp_uart_requestStatus();
    s_lastStatusRequest = now;
  }
}

extern int g_liveLevel;

static void parseStatusFields(const String &body)
{
  // body = "ON:75:AUTO"
  int colon1 = body.indexOf(':');
  if (colon1 == -1) return;

  int colon2 = body.indexOf(':', colon1 + 1);
  if (colon2 == -1) return;

  g_motorStatus = body.substring(0, colon1);

  int level = body.substring(colon1 + 1, colon2).toInt();

  g_waterLevel = level;   // internal alias
  g_liveLevel  = level;   // used by web server

  g_liveMode = body.substring(colon2 + 1);

  autoMode_applyStatusFromSTM32(g_liveMode);

  Serial.printf("📶 STATUS → Motor:%s | Level:%d%% | Mode:%s\n",
                g_motorStatus.c_str(),
                level,
                g_liveMode.c_str());
}

/* =====================================================
   COMMAND PROCESSOR
===================================================== */

void esp_uart_processCommand(const char *cmd) {
  if (!cmd) return;

  /* ── STATUS ── */
  if (strncmp(cmd, "@STATUS:", 8) == 0) {
    // Strip leading "@STATUS:" and trailing "#"
    String body = String(cmd + 8);
    int hashPos = body.lastIndexOf('#');
    if (hashPos != -1) body.remove(hashPos);
    body.trim();
    parseStatusFields(body);
    return;
  }

  /* ── SETTINGS ACK ── */
  if (strstr(cmd, "@SOK#")) {
    Serial.println("✅ STM32 SETTINGS SAVED");
    return;
  }

  /* ── LEGACY ACK ── */
  if (strncmp(cmd, "@ACK:", 5) == 0) {
    Serial.print("ACK → ");
    Serial.println(cmd);
    return;
  }

  /* ── ERROR ── */
  if (strncmp(cmd, "@ERR:", 5) == 0) {
    Serial.print("ERR → ");
    Serial.println(cmd);
    return;
  }

  /* ── PONG ── */
  if (strstr(cmd, "@PONG#")) {
    DBG_PRINTLN("PONG received");
    return;
  }

  Serial.print("❓ Unknown packet: ");
  Serial.println(cmd);
}