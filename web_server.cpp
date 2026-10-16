/*
 * web_server.cpp — ESP Web Server
 *
 * FIXES APPLIED:
 *  1. parseStatus() — positional split of @STATUS:ON:75:AUTO#
 *  2. buildSettingsJson() — keys now match HTML element IDs exactly
 *  3. buildTimerJson() — keyed object {"slot1":{...},...} with "on"/"off" keys
 *  4. GET endpoints added for all pages
 *  5. EEPROM persistence of the last known values (fallback only)
 *  6. Mode toggle endpoints return {"mode":"..."} JSON
 *  7. [NEW] Pages show the DEVICE's current values: every /get_* endpoint
 *     first asks the STM32 (@SETTINGS:GET#, @TINFO#, @TWIST:GET#,
 *     @COUNTDOWN:GET#) and fills the cache from its reply. Before, the
 *     cache held ESP defaults, and "Apply" pushed them onto the device.
 *  8. [NEW] Timer: disabled slots are sent too (unchecking now sticks);
 *     twist/countdown/timer no longer send extra "<mode>:OFF/ON" pairs
 *     after their own command; countdown sent in minutes; factory reset
 *     and timer stop endpoints added.
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

#define DEVICE_REPLY_TIMEOUT_MS 800UL


/* ======================================================
   GLOBAL STATE
====================================================== */

String g_liveMode = "STANDBY";
String g_motorStatus = "OFF";
int g_liveLevel = 0;
int g_waterLevel = 0;

/* Live data from @LIVE (every second): voltage, current, G.W, dry, time */
struct LiveCache {
  int volts = 0;
  String amps = "0.0";
  int gw = 0;
  int dryWater = 0;
  String time = "--:--:--";
  long runS = -1;           /* motor run seconds, -1 = motor off (N-03) */
  String fault = "";        /* device stop reason, "" = none (N-02) */
  unsigned long at = 0;     /* millis() of the last packet, 0 = none yet */
} g_live;


/* ================= SETTINGS CACHE =================
   Defaults = STM32 factory defaults; replaced by the device's values
   as soon as it answers. */

struct SettingsCache {
  int dryRunGap = 2;
  int testingGap = 30;
  int maxRun = 150;
  int retryCount = 5;
  int lowVolt = 180;
  int highVolt = 280;
  int overLoad = 25;
  int underLoad = 0;
  int powerRestore = 1;     /* 0 = ON, 1 = OFF */
  int dryRunEn = 0;
  int testingEn = 1;
  int maxRunEn = 1;
  int retryEn = 1;
  int lowVoltEn = 1;
  int highVoltEn = 1;
  int overLoadEn = 1;
  int underLoadEn = 0;
  int buzzerPump = 0;
  int buzzerFull = 0;
  int buzzerEmpty = 0;
  int levelSource = 1;      /* 0 = wired probes, 1 = wireless transmitter (N-08) */
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
  int duration = 600;      /* seconds (page shows minutes) */
  int remaining = 0;       /* seconds */
  bool paused = false;     /* device paused it on a fault (CD-05/06) */
} g_countdown;


/* ================= TWIST CACHE ================= */

struct TwistCache {
  bool active = false;
  int onDuration = 5;      /* minutes */
  int offDuration = 5;     /* minutes */
  String onTime = "06:00";
  String offTime = "18:00";
  String days = "mon,tue,wed,thu,fri,sat,sun";
} g_twist;


/* ======================================================
   EEPROM PERSISTENCE
   Last known values, used only until the STM32 answers.
====================================================== */

#define EEPROM_MAGIC 0xBEEFC0DF   /* changed: layout of defaults changed */
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
}

