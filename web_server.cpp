/*
 * web_server.cpp — ESP Web Server
 *
 * FIXES APPLIED:
 *  1. parseStatus() — positional split of @STATUS:ON:75:AUTO#
 *  2. buildSettingsJson() — keys now match HTML element IDs exactly
 *  3. buildTimerJson() — keyed object {"slot1":{...},...} with "on"/"off" keys
 *  4. GET endpoints added for all pages
 *  5. [NEW] EEPROM persistence — all user-set values saved to EEPROM and
 *     restored on power-up so pages always show last values set by user
 *  6. [NEW] Mode toggle endpoints now return {"mode":"..."} JSON so the
 *     dashboard JS can update buttons/mode text WITHOUT touching water level,
 *     fixing the spurious 100% level jump on mode switch
 */

#include <Arduino.h>

#if defined(ESP8266)
#include <ESP8266WebServer.h>
static ESP8266WebServer server(80);
#else
#include <WebServer.h>
static WebServer server(80);
#endif

#include <EEPROM.h>

#include "web_server.h"
#include "timer_mode.h"
#include "settings_mode.h"
#include "countdown_mode.h"
#include "twist_mode.h"
#include "auto_mode.h"
#include "esp_uart_comm.h"

/* Forward declarations */
void esp_uart_send(const char *message);
void esp_uart_autoStatusRequest(void);
void esp_uart_sendSettings(
  int dryRun, int testingGap, int maxRun, int retry,
  int lowVolt, int highVolt, int overLoad, int underLoad,
  int powerRestore,
  int dryRun_en, int testing_en, int maxRun_en, int retry_en,
  int lowVolt_en, int highVolt_en, int overLoad_en, int underLoad_en,
  int buzzerEnable, int buzzerFull, int buzzerEmpty);
bool esp_uart_receive(char *buffer, size_t bufferSize, unsigned long timeoutMs);
void esp_uart_processCommand(const char *cmd);

extern const char *htmlContent;

#ifndef MOTOR_PIN
#define MOTOR_PIN 2
#endif


/* ======================================================
   GLOBAL STATE
====================================================== */

String g_liveMode = "STANDBY";
String g_motorStatus = "OFF";
int g_liveLevel = 0;
int g_waterLevel = 0;


/* ================= SETTINGS CACHE ================= */

struct SettingsCache {
  int dryRunGap = 5;
  int testingGap = 30;
  int maxRun = 120;
  int retryCount = 3;
  int lowVolt = 180;
  int highVolt = 260;
  int overLoad = 0;
  int underLoad = 0;
  int powerRestore = 0;
  int dryRunEn = 1;
  int testingEn = 1;
  int maxRunEn = 1;
  int retryEn = 1;
  int lowVoltEn = 1;
  int highVoltEn = 1;
  int overLoadEn = 0;
  int underLoadEn = 0;
  int buzzerPump = 1;
  int buzzerFull = 1;
  int buzzerEmpty = 1;
} g_settings;


/* ================= TIMER CACHE ================= */

struct TimerSlot {
  bool enabled = false;
  String days = "";
  String onTime = "06:00";
  String offTime = "08:00";
  int gap = 0;
};
TimerSlot g_timer[5];


/* ================= COUNTDOWN CACHE ================= */

struct CountdownCache {
  bool active = false;
  int duration = 600;
  int remaining = 0;
} g_countdown;


/* ================= TWIST CACHE ================= */

struct TwistCache {
  bool active = false;
  int onDuration = 5;
  int offDuration = 5;
  String onTime = "06:00";
  String offTime = "18:00";
  String days = "Mon,Tue,Wed,Thu,Fri,Sat,Sun";
} g_twist;


/* ======================================================
   EEPROM PERSISTENCE
   All user-configurable values are stored in EEPROM so
   they survive power cycles. Strings are stored as fixed
   char arrays. A magic number detects a blank/corrupt
   EEPROM and falls back to firmware defaults.
====================================================== */

#define EEPROM_MAGIC 0xBEEFC0DE
#define EEPROM_SIZE 512

struct EEPROMData {
  uint32_t magic;

  /* Settings */
  int dryRunGap, testingGap, maxRun, retryCount;
  int lowVolt, highVolt, overLoad, underLoad, powerRestore;
  int dryRunEn, testingEn, maxRunEn, retryEn;
  int lowVoltEn, highVoltEn, overLoadEn, underLoadEn;
  int buzzerPump, buzzerFull, buzzerEmpty;

