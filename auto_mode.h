#pragma once
#ifndef AUTO_MODE_H
#define AUTO_MODE_H
#include <Arduino.h>
#include "esp_uart_comm.h"

enum AutoModeState {
    AUTO_STATE_OFF = 0,
    AUTO_STATE_ON  = 1
};
static AutoModeState g_autoState = AUTO_STATE_OFF;
static String g_autoStatusMsg = "AUTO:OFF";

static inline void autoMode_start() {
    g_autoState = AUTO_STATE_ON;
    g_autoStatusMsg = "AUTO:ON";
    esp_uart_send("@AUTO:ON#");   
    Serial.println("[AUTO] Sent @AUTO:ON#");
}
static inline void autoMode_stop() {
    g_autoState = AUTO_STATE_OFF;
    g_autoStatusMsg = "AUTO:OFF";

    esp_uart_send("@AUTO:OFF#");
    Serial.println("[AUTO] Sent @AUTO:OFF#");
}

static inline void autoMode_toggle() {
    if (g_autoState == AUTO_STATE_ON) {
        autoMode_stop();
    } else {
        autoMode_start();
    }
}

static inline void autoMode_applyStatusFromSTM32(const String &modeString) {
    if (modeString == "AUTO") {
        g_autoState = AUTO_STATE_ON;
        g_autoStatusMsg = "AUTO:ON";
    } else {
        g_autoState = AUTO_STATE_OFF;
        g_autoStatusMsg = "AUTO:OFF";
    }
}

static inline String autoMode_getStatusText() {
    return g_autoStatusMsg;
}
static const char* autoModeHtml = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Auto Mode</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0"/>
  <style>
    body {
      font-family: 'Segoe UI', sans-serif;
      background: #111;
      color: #fff;
      text-align: center;
      padding: 20px;
    }
    h1 { color: #00e676; margin-bottom: 20px; }
    .statusBox {
      margin: 20px auto;
      padding: 20px;
      background: #1e1e1e;
      border-radius: 10px;
      width: 80%;
      font-size: 20px;
      border: 2px solid #00e676;
    }
    .btn {
      padding: 15px 25px;
      margin-top: 20px;
      font-size: 18px;
      border: none;
      border-radius: 10px;
      cursor: pointer;
      background: linear-gradient(135deg,#00e676,#00c853);
      color: black;
      font-weight: bold;
    }
    .btnOff {
      background: linear-gradient(135deg,#ff1744,#d50000);
      color:white;
    }
  </style>
</head>
<body>
  <h1>AUTO MODE CONTROL</h1>
  <div id="stateBox" class="statusBox">Loading...</div>
  <button id="toggleBtn" class="btn" onclick="toggleAuto()">Toggle Auto Mode</button>
  <br><br>
  <button class="btnOff" onclick="window.location.href='/'">BACK</button>
<script>
async function refreshStatus() {
  const res = await fetch('/status');
  const data = await res.json();
  document.getElementById("stateBox").innerHTML =
    "Motor: " + data.motor + "<br>" +
    "Level: " + data.level + "%<br>" +
    "Mode: " + data.mode;
}
async function toggleAuto() {
  await fetch('/auto_toggle');
  setTimeout(refreshStatus, 500);
}
setInterval(refreshStatus, 1000);
refreshStatus();
</script>
</body>
</html>
)rawliteral";
#endif 