static void loadFromEEPROM() {
  EEPROMData d;
  EEPROM.get(0, d);

  if (d.magic != EEPROM_MAGIC) return;   /* keep firmware defaults */

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
  g_countdown.paused = false;

  /* Twist config — never restore active state */
  g_twist.onDuration = d.twistOnDuration;
  g_twist.offDuration = d.twistOffDuration;
  g_twist.onTime = String(d.twistOnTime);
  g_twist.offTime = String(d.twistOffTime);
  g_twist.days = String(d.twistDays);
  g_twist.active = false;
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

/* Toggle a simple mode (AUTO / MANUAL / SEMIAUTO) on the STM32. Starting
 * a mode on the STM32 already stops the others, so only one packet is
 * needed; "<old>:OFF" first is not required. */
void setMode(String newMode) {
  if (g_liveMode == newMode) {
    sendPacket("@" + newMode + ":OFF#");
    g_liveMode = "STANDBY";
  } else {
    sendPacket("@" + newMode + ":ON#");
    g_liveMode = newMode;
  }
}

static String two(int v) {
  return (v < 10 ? "0" : "") + String(v);
}

/* Split "a:b:c" into up to maxParts fields */
static int splitColon(const String &s, String *out, int maxParts) {
  int n = 0, start = 0;
  while (n < maxParts) {
    int c = s.indexOf(':', start);
    if (c == -1) { out[n++] = s.substring(start); break; }
    out[n++] = s.substring(start, c);
    start = c + 1;
  }
  return n;
}

/* Strip "@PREFIX:" and trailing "#" */
static String packetBody(const String &pkt, const char *prefix) {
  String b = pkt.substring(strlen(prefix));
  int h = b.lastIndexOf('#');
  if (h != -1) b.remove(h);
  b.trim();
  return b;
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
  g_countdown.active = (g_liveMode == "COUNTDOWN");
  g_twist.active     = (g_liveMode == "TWIST");
}


/* ======================================================
   SETTINGS PARSER  (from the web page, "key=value;...")
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
   DEVICE REPLY PARSERS
====================================================== */

/* A value of 0 from the device means "disabled": keep the last
 * number so the user can simply switch it back on. */
static void takeLimit(int v, int &value, int &enable) {
  enable = (v > 0) ? 1 : 0;
  if (v > 0) value = v;
}

/* @SETTINGS_DATA:D=2;T=30;RC=5;M=150;LV=180;HV=280;OL=25;UL=0;PR=1;DE=0;BZ=0;BF=0;BE=0;CD=10# */
static void parseDeviceSettings(const String &body) {
  int start = 0;
  while (start < (int)body.length()) {
    int semi = body.indexOf(';', start);
    String token = (semi == -1) ? body.substring(start) : body.substring(start, semi);
    start = (semi == -1) ? body.length() : semi + 1;

    int eq = token.indexOf('=');
    if (eq == -1) continue;
    String key = token.substring(0, eq);
    int v = token.substring(eq + 1).toInt();

    if      (key == "D")  { if (v > 0) g_settings.dryRunGap = v; }
    else if (key == "DE") g_settings.dryRunEn = v ? 1 : 0;
    else if (key == "T")  { g_settings.testingGap = v; g_settings.testingEn = 1; }
    else if (key == "RC") takeLimit(v, g_settings.retryCount, g_settings.retryEn);
    else if (key == "M")  takeLimit(v, g_settings.maxRun,     g_settings.maxRunEn);
    else if (key == "LV") takeLimit(v, g_settings.lowVolt,    g_settings.lowVoltEn);
    else if (key == "HV") takeLimit(v, g_settings.highVolt,   g_settings.highVoltEn);
    else if (key == "OL") takeLimit(v, g_settings.overLoad,   g_settings.overLoadEn);
    else if (key == "UL") takeLimit(v, g_settings.underLoad,  g_settings.underLoadEn);
    else if (key == "PR") g_settings.powerRestore = (v == 1) ? 1 : 0;
    else if (key == "BZ") g_settings.buzzerPump  = v ? 1 : 0;
    else if (key == "BF") g_settings.buzzerFull  = v ? 1 : 0;
    else if (key == "BE") g_settings.buzzerEmpty = v ? 1 : 0;
    else if (key == "CD") { if (v > 0) g_countdown.duration = v * 60; }
    else if (key == "SRC") g_settings.levelSource = v ? 1 : 0;
  }
}

/* @TSLOT:1:06:00:08:00:0x7F:1#  (day mask bit0 = Mon .. bit6 = Sun) */
static void parseDeviceTimerSlot(const String &body) {
  static const char *DAY_NAMES[7] = { "mon","tue","wed","thu","fri","sat","sun" };
  String f[7];
  if (splitColon(body, f, 7) < 7) return;

  int n = f[0].toInt();
  if (n < 1 || n > 5) return;
  TimerSlot &t = g_timer[n - 1];

  t.onTime  = two(f[1].toInt()) + ":" + two(f[2].toInt());
  t.offTime = two(f[3].toInt()) + ":" + two(f[4].toInt());

  int mask = (int)strtol(f[5].c_str(), nullptr, 16);
  String days = "";
  for (int d = 0; d < 7; d++)
    if (mask & (1 << d)) { if (days.length()) days += ","; days += DAY_NAMES[d]; }
  t.days = days;
  t.enabled = (f[6].toInt() != 0);
}

/* @TWIST_DATA:<onMin>:<offMin>:HH:MM:HH:MM:<active># */
static void parseDeviceTwist(const String &body) {
  String f[7];
  if (splitColon(body, f, 7) < 7) return;
  if (f[0].toInt() > 0) g_twist.onDuration  = f[0].toInt();
  if (f[1].toInt() > 0) g_twist.offDuration = f[1].toInt();
  g_twist.onTime  = two(f[2].toInt()) + ":" + two(f[3].toInt());
  g_twist.offTime = two(f[4].toInt()) + ":" + two(f[5].toInt());
  g_twist.active  = (f[6].toInt() != 0);
}

/* @COUNTDOWN_DATA:<defaultMin>:<remainingSec>:<active>[:<paused>]# */
static void parseDeviceCountdown(const String &body) {
  String f[4];
  int n = splitColon(body, f, 4);
  if (n < 3) return;
  if (f[0].toInt() > 0) g_countdown.duration = f[0].toInt() * 60;
  g_countdown.remaining = f[1].toInt();
  g_countdown.active    = (f[2].toInt() != 0);
  g_countdown.paused    = (n >= 4) && (f[3].toInt() != 0);
}

/* @LIVE:<volts>:<amps>:<gw>:<dryWater>:<hh>:<mm>:<ss>:<run s>:<fault>#
   (run s is -1 while the motor is off; fault is the device's stop reason
   or CLOCK LOST, empty when none; older firmware sends neither) */
static void parseLive(const String &body) {
  String f[9];
  int n = splitColon(body, f, 9);
  if (n < 7) return;
  g_live.runS     = (n >= 8) ? f[7].toInt() : -1;
  g_live.fault    = (n >= 9) ? f[8] : "";
  g_live.volts    = f[0].toInt();
  g_live.amps     = f[1];
  g_live.gw       = f[2].toInt();
  g_live.dryWater = f[3].toInt();
  g_live.time     = f[4] + ":" + f[5] + ":" + f[6];
  g_live.at       = millis();
}

/* Route one packet from the STM32. Returns true if it was consumed. */
static bool handleDevicePacket(const String &pkt) {
  if (pkt.startsWith("@LIVE:"))           { parseLive(packetBody(pkt, "@LIVE:")); return true; }
  if (pkt.startsWith("@STATUS:"))         { parseStatus(pkt); return true; }
  if (pkt.startsWith("@SETTINGS_DATA:"))  { parseDeviceSettings(packetBody(pkt, "@SETTINGS_DATA:")); return true; }
  if (pkt.startsWith("@TSLOT:"))          { parseDeviceTimerSlot(packetBody(pkt, "@TSLOT:")); return true; }
  if (pkt.startsWith("@TWIST_DATA:"))     { parseDeviceTwist(packetBody(pkt, "@TWIST_DATA:")); return true; }
  if (pkt.startsWith("@COUNTDOWN_DATA:")) { parseDeviceCountdown(packetBody(pkt, "@COUNTDOWN_DATA:")); return true; }
  if (pkt == "@COUNTDOWN_OFF#" || pkt.startsWith("@COUNTDOWN:DONE")) {
    g_countdown.active = false;
    g_countdown.remaining = 0;
    g_countdown.paused = false;
    return true;
  }
  return false;
}

/* Send a request (none if cmd is empty - just wait for a reply to a
 * command already sent) until `expectCount` replies starting with
 * `expectPrefix` have arrived. Other packets are routed as usual. */
static bool requestFromDevice(const char *cmd, const char *expectPrefix, int expectCount) {
  if (cmd && *cmd) sendPacket(cmd);
  unsigned long start = millis();
  int got = 0;
  char buf[ESP_UART_RX_BUFFER_SIZE];

  while (millis() - start < DEVICE_REPLY_TIMEOUT_MS) {
    if (esp_uart_receive(buf, sizeof(buf), 20)) {
      String pkt = String(buf);
      pkt.trim();
      bool match = pkt.startsWith(expectPrefix);
      if (!handleDevicePacket(pkt)) esp_uart_processCommand(buf);
      if (match && ++got >= expectCount) return true;
    }
    yield();
  }
  return false;
}

static bool syncSettings()  { return requestFromDevice("@SETTINGS:GET#",  "@SETTINGS_DATA:",  1); }
static bool syncTimer()     { return requestFromDevice("@TINFO#",         "@TSLOT:",          5); }
static bool syncTwist()     { return requestFromDevice("@TWIST:GET#",     "@TWIST_DATA:",     1); }
static bool syncCountdown() { return requestFromDevice("@COUNTDOWN:GET#", "@COUNTDOWN_DATA:", 1); }


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

static String buildSettingsJson(bool fromDevice) {
  String j = "{";
  j += "\"fromDevice\":" + String(fromDevice ? 1 : 0) + ",";
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
  j += "\"maxRun_en\":" + String(g_settings.maxRunEn) + ",";
  j += "\"retryCount_en\":" + String(g_settings.retryEn) + ",";
  j += "\"lowVolt_en\":" + String(g_settings.lowVoltEn) + ",";
  j += "\"highVolt_en\":" + String(g_settings.highVoltEn) + ",";
  j += "\"overLoad_en\":" + String(g_settings.overLoadEn) + ",";
  j += "\"underLoad_en\":" + String(g_settings.underLoadEn) + ",";
  j += "\"buzzerPump\":" + String(g_settings.buzzerPump) + ",";
  j += "\"buzzerFull\":" + String(g_settings.buzzerFull) + ",";
  j += "\"buzzerEmpty\":" + String(g_settings.buzzerEmpty) + ",";
  j += "\"levelSource\":" + String(g_settings.levelSource) + "}";
  return j;
}

static String buildTimerJson(bool fromDevice) {
  String j = "{";
  j += "\"fromDevice\":" + String(fromDevice ? 1 : 0) + ",";
  j += "\"timerActive\":" + String(g_liveMode == "TIMER" ? 1 : 0);
  for (int i = 0; i < 5; i++) {
    j += ",\"slot" + String(i + 1) + "\":{";
    j += "\"enabled\":" + String(g_timer[i].enabled ? 1 : 0) + ",";
    j += "\"days\":" + jsonStr(g_timer[i].days) + ",";
    j += "\"on\":" + jsonStr(g_timer[i].onTime) + ",";
    j += "\"off\":" + jsonStr(g_timer[i].offTime) + "}";
  }
  j += "}";
  return j;
}

static String buildCountdownJson(bool fromDevice) {
  String j = "{";
  j += "\"fromDevice\":" + String(fromDevice ? 1 : 0) + ",";
  j += "\"active\":" + String(g_countdown.active ? 1 : 0) + ",";
  j += "\"duration\":" + String(g_countdown.duration) + ",";
  j += "\"remaining\":" + String(g_countdown.remaining) + ",";
  j += "\"paused\":" + String(g_countdown.paused ? 1 : 0) + "}";
  return j;
}

static String buildTwistJson(bool fromDevice) {
  String j = "{";
  j += "\"fromDevice\":" + String(fromDevice ? 1 : 0) + ",";
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

/* Pages must not come from the phone's cache: after a firmware update the
   browser kept showing the old page (16-10). */
static void noCachePage() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
}


/* ======================================================
   WEB SERVER
====================================================== */

void start_webserver() {
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);

  /* ── EEPROM: last known values until the STM32 answers ── */
  EEPROM.begin(EEPROM_SIZE);
  loadFromEEPROM();

  /* ── Main dashboard ── */
  server.on("/", HTTP_GET, []() {
    noCachePage();
    server.send(200, "text/html", htmlContent);
  });

  /* ── STATUS (live poll) ── */
  server.on("/status", HTTP_GET, []() {
    setCORSHeaders();
    server.send(200, "application/json", buildStatusJson());
  });

  /* Other data, refreshed every second by the dashboard (DEC-16) */
  server.on("/live", HTTP_GET, []() {
    setCORSHeaders();
    bool fresh = g_live.at != 0 && (millis() - g_live.at) < 5000UL;
    String j = "{";
    j += "\"fresh\":" + String(fresh ? 1 : 0) + ",";
    j += "\"volts\":" + String(g_live.volts) + ",";
    j += "\"amps\":" + jsonStr(g_live.amps) + ",";
    j += "\"gw\":" + String(g_live.gw) + ",";
    j += "\"dryWater\":" + String(g_live.dryWater) + ",";
    j += "\"time\":" + jsonStr(g_live.time) + ",";
    j += "\"run\":" + String(fresh ? g_live.runS : -1L) + ",";
    j += "\"fault\":" + jsonStr(fresh ? g_live.fault : String("")) + "}";
    server.send(200, "application/json", j);
  });

  /* ── GET endpoints: read the device first, then answer ── */
  server.on("/get_status", HTTP_GET, []() {
    setCORSHeaders();
    server.send(200, "application/json", buildStatusJson());
  });

  server.on("/get_settings", HTTP_GET, []() {
    setCORSHeaders();
    bool ok = syncSettings();
    if (ok) saveToEEPROM();
    server.send(200, "application/json", buildSettingsJson(ok));
  });

  server.on("/get_timer", HTTP_GET, []() {
    setCORSHeaders();
    bool ok = syncTimer();
    if (ok) saveToEEPROM();
    server.send(200, "application/json", buildTimerJson(ok));
  });

  server.on("/get_countdown", HTTP_GET, []() {
    setCORSHeaders();
    bool ok = syncCountdown();
    server.send(200, "application/json", buildCountdownJson(ok));
  });

  server.on("/get_twist", HTTP_GET, []() {
    setCORSHeaders();
    bool ok = syncTwist();
    if (ok) saveToEEPROM();
    server.send(200, "application/json", buildTwistJson(ok));
  });

  /* ── MODE TOGGLES ── */
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
    noCachePage();
    server.send(200, "text/html", timerModeHtml);
  });

  /* All five slots are sent - a slot switched off is sent with
     enabled=0, so unchecking it is stored on the device. */
  server.on("/timer/set", HTTP_GET, []() {
    bool anyEnabled = false;

    for (int i = 1; i <= 5; i++) {
      String slotKey = "slot" + String(i);
      if (!server.hasArg(slotKey)) continue;

      int idx = i - 1;
      g_timer[idx].enabled = server.arg(slotKey).toInt() != 0;
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
      delay(40);   /* let the STM32 take each slot before the next one */

      if (g_timer[idx].enabled) anyEnabled = true;
    }

    if (anyEnabled) {
      sendPacket("@TIMER:ON#");
      g_liveMode = "TIMER";
    } else if (g_liveMode == "TIMER") {
      sendPacket("@TIMER:OFF#");
      g_liveMode = "STANDBY";
    }

    saveToEEPROM();
    server.send(200, "text/plain", anyEnabled ? "Timer saved and started"
                                              : "Timer saved (no slot enabled)");
  });

  server.on("/timer_stop", HTTP_GET, []() {
    sendPacket("@TIMER:OFF#");
    if (g_liveMode == "TIMER") g_liveMode = "STANDBY";
    server.send(200, "text/plain", "Timer Stopped");
  });

  /* ── COUNTDOWN PAGE ── */
  server.on("/countdown", HTTP_GET, []() {
    noCachePage();
    server.send(200, "text/html", countdownModeHtml);
  });

  /* duration arrives in seconds from the page; the STM32 takes minutes */
  server.on("/start_countdown", HTTP_GET, []() {
    int dur = server.arg("duration").toInt();
    if (dur < 60) dur = 60;
    if (dur > 10800) dur = 10800;
    int mins = (dur + 59) / 60;

    g_countdown.duration = mins * 60;
    g_countdown.remaining = mins * 60;
    g_countdown.paused = false;
    g_countdown.active = true;

    sendPacket("@COUNTDOWN:ON:" + String(mins) + "#");
    g_liveMode = "COUNTDOWN";

    saveToEEPROM();
    server.send(200, "text/plain", "Countdown Started");
  });

  server.on("/countdown_stop", HTTP_GET, []() {
    g_countdown.active = false;
    g_countdown.remaining = 0;
    g_countdown.paused = false;

    sendPacket("@COUNTDOWN:OFF#");
    if (g_liveMode == "COUNTDOWN") g_liveMode = "STANDBY";

    server.send(200, "text/plain", "Countdown Stopped");
  });

  /* ── TWIST PAGE ── */
  server.on("/twist", HTTP_GET, []() {
    noCachePage();
    server.send(200, "text/html", twistModeHtml);
  });

  /* ── MOTOR MANUAL ON/OFF TOGGLE ── */
  server.on("/motor_toggle", HTTP_GET, []() {
    setCORSHeaders();

    /* Client report 12-10, item 12: the button must never show OFF while
     * the motor runs. One tap sets the motor directly (the device goes to
     * Manual from any mode), and the page shows only what the device
     * reports back - never a guessed state. */
    bool wantOn = (g_motorStatus != "ON");
    bool ok = requestFromDevice(wantOn ? "@MANUAL:MOTOR_ON#" : "@MANUAL:MOTOR_OFF#",
                                "@STATUS:", 1);
    if (!ok) ok = requestFromDevice("@STATUS#", "@STATUS:", 1);

    String j = "{\"motor\":" + jsonStr(g_motorStatus) + ",\"mode\":" + jsonStr(g_liveMode) +
               ",\"fromDevice\":" + String(ok ? 1 : 0) + "}";
    server.send(200, "application/json", j);
  });

  /* On/off durations in minutes. TWIST:SET also starts twist on the
     STM32, so no separate mode packet is sent. */
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
    g_liveMode = "TWIST";

    saveToEEPROM();
    server.send(200, "text/plain", "Twist saved and started");
  });

  server.on("/twist_stop", HTTP_GET, []() {
    g_twist.active = false;
    sendPacket("@TWIST:OFF#");
    if (g_liveMode == "TWIST") g_liveMode = "STANDBY";
    server.send(200, "text/plain", "Twist Stopped");
  });

  /* ── SETTINGS PAGE ── */
  server.on("/settings", HTTP_GET, []() {
    noCachePage();
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

    /* The STM32 answers a SET with @SOK and its stored values */
    bool ok = requestFromDevice("", "@SETTINGS_DATA:", 1);
    saveToEEPROM();
    server.send(200, "text/plain", ok ? "Settings saved on device"
                                      : "Sent - device did not confirm");
  });

  /* Voltage / current calibration against a meter: @CAL:V:<volts># or
     @CAL:I:<amps>#; the device answers @CAL_OK# or @CAL_ERR#. */
  server.on("/calibrate", HTTP_GET, []() {
    setCORSHeaders();
    String type = server.arg("type");
    String value = server.arg("value");
    if ((type != "V" && type != "I") || value.length() == 0) {
      server.send(400, "text/plain", "Bad request");
      return;
    }
    sendPacket("@CAL:" + type + ":" + value + "#");
    bool ok = requestFromDevice("", "@CAL_OK#", 1);
    server.send(200, "text/plain", ok ? "Calibrated - device confirmed"
                                      : "Not accepted (motor must run for current; 50-400 V for voltage)");
  });

  /* Level source (N-08): mode=wired (Main board probes) or wireless
     (transmitter). The device answers @SOURCE_OK# then its settings with
     SRC=0/1; the page shows what the device reports. */
  server.on("/source", HTTP_GET, []() {
    setCORSHeaders();
    String mode = server.arg("mode");
    if (mode != "wired" && mode != "wireless") {
      server.send(400, "text/plain", "Bad request");
      return;
    }
    int want = (mode == "wireless") ? 1 : 0;
    sendPacket(want ? "@SOURCE:WIRELESS#" : "@SOURCE:WIRED#");
    bool ok = requestFromDevice("", "@SETTINGS_DATA:", 1) && g_settings.levelSource == want;
    server.send(200, "text/plain", ok ? (want ? "Level from transmitter - device confirmed"
                                              : "Level from device probes - device confirmed")
                                      : "Not confirmed by the device");
  });

  /* Set the device clock from the phone: @TIME:SET:HH:MM:SS:DD:MM:YYYY:DOW#
     (DOW 1 = Sunday). The device answers @TIME_SET_OK# then @TIME_DATA:..#,
     or @TIME_ERR#. The RTC has no backup cell, so this is needed after
     every power cut. */
  server.on("/set_time", HTTP_GET, []() {
    setCORSHeaders();
    const char *keys[7] = {"h", "m", "s", "d", "mo", "y", "dow"};
    const int   lo[7]   = {0, 0, 0, 1, 1, 2024, 1};
    const int   hi[7]   = {23, 59, 59, 31, 12, 2099, 7};
    int v[7];
    for (int i = 0; i < 7; i++) {
      String a = server.arg(keys[i]);
      v[i] = a.toInt();
      if (a.length() == 0 || v[i] < lo[i] || v[i] > hi[i]) {
        server.send(400, "text/plain", "Bad time");
        return;
      }
    }
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "@TIME:SET:%02d:%02d:%02d:%02d:%02d:%04d:%d#",
             v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
    bool ok = requestFromDevice(cmd, "@TIME_DATA:", 1);
    server.send(200, "text/plain", ok ? "Device clock set"
                                      : "Sent - device did not confirm");
  });

  server.on("/factory_reset", HTTP_GET, []() {
    sendPacket("@FACTORY:RESET#");
    bool ok = requestFromDevice("", "@SETTINGS_DATA:", 1);
    saveToEEPROM();
    server.send(200, "text/plain", ok ? "Factory reset done"
                                      : "Sent - device did not confirm");
  });

  server.begin();
}


/* ======================================================
   MAIN LOOP HANDLER
====================================================== */

void handleClient() {
  server.handleClient();
  esp_uart_autoStatusRequest();

  char rxBuf[ESP_UART_RX_BUFFER_SIZE];
  if (esp_uart_receive(rxBuf, sizeof(rxBuf), 10)) {
    String pkt = String(rxBuf);
    pkt.trim();
    if (!handleDevicePacket(pkt))
      esp_uart_processCommand(rxBuf);
  }
}