  /* Timer slots */
  struct {
    uint8_t enabled;
    char days[32];
    char onTime[6];
    char offTime[6];
    int gap;
  } timer[5];

  /* Countdown — only config, active state resets on boot */
  int countdownDuration;

  /* Twist — only config, active state resets on boot */
  int twistOnDuration;
  int twistOffDuration;
  char twistOnTime[6];
  char twistOffTime[6];
  char twistDays[64];
};

static void saveToEEPROM() {
  EEPROMData d;
  memset(&d, 0, sizeof(d));

  d.magic = EEPROM_MAGIC;

  /* Settings */
  d.dryRunGap = g_settings.dryRunGap;
  d.testingGap = g_settings.testingGap;
  d.maxRun = g_settings.maxRun;
  d.retryCount = g_settings.retryCount;
  d.lowVolt = g_settings.lowVolt;
  d.highVolt = g_settings.highVolt;
  d.overLoad = g_settings.overLoad;
  d.underLoad = g_settings.underLoad;
  d.powerRestore = g_settings.powerRestore;
  d.dryRunEn = g_settings.dryRunEn;
  d.testingEn = g_settings.testingEn;
  d.maxRunEn = g_settings.maxRunEn;
  d.retryEn = g_settings.retryEn;
  d.lowVoltEn = g_settings.lowVoltEn;
  d.highVoltEn = g_settings.highVoltEn;
  d.overLoadEn = g_settings.overLoadEn;
  d.underLoadEn = g_settings.underLoadEn;
  d.buzzerPump = g_settings.buzzerPump;
  d.buzzerFull = g_settings.buzzerFull;
  d.buzzerEmpty = g_settings.buzzerEmpty;

  /* Timer */
  for (int i = 0; i < 5; i++) {
    d.timer[i].enabled = g_timer[i].enabled ? 1 : 0;
    strncpy(d.timer[i].days, g_timer[i].days.c_str(), 31);
    d.timer[i].days[31] = 0;
    strncpy(d.timer[i].onTime, g_timer[i].onTime.c_str(), 5);
    d.timer[i].onTime[5] = 0;
    strncpy(d.timer[i].offTime, g_timer[i].offTime.c_str(), 5);
    d.timer[i].offTime[5] = 0;
    d.timer[i].gap = g_timer[i].gap;
  }

  /* Countdown config */
  d.countdownDuration = g_countdown.duration;

  /* Twist config */
  d.twistOnDuration = g_twist.onDuration;
  d.twistOffDuration = g_twist.offDuration;
  strncpy(d.twistOnTime, g_twist.onTime.c_str(), 5);
  d.twistOnTime[5] = 0;
  strncpy(d.twistOffTime, g_twist.offTime.c_str(), 5);
  d.twistOffTime[5] = 0;
  strncpy(d.twistDays, g_twist.days.c_str(), 63);
  d.twistDays[63] = 0;

  EEPROM.put(0, d);
  EEPROM.commit();
  Serial.println("💾 EEPROM saved");
}

static void loadFromEEPROM() {
  EEPROMData d;
  EEPROM.get(0, d);

  if (d.magic != EEPROM_MAGIC) {
    Serial.println("⚠️  EEPROM: no valid data, using firmware defaults");
    return;
  }

  /* Settings */
  g_settings.dryRunGap = d.dryRunGap;
  g_settings.testingGap = d.testingGap;
  g_settings.maxRun = d.maxRun;
  g_settings.retryCount = d.retryCount;
  g_settings.lowVolt = d.lowVolt;
  g_settings.highVolt = d.highVolt;
  g_settings.overLoad = d.overLoad;
  g_settings.underLoad = d.underLoad;
  g_settings.powerRestore = d.powerRestore;
  g_settings.dryRunEn = d.dryRunEn;
  g_settings.testingEn = d.testingEn;
  g_settings.maxRunEn = d.maxRunEn;
  g_settings.retryEn = d.retryEn;
  g_settings.lowVoltEn = d.lowVoltEn;
  g_settings.highVoltEn = d.highVoltEn;
  g_settings.overLoadEn = d.overLoadEn;
  g_settings.underLoadEn = d.underLoadEn;
  g_settings.buzzerPump = d.buzzerPump;
  g_settings.buzzerFull = d.buzzerFull;
  g_settings.buzzerEmpty = d.buzzerEmpty;

  /* Timer */
  for (int i = 0; i < 5; i++) {
    g_timer[i].enabled = (d.timer[i].enabled != 0);
    g_timer[i].days = String(d.timer[i].days);
    g_timer[i].onTime = String(d.timer[i].onTime);
    g_timer[i].offTime = String(d.timer[i].offTime);
    g_timer[i].gap = d.timer[i].gap;
  }

  /* Countdown config — never restore active state */
  g_countdown.duration = d.countdownDuration;
  g_countdown.active = false;
  g_countdown.remaining = 0;

  /* Twist config — never restore active state */
  g_twist.onDuration = d.twistOnDuration;
  g_twist.offDuration = d.twistOffDuration;
  g_twist.onTime = String(d.twistOnTime);
  g_twist.offTime = String(d.twistOffTime);
  g_twist.days = String(d.twistDays);
  g_twist.active = false;

  Serial.println("✅ EEPROM loaded — user settings restored");
}


/* ======================================================
   HELPERS
====================================================== */

void sendPacket(String pkt) {
  if (!pkt.startsWith("@")) pkt = "@" + pkt;
  if (!pkt.endsWith("#")) pkt += "#";
  esp_uart_send(pkt.c_str());
}

void requestStatus() {
  sendPacket("@STATUS#");
}

void setMode(String newMode) {
  if (g_liveMode == newMode) {
    sendPacket("@" + newMode + ":OFF#");
    g_liveMode = "STANDBY";
  } else {
    if (g_liveMode != "STANDBY" && g_liveMode != "OFFMODE")
      sendPacket("@" + g_liveMode + ":OFF#");
    sendPacket("@" + newMode + ":ON#");
    g_liveMode = newMode;
  }
}


/* ======================================================
   STATUS PARSER
   STM32 packet format: @STATUS:ON:75:AUTO#
====================================================== */
void parseStatus(String pkt) {
  if (pkt.startsWith("@STATUS:"))
    pkt.remove(0, 8);

  int hashPos = pkt.lastIndexOf('#');
  if (hashPos != -1) pkt.remove(hashPos);

  pkt.trim();

  int colon1 = pkt.indexOf(':');
  if (colon1 == -1) return;

  int colon2 = pkt.indexOf(':', colon1 + 1);
  if (colon2 == -1) return;

  g_motorStatus = pkt.substring(0, colon1);

  int level = pkt.substring(colon1 + 1, colon2).toInt();
  g_liveLevel = level;
  g_waterLevel = level;

  g_liveMode = pkt.substring(colon2 + 1);
}


/* ======================================================
   SETTINGS PARSER
====================================================== */

void parseSettings(String data) {
  int start = 0;
  while (start < (int)data.length()) {
    int semi = data.indexOf(';', start);
    String token;
    if (semi == -1) {
      token = data.substring(start);
      start = data.length();
    } else {
      token = data.substring(start, semi);
      start = semi + 1;
    }

    int eq = token.indexOf('=');
    if (eq == -1) continue;

    String key = token.substring(0, eq);
    int v = token.substring(eq + 1).toInt();

    if (key == "D" || key == "dryRunGap") g_settings.dryRunGap = v;
    else if (key == "T" || key == "testingGap") g_settings.testingGap = v;
    else if (key == "M" || key == "maxRun") g_settings.maxRun = v;
    else if (key == "RC" || key == "retryCount") g_settings.retryCount = v;
    else if (key == "LV" || key == "lowVolt") g_settings.lowVolt = v;
    else if (key == "HV" || key == "highVolt") g_settings.highVolt = v;
    else if (key == "OL" || key == "overLoad") g_settings.overLoad = v;
    else if (key == "UL" || key == "underLoad") g_settings.underLoad = v;
    else if (key == "PR" || key == "powerRestore") g_settings.powerRestore = v;
    else if (key == "DE") g_settings.dryRunEn = v;
    else if (key == "TE") g_settings.testingEn = v;
    else if (key == "ME") g_settings.maxRunEn = v;
    else if (key == "RCE") g_settings.retryEn = v;
    else if (key == "LVE") g_settings.lowVoltEn = v;
    else if (key == "HVE") g_settings.highVoltEn = v;
    else if (key == "OLE") g_settings.overLoadEn = v;
    else if (key == "ULE") g_settings.underLoadEn = v;
    else if (key == "BZ") g_settings.buzzerPump = v;
    else if (key == "BF") g_settings.buzzerFull = v;
    else if (key == "BE") g_settings.buzzerEmpty = v;
  }
}


/* ======================================================
   JSON HELPERS
====================================================== */

static String jsonStr(const String &s) {
  return "\"" + s + "\"";
}

/* /get_status  and  /status */
static String buildStatusJson() {
  String j = "{";
  j += "\"motor\":" + jsonStr(g_motorStatus) + ",";
  j += "\"level\":" + String(g_liveLevel) + ",";
  j += "\"mode\":" + jsonStr(g_liveMode) + "}";
  return j;
}

static String buildSettingsJson() {
  String j = "{";
  j += "\"dryRunGap\":" + String(g_settings.dryRunGap) + ",";
  j += "\"testingGap\":" + String(g_settings.testingGap) + ",";
  j += "\"maxRun\":" + String(g_settings.maxRun) + ",";
  j += "\"retryCount\":" + String(g_settings.retryCount) + ",";
  j += "\"lowVolt\":" + String(g_settings.lowVolt) + ",";
  j += "\"highVolt\":" + String(g_settings.highVolt) + ",";
  j += "\"overLoad\":" + String(g_settings.overLoad) + ",";
  j += "\"underLoad\":" + String(g_settings.underLoad) + ",";
  j += "\"powerRestore\":" + String(g_settings.powerRestore) + ",";
  j += "\"dryRunGap_en\":" + String(g_settings.dryRunEn) + ",";
  j += "\"testingGap_en\":" + String(g_settings.testingEn) + ",";
  j += "\"maxRun_en\":" + String(g_settings.maxRunEn) + ",";
  j += "\"retryCount_en\":" + String(g_settings.retryEn) + ",";
  j += "\"lowVolt_en\":" + String(g_settings.lowVoltEn) + ",";
  j += "\"highVolt_en\":" + String(g_settings.highVoltEn) + ",";
  j += "\"overLoad_en\":" + String(g_settings.overLoadEn) + ",";
  j += "\"underLoad_en\":" + String(g_settings.underLoadEn) + ",";
  j += "\"powerRestore_en\":1,";
  j += "\"buzzerEnable\":" + String(g_settings.buzzerPump) + ",";
  j += "\"buzzerTankFull\":" + String(g_settings.buzzerFull) + ",";
  j += "\"buzzerTankEmpty\":" + String(g_settings.buzzerEmpty) + ",";
  j += "\"buzzerMotorRunning\":" + String(g_settings.buzzerPump) + "}";
  return j;
}

static String buildTimerJson() {
  String j = "{";
  for (int i = 0; i < 5; i++) {
    if (i) j += ",";
    j += "\"slot" + String(i + 1) + "\":{";
    j += "\"enabled\":" + String(g_timer[i].enabled ? 1 : 0) + ",";
    j += "\"days\":" + jsonStr(g_timer[i].days) + ",";
    j += "\"on\":" + jsonStr(g_timer[i].onTime) + ",";
    j += "\"off\":" + jsonStr(g_timer[i].offTime) + "}";
  }
  j += "}";
  return j;
}

static String buildCountdownJson() {
  String j = "{";
  j += "\"active\":" + String(g_countdown.active ? 1 : 0) + ",";
  j += "\"duration\":" + String(g_countdown.duration) + ",";
  j += "\"remaining\":" + String(g_countdown.remaining) + "}";
  return j;
}

static String buildTwistJson() {
  String j = "{";
  j += "\"active\":" + String(g_twist.active ? 1 : 0) + ",";
  j += "\"onDuration\":" + String(g_twist.onDuration) + ",";
  j += "\"offDuration\":" + String(g_twist.offDuration) + ",";
  j += "\"onTime\":" + jsonStr(g_twist.onTime) + ",";
  j += "\"offTime\":" + jsonStr(g_twist.offTime) + ",";
  j += "\"days\":" + jsonStr(g_twist.days) + "}";
  return j;
}

/* Helper: returns just {"mode":"..."} — used by toggle endpoints so the
   dashboard JS can update buttons without disturbing the water level. */
static String buildModeJson() {
  return "{\"mode\":" + jsonStr(g_liveMode) + "}";
}

static void setCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-cache");
}


/* ======================================================
   WEB SERVER
====================================================== */

void start_webserver() {
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);

  /* ── EEPROM: initialise and restore user values ── */
  EEPROM.begin(EEPROM_SIZE);
  loadFromEEPROM();

  /* ── Main dashboard ── */
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", htmlContent);
  });

  /* ── STATUS (live poll) ── */
  server.on("/status", HTTP_GET, []() {
    setCORSHeaders();
    server.send(200, "application/json", buildStatusJson());
  });

  /* ── GET endpoints for page pre-population ── */
  server.on("/get_status", HTTP_GET, []() {
    setCORSHeaders();
    server.send(200, "application/json", buildStatusJson());
  });

  server.on("/get_settings", HTTP_GET, []() {
    setCORSHeaders();
    server.send(200, "application/json", buildSettingsJson());
  });

  server.on("/get_timer", HTTP_GET, []() {
    setCORSHeaders();
    server.send(200, "application/json", buildTimerJson());
  });

  server.on("/get_countdown", HTTP_GET, []() {
    setCORSHeaders();
    server.send(200, "application/json", buildCountdownJson());
  });

  server.on("/get_twist", HTTP_GET, []() {
    setCORSHeaders();
    server.send(200, "application/json", buildTwistJson());
  });

  /* ── MODE TOGGLES ──
     FIX: return {"mode":"..."} JSON instead of plain "OK" so the
     dashboard JS can update buttons and mode text without re-fetching
     the full status (which was causing the spurious 100% level jump).
  ── */
  server.on("/auto_toggle", HTTP_GET, []() {
    setCORSHeaders();
    setMode("AUTO");
    server.send(200, "application/json", buildModeJson());
  });

  server.on("/manual_toggle", HTTP_GET, []() {
    setCORSHeaders();
    setMode("MANUAL");
    server.send(200, "application/json", buildModeJson());
  });

  server.on("/semi_toggle", HTTP_GET, []() {
    setCORSHeaders();
    setMode("SEMIAUTO");
    server.send(200, "application/json", buildModeJson());
  });

  /* ── TIMER PAGE ── */
  server.on("/timer", HTTP_GET, []() {
    server.send(200, "text/html", timerModeHtml);
  });

  server.on("/timer/set", HTTP_GET, []() {
    bool anyEnabled = false;

    for (int i = 1; i <= 5; i++) {
      String slotKey = "slot" + String(i);
      if (!server.hasArg(slotKey)) continue;

      int idx = i - 1;
      g_timer[idx].enabled = server.arg(slotKey).toInt();
      g_timer[idx].days = server.arg("days" + String(i));
      g_timer[idx].onTime = server.arg("on" + String(i));
      g_timer[idx].offTime = server.arg("off" + String(i));

      int onH = g_timer[idx].onTime.substring(0, 2).toInt();
      int onM = g_timer[idx].onTime.substring(3, 5).toInt();
      int offH = g_timer[idx].offTime.substring(0, 2).toInt();
      int offM = g_timer[idx].offTime.substring(3, 5).toInt();

      sendPacket("@TIMER:SET:" + String(i) + ":"
                 + g_timer[idx].days + ":"
                 + String(onH) + ":" + String(onM) + ":"
                 + String(offH) + ":" + String(offM) + ":"
                 + String(g_timer[idx].enabled ? 1 : 0) + "#");

      if (g_timer[idx].enabled) anyEnabled = true;
    }

    if (anyEnabled) setMode("TIMER");
    else setMode("STANDBY");

    saveToEEPROM(); /* persist timer settings */
    server.send(200, "text/plain", "Timer Updated");
  });

  /* ── COUNTDOWN PAGE ── */
  server.on("/countdown", HTTP_GET, []() {
    server.send(200, "text/html", countdownModeHtml);
  });

  server.on("/start_countdown", HTTP_GET, []() {
    int dur = server.arg("duration").toInt();
    if (dur < 60) dur = 60;
    if (dur > 10800) dur = 10800;

    g_countdown.duration = dur;
    g_countdown.remaining = dur;
    g_countdown.active = true;

    sendPacket("@COUNTDOWN:ON:" + String(dur) + "#");
    setMode("COUNTDOWN");

    saveToEEPROM(); /* persist countdown duration */
    server.send(200, "text/plain", "Countdown Started");
  });

  server.on("/countdown_stop", HTTP_GET, []() {
    g_countdown.active = false;
    g_countdown.remaining = 0;

    sendPacket("@COUNTDOWN:OFF#");
    setMode("STANDBY");

    /* Note: only duration (config) is saved; active=false is not persisted
       because countdown should never auto-resume after power cycle */
    saveToEEPROM();
    server.send(200, "text/plain", "Countdown Stopped");
  });

  /* ── TWIST PAGE ── */
  server.on("/twist", HTTP_GET, []() {
    server.send(200, "text/html", twistModeHtml);
  });

  /* ── MOTOR MANUAL ON/OFF TOGGLE ── */
  server.on("/motor_toggle", HTTP_GET, []() {
    setCORSHeaders();

    if (g_motorStatus == "ON") {
      sendPacket("@MANUAL:OFF#");
      g_motorStatus = "OFF";
    } else {
      sendPacket("@MANUAL:ON#");
      g_motorStatus = "ON";
    }

    String j = "{\"motor\":" + jsonStr(g_motorStatus) + "}";
    server.send(200, "application/json", j);
  });

  server.on("/twist_submit", HTTP_GET, []() {
    g_twist.onDuration = server.arg("onDuration").toInt();
    g_twist.offDuration = server.arg("offDuration").toInt();
    g_twist.onTime = server.arg("onTime");
    g_twist.offTime = server.arg("offTime");
    if (server.hasArg("days")) g_twist.days = server.arg("days");
    g_twist.active = true;

    int onH = g_twist.onTime.substring(0, 2).toInt();
    int onM = g_twist.onTime.substring(3, 5).toInt();
    int offH = g_twist.offTime.substring(0, 2).toInt();
    int offM = g_twist.offTime.substring(3, 5).toInt();

    sendPacket("@TWIST:SET:"
               + String(g_twist.onDuration) + ":"
               + String(g_twist.offDuration) + ":"
               + String(onH) + ":" + String(onM) + ":"
               + String(offH) + ":" + String(offM) + "#");

    setMode("TWIST");
    saveToEEPROM(); /* persist twist settings */
    server.send(200, "text/plain", "Twist Updated");
  });

  server.on("/twist_stop", HTTP_GET, []() {
    g_twist.active = false;
    sendPacket("@TWIST:OFF#");
    setMode("STANDBY");
    saveToEEPROM();
    server.send(200, "text/plain", "Twist Stopped");
  });

  /* ── SETTINGS PAGE ── */
  server.on("/settings", HTTP_GET, []() {
    server.send(200, "text/html", settingsModeHtml);
  });

  server.on("/settings/set", HTTP_GET, []() {
    String data = server.arg("data");
    parseSettings(data);

    esp_uart_sendSettings(
      g_settings.dryRunGap,
      g_settings.testingGap,
      g_settings.maxRun,
      g_settings.retryCount,
      g_settings.lowVolt,
      g_settings.highVolt,
      g_settings.overLoad,
      g_settings.underLoad,
      g_settings.powerRestore,
      g_settings.dryRunEn,
      g_settings.testingEn,
      g_settings.maxRunEn,
      g_settings.retryEn,
      g_settings.lowVoltEn,
      g_settings.highVoltEn,
      g_settings.overLoadEn,
      g_settings.underLoadEn,
      g_settings.buzzerPump,
      g_settings.buzzerFull,
      g_settings.buzzerEmpty);

    saveToEEPROM(); /* persist settings */
    server.send(200, "text/plain", "Settings Updated");
  });

  server.begin();
  Serial.println("✅ Web server started — all endpoints active");
}


/* ======================================================
   MAIN LOOP HANDLER
====================================================== */

void handleClient() {
  server.handleClient();
  esp_uart_autoStatusRequest();

  char rxBuf[256];
  if (esp_uart_receive(rxBuf, sizeof(rxBuf), 10)) {
    String pkt = String(rxBuf);
    pkt.trim();

    if (pkt.startsWith("@STATUS:")) {
      parseStatus(pkt);
    } else if (pkt.startsWith("@SOK#")) {
      Serial.println("✅ STM32 settings saved");
    } else if (pkt.startsWith("@COUNTDOWN:DONE") || pkt == "@COUNTDOWN_OFF#") {
      g_countdown.active = false;
      g_countdown.remaining = 0;
    } else {
      esp_uart_processCommand(rxBuf);
    }
  }
}